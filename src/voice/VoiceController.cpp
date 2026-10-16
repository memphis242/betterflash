#include "VoiceController.h"

#include "VoiceWorker.h"
#include "CustomSttTests.h"
#include "TranscriptCorrector.h"
#include "LocalTtsWorker.h"
#include "RemoteTts.h"
#include "integrations/ApiKeyBinding.h"
#include "integrations/ApiKeyRegistry.h"
#include "models/ModelDownload.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QBuffer>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QLibrary>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSettings>
#include <QStandardPaths>
#include <QMediaDevices>
#include <QTextToSpeech>
#include <QThread>
#include <QTimer>
#include <utility>
#include <algorithm>
#ifdef Q_OS_ANDROID
#include <QMicrophonePermission>
#endif

namespace {
constexpr int kCurrentSttProfileVersion = 3;

QString managedSttModelDirectory(const QString &root, const QString &profile) {
    Q_ASSERT(profile == QStringLiteral("small-en") || profile == QStringLiteral("balanced-en") || profile == QStringLiteral("whisper-turbo"));
    if (profile == QStringLiteral("whisper-turbo")) return QDir(root).filePath(QStringLiteral("whisper/ggml-large-v3-turbo-q8_0.bin"));
    return QDir(root).filePath(profile == QStringLiteral("balanced-en") ? QStringLiteral("model-balanced-en") : QStringLiteral("model"));
}

QStringList validationWords(const QString &text) {
    return text.trimmed().toCaseFolded().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString numericValidationText(QString text) {
    text = text.toCaseFolded();
    static const QStringList words = {QStringLiteral("zero"), QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three"),
        QStringLiteral("four"), QStringLiteral("five"), QStringLiteral("six"), QStringLiteral("seven"), QStringLiteral("eight"), QStringLiteral("nine")};
    for (int i = 0; i < words.size(); ++i)
        text.replace(QRegularExpression(QStringLiteral("\\b%1\\b").arg(words.at(i))), QString::number(i));
    text.replace(QRegularExpression(QStringLiteral("\\boh\\b")), QStringLiteral("0"));
    text.remove(QRegularExpression(QStringLiteral("[.,!?;:'\\\"-]")));
    QString normalized;
    normalized.reserve(text.size() * 2);
    bool previousDigit = false;
    for (const QChar character : text) {
        const bool digit = character.isDigit();
        if (digit && previousDigit) normalized.append(QLatin1Char(' '));
        normalized.append(character);
        previousDigit = digit;
        if (!digit) previousDigit = false;
    }
    return normalized.simplified();
}

void validationScore(const QString &expected, const QString &actual, int *substitutions, int *deletions,
    int *insertions, double *wordErrorRate) {
    const QStringList reference = validationWords(expected);
    const QStringList hypothesis = validationWords(actual);
    const int columns = hypothesis.size() + 1;
    QVector<int> distance((reference.size() + 1) * columns);
    QVector<char> operation(distance.size(), 0);
    for (int row = 0; row <= reference.size(); ++row) distance[row * columns] = row;
    for (int column = 0; column <= hypothesis.size(); ++column) distance[column] = column;
    for (int row = 1; row <= reference.size(); ++row) for (int column = 1; column <= hypothesis.size(); ++column) {
        const int substitution = distance[(row - 1) * columns + column - 1]
            + (reference.at(row - 1) == hypothesis.at(column - 1) ? 0 : 1);
        const int deletion = distance[(row - 1) * columns + column] + 1;
        const int insertion = distance[row * columns + column - 1] + 1;
        distance[row * columns + column] = std::min({substitution, deletion, insertion});
        operation[row * columns + column] = distance[row * columns + column] == substitution ? 0
            : distance[row * columns + column] == deletion ? 1 : 2;
    }
    int row = reference.size(), column = hypothesis.size();
    int substitutionsValue = 0, deletionsValue = 0, insertionsValue = 0;
    while (row > 0 || column > 0) {
        const char step = row > 0 && column > 0 ? operation[row * columns + column] : row > 0 ? 1 : 2;
        if (step == 0) { if (reference.at(row - 1) != hypothesis.at(column - 1)) ++substitutionsValue; --row; --column; }
        else if (step == 1) { ++deletionsValue; --row; }
        else { ++insertionsValue; --column; }
    }
    *substitutions = substitutionsValue;
    *deletions = deletionsValue;
    *insertions = insertionsValue;
    *wordErrorRate = reference.isEmpty() ? (hypothesis.isEmpty() ? 0.0 : 1.0)
        : static_cast<double>(substitutionsValue + deletionsValue + insertionsValue) / reference.size();
}

}

VoiceController::VoiceController(QObject *parent, ApiKeyRegistry *registry)
    : QObject(parent), m_registry(registry ? registry : new ApiKeyRegistry(this)),
      m_remoteTts(new RemoteTts(this, m_registry)),
      m_secret(m_registry->binding(QStringLiteral("speech-recognition"), this)),
      m_speech(nullptr), m_customSttTests(new CustomSttTests(this)), m_corrector(new TranscriptCorrector(this)) {
    m_secret->setEndpoint(QStringLiteral("https://api.groq.com/openai/v1/audio/transcriptions"));
    connect(m_corrector, &TranscriptCorrector::configurationChanged, this, [this] { cancelTranscriptCorrection(); });
    m_localDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("models/voice"));
    m_sttDownload = new ModelDownload(QDir(m_localDirectory).filePath(QStringLiteral("stt")), this);
    m_ttsDownload = new ModelDownload(QDir(m_localDirectory).filePath(QStringLiteral("tts")), this);
    m_fixtureDownload = new ModelDownload(QDir(m_localDirectory).filePath(QStringLiteral("validation")), this);
#ifdef Q_OS_ANDROID
    m_ttsProfile = QStringLiteral("amy-low");
#endif
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice"));
    const QString storedModelPath = settings.value(QStringLiteral("modelPath")).toString().trimmed();
    const QString storedProfile = settings.value(QStringLiteral("sttProfile")).toString().trimmed();
    const int storedProfileVersion = settings.value(QStringLiteral("sttProfileVersion"), 0).toInt();
    const bool desktop =
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64) && !defined(Q_OS_ANDROID)
        true;
#else
        false;
#endif
    m_sttProfile = desktop ? QStringLiteral("whisper-turbo") : QStringLiteral("small-en");
    if (storedProfileVersion >= kCurrentSttProfileVersion
        && (storedProfile == QStringLiteral("small-en") || storedProfile == QStringLiteral("balanced-en") || storedProfile == QStringLiteral("whisper-turbo")))
        m_sttProfile = storedProfile;
    const QString oldManagedPath = QDir(m_sttDownload->directory()).filePath(QStringLiteral("model"));
    const QString balancedManagedPath = QDir(m_sttDownload->directory()).filePath(QStringLiteral("model-balanced-en"));
    const bool oldManagedDefault = storedModelPath.isEmpty() || storedModelPath == oldManagedPath || storedModelPath == balancedManagedPath;
    if (!desktop && storedProfileVersion < kCurrentSttProfileVersion)
        m_sttProfile = QStringLiteral("small-en");
    if (storedModelPath.isEmpty() || (desktop && storedProfileVersion < kCurrentSttProfileVersion && oldManagedDefault))
        m_modelPath = managedSttModelDirectory(m_sttDownload->directory(), m_sttProfile);
    else
        m_modelPath = storedModelPath;
    const QString provider = settings.value(QStringLiteral("provider"), QStringLiteral("local")).toString().trimmed().toLower();
    if (provider == QStringLiteral("local") || provider == QStringLiteral("groq")) m_sttProvider = provider;
    const QString storedReviewMode = settings.value(QStringLiteral("reviewMode"), QStringLiteral("trigger")).toString().trimmed().toLower();
    if (storedReviewMode == QStringLiteral("trigger") || storedReviewMode == QStringLiteral("always-on"))
        m_reviewMode = storedReviewMode;
    const QString model = settings.value(QStringLiteral("sttModel"), m_sttModel).toString().trimmed();
    static const QRegularExpression validModel(QStringLiteral(R"(^[A-Za-z0-9._:-]{1,128}$)"));
    if (validModel.match(model).hasMatch()) m_sttModel = model;
    m_recognitionVocabulary = settings.value(QStringLiteral("recognitionVocabulary")).toString().trimmed().left(4096);
    m_sttAcceleration = settings.value(QStringLiteral("sttAcceleration"), QStringLiteral("automatic")).toString().trimmed().toLower();
    if (m_sttAcceleration != QStringLiteral("automatic") && m_sttAcceleration != QStringLiteral("cpu")) m_sttAcceleration = QStringLiteral("automatic");
    const QString ttsProvider = settings.value(QStringLiteral("ttsProvider"), m_ttsProvider).toString().trimmed().toLower();
    if (ttsProvider == QStringLiteral("local") || ttsProvider == QStringLiteral("system") || ttsProvider == QStringLiteral("remote")) m_ttsProvider = ttsProvider;
    const QString ttsProfile = settings.value(QStringLiteral("ttsProfile"), m_ttsProfile).toString().trimmed();
    if (ttsProfile == QStringLiteral("amy-low") || ttsProfile == QStringLiteral("ljspeech-medium")) m_ttsProfile = ttsProfile;
    settings.beginGroup(QStringLiteral("remoteTts"));
    m_remoteTts->setEndpoint(settings.value(QStringLiteral("endpoint"), m_remoteTts->endpoint()).toString());
    m_remoteTts->setModel(settings.value(QStringLiteral("model"), m_remoteTts->model()).toString());
    m_remoteTts->setVoice(settings.value(QStringLiteral("voice"), m_remoteTts->voice()).toString());
    settings.endGroup();
    if (storedProfileVersion < kCurrentSttProfileVersion) {
        settings.setValue(QStringLiteral("sttProfileVersion"), kCurrentSttProfileVersion);
        settings.setValue(QStringLiteral("sttProfile"), m_sttProfile);
        settings.setValue(QStringLiteral("modelPath"), m_modelPath);
    }
    configureDownloads();
    for (ModelDownload *const download : {m_sttDownload, m_ttsDownload, m_fixtureDownload}) {
        connect(download, &ModelDownload::changed, this, [this] { emit modelReadinessChanged(); });
        connect(download, &ModelDownload::finished, this, [this, download](const bool success) {
            if (success) { continuePendingOperations(); return; }
            const QString message = download->error().isEmpty() ? QStringLiteral("The model download was canceled. Start it again when ready.") : download->error();
            if (download == m_sttDownload || download == m_fixtureDownload) {
                if (m_sttTestBusy) finishSttTest(false, QStringLiteral("Speech recognition validation failed"), message);
                if (download == m_sttDownload && m_enabled) { setEnabled(false); setError(QStringLiteral("VOICE_MODEL_DOWNLOAD"), message); }
            }
            if (download == m_ttsDownload) {
                if (m_ttsTestBusy) finishTtsTest(false, QStringLiteral("Speech output validation failed"), message);
                if (m_speaking) speechError(message);
            }
        });
    }
    m_lastKey = m_secret->key();
    m_customSttTests->setTranscriptCorrector(m_corrector);
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key(), m_recognitionVocabulary, m_sttAcceleration);
    connect(m_customSttTests, &CustomSttTests::stateChanged, this, [this] {
        const bool active = m_customSttTests->busy() || m_customSttTests->recording();
        if (active && !m_customVoiceSuspended) {
            m_customVoiceSuspended = true;
            m_testSavedWantsListening = m_reviewMode == QStringLiteral("always-on") && m_wantsListening;
            stopListening();
            stopSpeaking();
        }
        if (active) setModelTestState(true, m_customSttTests->status(), m_customSttTests->error());
        else if (m_customVoiceSuspended) {
            m_customVoiceSuspended = false;
            setModelTestState(m_sttTestBusy || m_ttsTestBusy, m_customSttTests->status(), m_customSttTests->error());
            if (m_enabled) { m_wantsListening = m_testSavedWantsListening; setMicrophoneArmed(m_wantsListening); updateCapture(); }
        }
    });
    connect(m_secret, &ApiKeyBinding::changed, this, [this] {
        emit hasApiKeyChanged();
        emit keyStorageStatusChanged();
        const QString key = m_secret->key();
        if (key != m_lastKey) {
            m_lastKey = key;
            m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, key, m_recognitionVocabulary, m_sttAcceleration);
            if (m_sttProvider == QStringLiteral("groq")) {
                if (m_sttTestBusy)
                    finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech provider key changed."));
                resetSttTestReport();
            }
            if (m_enabled && m_sttProvider == QStringLiteral("groq")) restartWorker();
        }
    });
    connect(m_remoteTts, &RemoteTts::configurationChanged, this, [this] {
        if (m_ttsProvider == QStringLiteral("remote")) {
            stopSpeaking();
            if (m_ttsTestBusy) finishTtsTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Remote speech configuration changed."));
            resetTtsTestReport();
        }
        QSettings settings;
        settings.beginGroup(QStringLiteral("voice"));
        settings.beginGroup(QStringLiteral("remoteTts"));
        settings.setValue(QStringLiteral("endpoint"), m_remoteTts->endpoint());
        settings.setValue(QStringLiteral("model"), m_remoteTts->model());
        settings.setValue(QStringLiteral("voice"), m_remoteTts->voice());
        settings.endGroup();
        emit modelReadinessChanged();
    });
    connect(m_remoteTts, &RemoteTts::credentialChanged, this, [this] {
        if (m_ttsProvider == QStringLiteral("remote")) {
            if (m_ttsTestBusy) finishTtsTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Remote speech API key changed."));
            if (m_speaking) stopSpeaking();
            resetTtsTestReport();
        }
    });
    connect(m_remoteTts, &RemoteTts::keyChanged, this, [this] { emit modelReadinessChanged(); });
    connect(m_remoteTts, &RemoteTts::readinessChanged, this, [this] { emit modelReadinessChanged(); });
    connect(m_remoteTts, &RemoteTts::audioReady, this, [this](const QByteArray &pcm, const QAudioFormat &format,
        const double seconds, const quint64 request) {
        if (request != m_speechGeneration || !m_speaking) return;
        m_playbackFormat = format;
        startLocalPlayback(pcm, request, seconds);
    });
    connect(m_remoteTts, &RemoteTts::failed, this, [this](const QString &message, const quint64 request) {
        if (request == m_speechGeneration && m_speaking) speechError(message);
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { setEnabled(false); });
}

VoiceController::~VoiceController() {
    m_enabled = false;
    ++m_testGeneration;
    if (m_testThread) m_testThread->requestInterruption();
    if (m_testWorker && m_testThread) {
        const QPointer<VoiceWorker> worker = m_testWorker;
        const QPointer<QThread> thread = m_testThread;
        QMetaObject::invokeMethod(worker, [worker, thread] { if (worker) worker->shutdown(); if (thread) thread->quit(); }, Qt::QueuedConnection);
    }
    m_synthesisCancelled->store(true);
    retireTtsWorker();
    retireWorker();
    if (m_speech) m_speech->stop(QTextToSpeech::BoundaryHint::Immediate);
    if (m_audioSink) m_audioSink->stop();
    // A worker owns all decoding and audio resources until its event loop exits.
    const QDeadlineTimer deadline(250);
    for (const QPointer<QThread> &thread : std::as_const(m_threads))
        if (thread && thread->isRunning() && !deadline.hasExpired()) thread->wait(deadline);
}

bool VoiceController::hasApiKey() const { return m_secret->hasKey(); }
QString VoiceController::keyStorageStatus() const { return m_secret->storageStatus(); }
QString VoiceController::keyStorageState() const { return m_secret->storageState(); }
QObject *VoiceController::customSttTests() const { return m_customSttTests; }
QObject *VoiceController::remoteTts() const { return m_remoteTts; }
QObject *VoiceController::transcriptCorrector() const { return m_corrector; }

void VoiceController::attachLanguageModel(Summarizer *summarizer) { m_corrector->attachSummarizer(summarizer); }

void VoiceController::cancelTranscriptCorrection() {
    const quint64 job = m_correctionJob;
    ++m_correctionGeneration;
    m_correctionJob = 0;
    m_correctionPending = false;
    m_processing = false;
    if (job != 0) m_corrector->cancel(job);
}

void VoiceController::persistConfiguration() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice"));
    settings.setValue(QStringLiteral("modelPath"), m_modelPath);
    settings.setValue(QStringLiteral("provider"), m_sttProvider);
    settings.setValue(QStringLiteral("reviewMode"), m_reviewMode);
    settings.setValue(QStringLiteral("sttModel"), m_sttModel);
    settings.setValue(QStringLiteral("sttProfile"), m_sttProfile);
    settings.setValue(QStringLiteral("recognitionVocabulary"), m_recognitionVocabulary);
    settings.setValue(QStringLiteral("sttAcceleration"), m_sttAcceleration);
    settings.setValue(QStringLiteral("sttProfileVersion"), kCurrentSttProfileVersion);
    settings.setValue(QStringLiteral("ttsProvider"), m_ttsProvider);
    settings.setValue(QStringLiteral("ttsProfile"), m_ttsProfile);
    settings.beginGroup(QStringLiteral("remoteTts"));
    settings.setValue(QStringLiteral("endpoint"), m_remoteTts->endpoint());
    settings.setValue(QStringLiteral("model"), m_remoteTts->model());
    settings.setValue(QStringLiteral("voice"), m_remoteTts->voice());
    settings.endGroup();
}

void VoiceController::setModelPath(const QString &path) {
    cancelTranscriptCorrection();
    const QString value = path.trimmed();
    if (m_modelPath == value) return;
    if (m_sttTestBusy) finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech model path changed."));
    m_modelPath = value;
    resetSttTestReport();
    persistConfiguration();
    emit modelPathChanged();
    emit modelReadinessChanged();
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key(), m_recognitionVocabulary, m_sttAcceleration);
    if (m_enabled && m_sttProvider == QStringLiteral("local")) restartWorker();
}

void VoiceController::setSttProvider(const QString &provider) {
    cancelTranscriptCorrection();
    QString value = provider.trimmed().toLower();
    if (value == QStringLiteral("local test")) value = QStringLiteral("local");
    if (value != QStringLiteral("groq") && value != QStringLiteral("local")) {
        setError(QStringLiteral("VOICE_PROVIDER_INVALID"), QStringLiteral("Choose local speech recognition or Groq."));
        return;
    }
    if (m_sttProvider == value) return;
    if (m_sttTestBusy) finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech provider changed."));
    m_sttProvider = value;
    resetSttTestReport();
    persistConfiguration();
    emit sttProviderChanged();
    emit modelReadinessChanged();
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key(), m_recognitionVocabulary, m_sttAcceleration);
    if (m_enabled) restartWorker();
}

void VoiceController::setSttModel(const QString &model) {
    cancelTranscriptCorrection();
    const QString value = model.trimmed();
    static const QRegularExpression validModel(QStringLiteral(R"(^[A-Za-z0-9._:-]{1,128}$)"));
    if (!validModel.match(value).hasMatch()) {
        setError(QStringLiteral("VOICE_MODEL_NAME_INVALID"), QStringLiteral("Enter a speech model identifier using letters, numbers, periods, dashes, or underscores."));
        return;
    }
    if (m_sttModel == value) return;
    if (m_sttTestBusy) finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech model changed."));
    m_sttModel = value;
    resetSttTestReport();
    persistConfiguration();
    emit sttModelChanged();
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key(), m_recognitionVocabulary, m_sttAcceleration);
    if (m_enabled && m_sttProvider == QStringLiteral("groq")) restartWorker();
}

void VoiceController::setSttProfile(const QString &profile) {
    cancelTranscriptCorrection();
    const QString value = profile.trimmed();
    if (value != QStringLiteral("small-en") && value != QStringLiteral("balanced-en") && value != QStringLiteral("whisper-turbo")) {
        setError(QStringLiteral("VOICE_PROFILE_INVALID"), QStringLiteral("Choose a supported English recognition profile."));
        return;
    }
    if (m_sttProfile == value) return;
    if (m_sttTestBusy) finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech profile changed."));
    m_sttProfile = value;
    const QString managedPath = managedSttModelDirectory(m_sttDownload->directory(), m_sttProfile);
    if (m_modelPath != managedPath) {
        m_modelPath = managedPath;
        emit modelPathChanged();
    }
    resetSttTestReport();
    configureSttDownloads();
    persistConfiguration();
    emit sttProfileChanged();
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key(), m_recognitionVocabulary, m_sttAcceleration);
    emit modelReadinessChanged();
    if (m_enabled && m_sttProvider == QStringLiteral("local")) restartWorker();
}

void VoiceController::setRecognitionVocabulary(const QString &vocabulary) {
    cancelTranscriptCorrection();
    const QString value = vocabulary.trimmed().left(4096);
    if (m_recognitionVocabulary == value) return;
    m_recognitionVocabulary = value;
    m_corrector->setVocabulary(value);
    persistConfiguration();
    emit recognitionVocabularyChanged();
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key(), m_recognitionVocabulary, m_sttAcceleration);
    if (m_enabled && m_sttProvider == QStringLiteral("local")) restartWorker();
}

void VoiceController::setSttAcceleration(const QString &acceleration) {
    const QString value = acceleration.trimmed().toLower();
    if (value != QStringLiteral("automatic") && value != QStringLiteral("cpu")) {
        setError(QStringLiteral("VOICE_ACCELERATION_INVALID"), QStringLiteral("Choose automatic or CPU speech recognition."));
        return;
    }
    if (m_sttAcceleration == value) return;
    cancelTranscriptCorrection();
    if (m_sttTestBusy) finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech acceleration changed."));
    m_sttAcceleration = value;
    resetSttTestReport();
    persistConfiguration();
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key(), m_recognitionVocabulary, m_sttAcceleration);
    emit sttAccelerationChanged();
    if (m_enabled && m_sttProvider == QStringLiteral("local")) restartWorker();
}

void VoiceController::setTtsProvider(const QString &provider) {
    const QString value = provider.trimmed().toLower();
    if (value != QStringLiteral("local") && value != QStringLiteral("system") && value != QStringLiteral("remote")) {
        setError(QStringLiteral("VOICE_TTS_PROVIDER_INVALID"), QStringLiteral("Choose local, remote cloud, or the operating system speech engine."));
        return;
    }
    if (m_ttsProvider == value) return;
    stopSpeaking();
    retireTtsWorker();
    if (m_ttsTestBusy) finishTtsTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech output provider changed."));
    m_ttsProvider = value;
    resetTtsTestReport();
    persistConfiguration();
    emit ttsProviderChanged();
    emit modelReadinessChanged();
}

void VoiceController::setTtsProfile(const QString &profile) {
    const QString value = profile.trimmed();
    if (value != QStringLiteral("amy-low") && value != QStringLiteral("ljspeech-medium")) {
        setError(QStringLiteral("VOICE_PROFILE_INVALID"), QStringLiteral("Choose Amy low or LJSpeech medium for the local voice."));
        return;
    }
    if (m_ttsProfile == value) return;
    stopSpeaking();
    retireTtsWorker();
    if (m_ttsTestBusy) finishTtsTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech output voice changed."));
    m_ttsProfile = value;
    resetTtsTestReport();
    configureDownloads(true);
    persistConfiguration();
    emit ttsProfileChanged();
    emit modelReadinessChanged();
}

void VoiceController::configureDownloads(const bool onlyTts) {
    if (!onlyTts) {
        m_fixtureDownload->configure({{QUrl(QStringLiteral("https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.45/python/example/test.wav")),
            QStringLiteral("test.wav"), 265914, QByteArrayLiteral("dcfea5712c43a43ba7ae8083afb39d36993e5a69c46e88b68aaa72b65cb615bb"), {}, {}}});
    }
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64) && !defined(Q_OS_ANDROID)
    if (!onlyTts) {
        configureSttDownloads();
    }
    const struct ModelDownloadAsset runtime{QUrl(QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.10.46/sherpa-onnx-v1.10.46-linux-x64-shared.tar.bz2")),
        QStringLiteral("sherpa-onnx-v1.10.46-linux-x64-shared.tar.bz2"), 20540943,
        QByteArrayLiteral("120dbddd6a97bc901ae007c7621fe1b48904dea444718fac5470972d95f80d2d"),
        QStringLiteral("sherpa-onnx-v1.10.46-linux-x64-shared/"), QStringLiteral("runtime")};
    const bool compact = m_ttsProfile == QStringLiteral("amy-low");
    const QString modelName = compact ? QStringLiteral("vits-piper-en_US-amy-low") : QStringLiteral("vits-piper-en_US-ljspeech-medium");
    const struct ModelDownloadAsset model{QUrl(QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/%1.tar.bz2").arg(modelName)),
        modelName + QStringLiteral(".tar.bz2"), compact ? 67095344 : 67169893,
        compact ? QByteArrayLiteral("c70f5284a09a7fd4ed203b39b2ff51cac1432b422b852eb647b481dade3cf639")
                : QByteArrayLiteral("3dfb4b759d8be032a4903a9538d128b0fda2a06ab1de6cbc2d93a97e2dd83dba"),
        modelName + QLatin1Char('/'), QStringLiteral("models/") + m_ttsProfile};
    m_ttsDownload->configure({runtime, model,
        {QUrl(QStringLiteral("https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v1.10.46/LICENSE")),
            QStringLiteral("licenses/Sherpa-ONNX-Apache-2.0.txt"), 11358,
            QByteArrayLiteral("cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30"), {}, {}},
        {QUrl(QStringLiteral("https://raw.githubusercontent.com/csukuangfj/espeak-ng/f6fed6c58b5e0998b8e68c6610125e2d07d595a7/COPYING")),
            QStringLiteral("licenses/espeak-ng-GPL-3.0.txt"), 35147,
            QByteArrayLiteral("8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903"), {}, {}},
        {QUrl(QStringLiteral("https://raw.githubusercontent.com/microsoft/onnxruntime/v1.17.1/LICENSE")),
            QStringLiteral("licenses/ONNX-Runtime-1.17.1-MIT.txt"), 1073,
            QByteArrayLiteral("2f07c72751aed99790b8a4869cf2311df85a860b22ded05fa22803587a48922c"), {}, {}}});
#else
    if (!onlyTts) m_sttDownload->configure({});
    m_ttsDownload->configure({});
#endif
}

void VoiceController::configureSttDownloads() {
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64) && !defined(Q_OS_ANDROID)
    if (m_sttProfile == QStringLiteral("whisper-turbo")) {
        const struct ModelDownloadAsset model{
            QUrl(QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-large-v3-turbo-q8_0.bin")),
            QStringLiteral("whisper/ggml-large-v3-turbo-q8_0.bin"), 874188075,
            QByteArrayLiteral("317eb69c11673c9de1e1f0d459b253999804ec71ac4c23c17ecf5fbe24e259a1"), {}, {}};
        const QSignalBlocker suppressCanceledDownloadSignals(m_sttDownload);
        m_sttDownload->configure({model});
        emit modelReadinessChanged();
        return;
    }
    const bool balanced = m_sttProfile == QStringLiteral("balanced-en");
    const struct ModelDownloadAsset model{
        QUrl(balanced ? QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-en-us-0.22-lgraph.zip")
            : QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip")),
        balanced ? QStringLiteral("vosk-model-en-us-0.22-lgraph.zip") : QStringLiteral("vosk-model-small-en-us-0.15.zip"),
        balanced ? 130557655 : 41205931,
        balanced ? QByteArrayLiteral("d9838b4aaa82a75c4a17f5aca300eaca129aaab2a7cbf951bafbb500eb9c4334")
                 : QByteArrayLiteral("30f26242c4eb449f948e42cb302dd7a686cb29a3423a8367f99ff41780942498"),
        balanced ? QStringLiteral("vosk-model-en-us-0.22-lgraph/") : QStringLiteral("vosk-model-small-en-us-0.15/"),
        balanced ? QStringLiteral("model-balanced-en") : QStringLiteral("model")};
    const QSignalBlocker suppressCanceledDownloadSignals(m_sttDownload);
    m_sttDownload->configure({model,
        {QUrl(QStringLiteral("https://files.pythonhosted.org/packages/fc/ca/83398cfcd557360a3d7b2d732aee1c5f6999f68618d1645f38d53e14c9ff/vosk-0.3.45-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.whl")),
            QStringLiteral("vosk-0.3.45-linux-x64.whl"), 7173758,
            QByteArrayLiteral("25e025093c4399d7278f543568ed8cc5460ac3a4bf48c23673ace1e25d26619f"),
            QStringLiteral("vosk/"), QStringLiteral("runtime")},
        {QUrl(QStringLiteral("https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.45/COPYING")),
            QStringLiteral("licenses/Vosk-Apache-2.0.txt"), 10173,
            QByteArrayLiteral("bb618af833c817ab01c1564bd3ec75ba4b4f9cf30910a4a2168e30a674ee8cc6"), {}, {}}});
    emit modelReadinessChanged();
#else
    m_sttDownload->configure({});
#endif
}

bool VoiceController::sttModelReady() const {
    if (m_sttProvider == QStringLiteral("groq")) return m_secret->hasKey();
    if (m_modelPath == managedSttModelDirectory(m_sttDownload->directory(), m_sttProfile) && !m_sttDownload->ready()) return false;
    if (m_modelPath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)) {
        const QString runner = qEnvironmentVariable("BETTERFLASH_WHISPER_EXECUTABLE").trimmed().isEmpty()
            ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("betterflash-whisper"))
            : qEnvironmentVariable("BETTERFLASH_WHISPER_EXECUTABLE").trimmed();
        return (m_modelPath != managedSttModelDirectory(m_sttDownload->directory(), m_sttProfile) || m_sttDownload->ready())
            && QFileInfo(m_modelPath).isFile() && QFileInfo(runner).isExecutable();
    }
    const QDir modelDirectory(m_modelPath);
    if (!modelDirectory.exists()) return false;
    const QStringList libraryCandidates = {qEnvironmentVariable("BETTERFLASH_VOSK_LIBRARY"),
        QDir(m_sttDownload->directory()).filePath(QStringLiteral("runtime/libvosk.so")),
        modelDirectory.filePath(QStringLiteral("libvosk.so")),
        modelDirectory.filePath(QStringLiteral("libvosk.dll")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lib/libvosk.so")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lib/libvosk.dll"))};
    const bool libraryPresent = std::any_of(libraryCandidates.cbegin(), libraryCandidates.cend(), [](const QString &candidate) {
        return !candidate.isEmpty() && QFileInfo(candidate).isFile();
    });
    return modelDirectory.exists(QStringLiteral("am/final.mdl")) && modelDirectory.exists(QStringLiteral("conf/model.conf")) && libraryPresent;
}

QString VoiceController::sttModelStatus() const {
    if (m_sttProvider == QStringLiteral("groq")) return m_secret->hasKey() ? QStringLiteral("Groq key is available") : QStringLiteral("Add a Groq API key");
    const QString managedPath = managedSttModelDirectory(m_sttDownload->directory(), m_sttProfile);
    if (sttModelReady()) {
        if (m_modelPath != managedPath) return QStringLiteral("Custom speech recognition model is ready for offline use");
        if (m_sttProfile == QStringLiteral("whisper-turbo")) return QStringLiteral("Whisper large v3 turbo is ready for offline use");
        return m_sttProfile == QStringLiteral("balanced-en")
            ? QStringLiteral("Vosk English 0.22 balanced recognition is ready for offline use")
            : QStringLiteral("Vosk small English recognition is ready for offline use");
    }
    if (m_modelPath != managedPath)
        return m_modelPath.isEmpty() ? QStringLiteral("Choose a local speech recognition model path")
            : QStringLiteral("Custom speech recognition path is unavailable: %1").arg(m_modelPath);
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64) && !defined(Q_OS_ANDROID)
    return m_sttDownload->error().isEmpty() ? m_sttDownload->status() : m_sttDownload->error();
#else
    return QStringLiteral("Local recognition for this platform is a draft; use Groq until a native kit is verified");
#endif
}

bool VoiceController::sttModelDownloading() const { return m_sttDownload->downloading() || m_fixtureDownload->downloading(); }
double VoiceController::sttModelProgress() const { return m_sttDownload->downloading() ? m_sttDownload->progress() : m_fixtureDownload->progress(); }

bool VoiceController::ttsReady() const {
    if (m_ttsProvider == QStringLiteral("local")) return m_ttsDownload->ready();
    if (m_ttsProvider == QStringLiteral("remote")) return m_remoteTts->ready();
    return !QTextToSpeech::availableEngines().isEmpty();
}

QString VoiceController::ttsStatus() const {
    if (m_ttsProvider == QStringLiteral("remote")) return m_remoteTts->status();
    if (m_ttsProvider == QStringLiteral("system")) return ttsReady() ? QStringLiteral("System speech engine is ready") : QStringLiteral("No system speech engine is available; choose the managed local voice");
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64) && !defined(Q_OS_ANDROID)
    return m_ttsDownload->error().isEmpty() ? m_ttsDownload->status() : m_ttsDownload->error();
#else
    return QStringLiteral("Local synthesis for this platform is a draft; choose system speech until a native kit is verified");
#endif
}

bool VoiceController::ttsModelDownloading() const { return m_ttsDownload->downloading(); }
double VoiceController::ttsModelProgress() const { return m_ttsDownload->progress(); }

void VoiceController::downloadSttModel() {
    if (m_sttProvider != QStringLiteral("local")) return;
    const QString managedPath = managedSttModelDirectory(m_sttDownload->directory(), m_sttProfile);
    if (m_modelPath != managedPath) setModelPath(managedPath);
    m_sttDownload->start();
}
void VoiceController::downloadTtsModel() { if (m_ttsProvider == QStringLiteral("local")) m_ttsDownload->start(); }
void VoiceController::cancelSttDownload() { m_sttDownload->cancel(); m_fixtureDownload->cancel(); }
void VoiceController::cancelTtsDownload() { m_ttsDownload->cancel(); }

void VoiceController::setModelTestState(const bool busy, const QString &status, const QString &error) {
    m_modelTestBusy = busy;
    m_modelTestStatus = status;
    m_modelTestError = error;
    emit modelTestChanged();
}

void VoiceController::testSttModel() {
    if (m_modelTestBusy || m_customSttTests->busy() || m_customSttTests->recording()) return;
    m_sttTestBusy = true;
    m_pendingSttTest = true;
    m_sttTestError.clear();
    m_sttTestReport = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("running")},
        {QStringLiteral("input"), QStringLiteral("Official Vosk test recording / 16 kHz mono audio")},
        {QStringLiteral("expected"), QStringLiteral("one zero zero zero one nine oh two one oh zero one eight zero three")},
        {QStringLiteral("scoringMode"), m_sttProfile == QStringLiteral("whisper-turbo") ? QStringLiteral("numeric-equivalent") : QStringLiteral("literal-words")}};
    m_sttTestStatus = QStringLiteral("Preparing a known spoken phrase");
    m_testSavedWantsListening = m_reviewMode == QStringLiteral("always-on") && m_wantsListening;
    stopListening();
    stopSpeaking();
    setModelTestState(true, m_sttTestStatus);
    if (m_sttProvider == QStringLiteral("groq") && !hasApiKey()) {
        finishSttTest(false, QStringLiteral("Speech recognition validation failed"), QStringLiteral("Add a Groq API key or choose local recognition."));
        return;
    }
    if (m_sttProvider == QStringLiteral("local") && !sttModelReady()) {
        finishSttTest(false, QStringLiteral("Speech recognition validation failed"), QStringLiteral("VOICE_TEST_MODEL_MISSING: Download the selected local speech model before testing it."));
        return;
    }
    if (!m_sttTestBusy) return;
    if (!m_fixtureDownload->ready()) m_fixtureDownload->start();
    continuePendingOperations();
}

void VoiceController::testTtsModel() {
    if (m_modelTestBusy || m_customSttTests->busy() || m_customSttTests->recording()) return;
    m_ttsTestBusy = true;
    m_pendingTtsTest = true;
    m_ttsTestError.clear();
    m_ttsTestReport = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("running")},
        {QStringLiteral("input"), QStringLiteral("BetterFlash speech test. Your next card is ready.")},
        {QStringLiteral("expected"), QStringLiteral("The selected speech engine synthesizes and plays the complete sample.")}};
    m_ttsTestStatus = QStringLiteral("Preparing a spoken sample");
    m_testSavedWantsListening = m_reviewMode == QStringLiteral("always-on") && m_wantsListening;
    stopListening();
    stopSpeaking();
    setModelTestState(true, m_ttsTestStatus);
    if (m_ttsProvider == QStringLiteral("system") && !ttsReady()) {
        finishTtsTest(false, QStringLiteral("Speech output validation failed"), QStringLiteral("No operating system speech engine is available. Choose the managed local voice."));
        return;
    }
    if (m_ttsProvider == QStringLiteral("local") && !ttsReady()) {
        finishTtsTest(false, QStringLiteral("Speech output validation failed"), QStringLiteral("VOICE_TEST_MODEL_MISSING: Download the selected local voice before testing it."));
        return;
    }
    if (m_ttsProvider == QStringLiteral("remote") && !ttsReady()) {
        finishTtsTest(false, QStringLiteral("Speech output validation failed"), QStringLiteral("VOICE_REMOTE_TTS_UNAVAILABLE: %1").arg(m_remoteTts->status()));
        return;
    }
    if (!m_ttsTestBusy) return;
    continuePendingOperations();
}

void VoiceController::continuePendingOperations() {
    if (m_pendingSttTest && sttModelReady() && m_fixtureDownload->ready()) beginSttTest();
    if (m_pendingTtsTest && ttsReady()) beginTtsTest();
    if (m_enabled && !m_worker && !m_sttTestBusy && sttModelReady()) beginWorker();
    if (m_speaking && !m_ttsIssued && ttsReady()) beginSpeech();
}

void VoiceController::beginSttTest() {
    if (!m_sttTestBusy || !m_pendingSttTest) return;
    m_pendingSttTest = false;
    QFile fixture(QDir(m_fixtureDownload->directory()).filePath(QStringLiteral("test.wav")));
    if (!fixture.open(QIODevice::ReadOnly) || fixture.size() != 265914) {
        finishSttTest(false, QStringLiteral("Speech recognition validation failed"), QStringLiteral("The spoken validation sample is unavailable. Download it again."));
        return;
    }
    const QByteArray wav = fixture.readAll();
    const quint64 generation = ++m_testGeneration;
    auto *const worker = new VoiceWorker(m_modelPath);
    auto *const thread = new QThread;
    worker->setProvider(m_sttProvider);
    worker->setSttModel(m_sttModel);
    worker->setRuntimePath(QDir(m_sttDownload->directory()).filePath(QStringLiteral("runtime")));
    worker->setApiKey(m_secret->key());
    worker->setRecognitionVocabulary(m_recognitionVocabulary);
    worker->setWhisperAcceleration(m_sttAcceleration);
    worker->moveToThread(thread);
    m_testWorker = worker;
    m_testThread = thread;
    m_threads.append(thread);
    connect(thread, &QThread::started, worker, &VoiceWorker::initializeForTest);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(worker, &VoiceWorker::ready, this, [this, worker, wav, generation] {
        if (generation != m_testGeneration || !m_sttTestBusy) return;
        m_sttTestStatus = QStringLiteral("Recognizing a known spoken phrase");
        setModelTestState(true, m_sttTestStatus);
        QMetaObject::invokeMethod(worker, [worker, wav] { worker->testAudio(wav); }, Qt::QueuedConnection);
    });
    connect(worker, &VoiceWorker::testFinished, this, [this, generation](const QString &transcript, const bool passed, const QString &error) {
        if (generation != m_testGeneration || !m_sttTestBusy) return;
        m_sttTestReport.insert(QStringLiteral("output"), transcript);
        if (!transcript.isEmpty()) {
            const bool numericEquivalent = m_sttProfile == QStringLiteral("whisper-turbo");
            int substitutions = 0, deletions = 0, insertions = 0;
            double wordErrorRate = 0.0;
            const QString expectedForScore = numericEquivalent ? numericValidationText(m_sttTestReport.value(QStringLiteral("expected")).toString())
                : m_sttTestReport.value(QStringLiteral("expected")).toString();
            const QString actualForScore = numericEquivalent ? numericValidationText(transcript) : transcript;
            validationScore(expectedForScore, actualForScore,
                &substitutions, &deletions, &insertions, &wordErrorRate);
            m_sttTestReport.insert(QStringLiteral("substitutions"), substitutions);
            m_sttTestReport.insert(QStringLiteral("deletions"), deletions);
            m_sttTestReport.insert(QStringLiteral("insertions"), insertions);
            m_sttTestReport.insert(QStringLiteral("wordErrorRate"), wordErrorRate);
            m_sttTestReport.insert(QStringLiteral("scoringMode"), numericEquivalent ? QStringLiteral("numeric-equivalent") : QStringLiteral("literal-words"));
            if (numericEquivalent) {
                m_sttTestReport.insert(QStringLiteral("normalizedExpected"), expectedForScore);
                m_sttTestReport.insert(QStringLiteral("normalizedOutput"), actualForScore);
            }
        }
        const QString mismatch = transcript.isEmpty() ? (error.isEmpty() ? QStringLiteral("The recognizer returned no transcript.") : error) : QStringLiteral("The recognizer completed, but its transcript differs from the expected phrase.")
            + (error.isEmpty() ? QString() : QStringLiteral(" %1").arg(error));
        finishSttTest(passed, passed ? QStringLiteral("Passed: known spoken digits matched") : QStringLiteral("Recognition completed; transcript differs"),
            passed ? QString() : mismatch + (transcript.isEmpty() ? QString() : QStringLiteral(" Heard: %1").arg(transcript)));
    });
    connect(worker, &VoiceWorker::errorOccurred, this, [this, generation](const QString &code, const QString &message, const bool) {
        if (generation == m_testGeneration && m_sttTestBusy) finishSttTest(false, QStringLiteral("Speech recognition validation failed"), code + QStringLiteral(": ") + message);
    });
    thread->start();
    QTimer::singleShot(45000, this, [this, generation] {
        if (generation == m_testGeneration && m_sttTestBusy) finishSttTest(false, QStringLiteral("Speech recognition validation timed out"), QStringLiteral("Check the selected model or network connection, then try again."));
    });
}

void VoiceController::finishSttTest(const bool passed, const QString &message, const QString &error) {
    ++m_testGeneration;
    m_sttTestBusy = false;
    m_pendingSttTest = false;
    m_sttTestStatus = message;
    m_sttTestError = error;
    m_sttTestReport.insert(QStringLiteral("outcome"), message.startsWith(QStringLiteral("Validation canceled")) ? QStringLiteral("canceled") : passed ? QStringLiteral("passed") : QStringLiteral("failed"));
    if (!error.isEmpty()) m_sttTestReport.insert(QStringLiteral("details"), error);
    const QPointer<VoiceWorker> worker = m_testWorker;
    const QPointer<QThread> thread = m_testThread;
    m_testWorker = nullptr; m_testThread = nullptr;
    if (thread) thread->requestInterruption();
    if (worker && thread) QMetaObject::invokeMethod(worker, [worker, thread] { if (worker) worker->shutdown(); if (thread) thread->quit(); }, Qt::QueuedConnection);
    setModelTestState(m_ttsTestBusy, message, error);
    if (m_enabled) { m_wantsListening = m_testSavedWantsListening; setMicrophoneArmed(m_wantsListening); updateCapture(); continuePendingOperations(); }
}

void VoiceController::beginTtsTest() {
    if (!m_ttsTestBusy || !m_pendingTtsTest) return;
    m_pendingTtsTest = false;
    m_ttsTestStatus = QStringLiteral("Synthesizing and playing a spoken sample");
    setModelTestState(true, m_ttsTestStatus);
    m_pendingSpeech = QStringLiteral("BetterFlash speech test. Your next card is ready.");
    m_ttsStarted = false;
    m_ttsIssued = false;
    m_captureSuspended = !m_worker || !m_ready;
    setSpeakingState(true);
    const quint64 generation = ++m_testGeneration;
    updateCapture();
    if (m_captureSuspended) beginSpeech();
    QTimer::singleShot(45000, this, [this, generation] {
        if (m_ttsTestBusy && generation == m_testGeneration) {
            stopSpeaking();
            finishTtsTest(false, QStringLiteral("Speech output validation timed out"), QStringLiteral("The voice did not finish synthesis and playback. Check the audio output and selected voice."));
        }
    });
}

void VoiceController::finishTtsTest(const bool passed, const QString &message, const QString &error) {
    ++m_testGeneration;
    m_ttsTestBusy = false;
    m_pendingTtsTest = false;
    m_ttsTestStatus = message;
    m_ttsTestError = error;
    m_ttsTestReport.insert(QStringLiteral("outcome"), message.startsWith(QStringLiteral("Validation canceled")) ? QStringLiteral("canceled") : passed ? QStringLiteral("passed") : QStringLiteral("failed"));
    if (!error.isEmpty()) m_ttsTestReport.insert(QStringLiteral("details"), error);
    setModelTestState(m_sttTestBusy, message, error);
    if (m_enabled) { m_wantsListening = m_testSavedWantsListening; setMicrophoneArmed(m_wantsListening); updateCapture(); }
}

void VoiceController::resetSttTestReport() {
    m_sttTestReport.clear();
    m_sttTestStatus.clear();
    m_sttTestError.clear();
    emit modelTestChanged();
}

void VoiceController::resetTtsTestReport() {
    m_ttsTestReport.clear();
    m_ttsTestStatus.clear();
    m_ttsTestError.clear();
    emit modelTestChanged();
}

void VoiceController::setApiKey(const QString &key) {
    if (key.trimmed().isEmpty()) m_secret->clearKey();
    else m_secret->saveKey(key);
}
void VoiceController::clearApiKey() { m_secret->clearKey(); }

void VoiceController::setMicrophoneArmed(const bool armed) {
    if (m_microphoneArmed == armed) return;
    m_microphoneArmed = armed;
    emit microphoneArmedChanged();
}

void VoiceController::setReviewMode(const QString &mode) {
    const QString value = mode.trimmed().toLower();
    if (value != QStringLiteral("trigger") && value != QStringLiteral("always-on")) {
        setError(QStringLiteral("VOICE_REVIEW_MODE_INVALID"), QStringLiteral("Choose keyboard trigger or always-on microphone mode."));
        return;
    }
    if (m_reviewMode == value) return;
    cancelTranscriptCorrection();
    ++m_captureEpoch;
    m_testSavedWantsListening = false;
    m_wantsListening = m_enabled && value == QStringLiteral("always-on");
    setMicrophoneArmed(m_wantsListening);
    m_reviewMode = value;
    persistConfiguration();
    emit reviewModeChanged();
    if (m_enabled && !m_modelTestBusy) {
        m_workerRestartPending = true;
        retireWorker();
        if (!m_retiringWorkerThread) m_workerRestartPending = false;
    }
    updateCapture();
    if (m_enabled) {
        setStatus(QStringLiteral("Ready"));
        if (!m_workerRestartPending) continuePendingOperations();
    }
}

void VoiceController::setEnabled(const bool enabled) {
    if (!enabled) cancelTranscriptCorrection();
    if (m_enabled == enabled) return;
    if (enabled && m_sttProvider == QStringLiteral("groq") && !m_secret->hasKey()) {
        setError(QStringLiteral("GROQ_KEY_MISSING"), QStringLiteral("Add a Groq API key in Voice settings before enabling speech recognition."));
        emit enabledChanged();
        return;
    }
    m_enabled = enabled;
    emit enabledChanged();
    if (!enabled) {
        m_wantsListening = false;
        setMicrophoneArmed(false);
        stopSpeaking();
        retireWorker();
        retireTtsWorker();
        if (m_ttsTestBusy) finishTtsTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech output was stopped."));
        setStatus(QStringLiteral("Voice disabled"));
        return;
    }
    m_wantsListening = m_reviewMode == QStringLiteral("always-on");
    setMicrophoneArmed(m_wantsListening);
    if (m_retiringWorkerThread) m_workerRestartPending = true;
    if (!m_lastError.isEmpty()) { m_lastError.clear(); emit lastErrorChanged(); }
#ifdef Q_OS_ANDROID
    const QMicrophonePermission permission;
    auto *const application = QCoreApplication::instance();
    const Qt::PermissionStatus status = application->checkPermission(permission);
    if (status != Qt::PermissionStatus::Granted) {
        const quint64 generation = ++m_workerGeneration;
        setStatus(QStringLiteral("Waiting for microphone permission"));
        application->requestPermission(permission, this, [this, generation](const QPermission &result) {
            if (!m_enabled || generation != m_workerGeneration) return;
            if (result.status() != Qt::PermissionStatus::Granted) {
                setEnabled(false);
                setError(QStringLiteral("VOICE_MICROPHONE_PERMISSION_DENIED"), QStringLiteral("Allow microphone access in Android settings, then enable voice again."));
                return;
            }
            beginWorker();
        });
        return;
    }
#endif
    beginWorker();
}

void VoiceController::beginWorker() {
    if (!m_enabled || m_worker || m_modelTestBusy || m_retiringWorkerThread) return;
    if (m_sttProvider == QStringLiteral("local") && !sttModelReady()) {
        setStatus(QStringLiteral("Preparing local speech recognition"));
        downloadSttModel();
        return;
    }
    const quint64 generation = ++m_workerGeneration;
    auto *const thread = new QThread;
    auto *const worker = new VoiceWorker(m_modelPath);
    worker->setProvider(m_sttProvider);
    worker->setSttModel(m_sttModel);
    worker->setRuntimePath(QDir(m_sttDownload->directory()).filePath(QStringLiteral("runtime")));
    worker->setRecognitionVocabulary(m_recognitionVocabulary);
    worker->setWhisperAcceleration(m_sttAcceleration);
    worker->setApiKey(m_secret->key());
    worker->setOneShot(m_reviewMode == QStringLiteral("trigger"));
    worker->moveToThread(thread);
    m_worker = worker;
    m_workerThread = thread;
    m_threads.removeIf([](const QPointer<QThread> &candidate) { return candidate.isNull(); });
    m_threads.append(thread);
    connect(thread, &QThread::started, worker, &VoiceWorker::initialize);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(worker, &VoiceWorker::ready, this, [this, generation] {
        if (!m_enabled || generation != m_workerGeneration) return;
        setStatus(QStringLiteral("Ready"));
        setReady(true);
        updateCapture();
    });
    connect(worker, &VoiceWorker::listeningChanged, this, [this, generation](const bool listening, const quint64 epoch) {
        if (!m_enabled || generation != m_workerGeneration || epoch != m_captureEpoch) return;
        m_captureSuspended = !listening;
        setListeningState(listening);
        if (!listening && m_speaking && !m_ttsIssued) beginSpeech();
        else if (listening && !m_speaking) setStatus(QStringLiteral("Listening"));
        else if (!m_speaking && !m_processing) setStatus(QStringLiteral("Ready"));
    });
    connect(worker, &VoiceWorker::processingChanged, this, [this, generation](const bool processing, const quint64 epoch) {
        if (!m_enabled || generation != m_workerGeneration || epoch != m_captureEpoch) return;
        const bool wasProcessing = m_processing;
        m_processing = processing;
        if (!processing && wasProcessing && m_reviewMode == QStringLiteral("trigger")) {
            m_wantsListening = false;
            setMicrophoneArmed(false);
        }
        if (!m_speaking) setStatus(processing ? QStringLiteral("Transcribing") : QStringLiteral("Ready"));
    });
    connect(worker, &VoiceWorker::transcriptReady, this, [this, generation](const QString &text, const quint64 epoch) {
        if (m_enabled && generation == m_workerGeneration && epoch == m_captureEpoch
            && m_wantsListening && !m_speaking && !m_echoTail) {
            if (m_reviewMode == QStringLiteral("trigger")) {
                m_wantsListening = false;
                setMicrophoneArmed(false);
            }
            processTranscript(text);
        }
    });
    connect(worker, &VoiceWorker::oneShotFinished, this, [this, generation](const quint64 epoch) {
        if (!m_enabled || generation != m_workerGeneration || epoch != m_captureEpoch || m_reviewMode != QStringLiteral("trigger")) return;
        m_wantsListening = false;
        setMicrophoneArmed(false);
        if (!m_processing && !m_speaking) setStatus(QStringLiteral("Ready"));
    });
    connect(worker, &VoiceWorker::errorOccurred, this, [this, generation](const QString &code, const QString &message, const bool fatal) {
        if (!m_enabled || generation != m_workerGeneration) return;
        if (fatal) setEnabled(false);
        setError(code, message);
    });
    connect(worker, &VoiceWorker::captureError, this, [this, generation](const QString &code, const QString &message,
        const bool fatal, const quint64 epoch) {
        if (!m_enabled || generation != m_workerGeneration || epoch != m_captureEpoch) return;
        if (m_reviewMode == QStringLiteral("trigger")) {
            m_wantsListening = false;
            setMicrophoneArmed(false);
        }
        if (fatal) setEnabled(false);
        setError(code, message);
    });
    setStatus(m_sttProvider == QStringLiteral("groq") ? QStringLiteral("Preparing microphone") : QStringLiteral("Loading local speech recognition"));
    thread->start();
}

void VoiceController::retireWorker() {
    cancelTranscriptCorrection();
    ++m_workerGeneration;
    ++m_captureEpoch;
    const QPointer<VoiceWorker> worker = m_worker;
    const QPointer<QThread> thread = m_workerThread;
    m_worker = nullptr;
    m_workerThread = nullptr;
    if (thread) {
        m_retiringWorkerThread = thread;
        connect(thread, &QThread::finished, this, [this, thread] {
            if (m_retiringWorkerThread == thread) m_retiringWorkerThread = nullptr;
            if (m_workerRestartPending && m_enabled) {
                m_workerRestartPending = false;
                beginWorker();
            }
        });
    }
    m_processing = false;
    m_captureSuspended = true;
    setReady(false);
    setListeningState(false);
    if (thread) thread->requestInterruption();
    if (worker && thread) {
        QMetaObject::invokeMethod(worker, [worker, thread] {
            if (worker) worker->shutdown();
            if (thread) thread->quit();
        }, Qt::QueuedConnection);
    }
}

void VoiceController::restartWorker() {
    if (!m_enabled) return;
    const bool wanted = m_reviewMode == QStringLiteral("always-on") && m_wantsListening;
    m_workerRestartPending = true;
    setEnabled(false);
    setEnabled(true);
    m_wantsListening = wanted;
    setMicrophoneArmed(wanted);
    if (!m_retiringWorkerThread) {
        m_workerRestartPending = false;
        continuePendingOperations();
    }
}

void VoiceController::updateCapture() {
    if (!m_worker || !m_ready) return;
    const bool shouldListen = m_enabled && m_wantsListening && !m_speaking && !m_echoTail;
    const quint64 epoch = m_captureEpoch;
    const QPointer<VoiceWorker> worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, shouldListen, epoch] {
        if (worker) worker->setListening(shouldListen, epoch);
    }, Qt::QueuedConnection);
}

void VoiceController::cancelPendingRecognition() {
    cancelTranscriptCorrection();
    if (m_reviewMode == QStringLiteral("trigger")) {
        m_wantsListening = false;
        setMicrophoneArmed(false);
    }
    ++m_captureEpoch;
    m_processing = false;
    setListeningState(false);
    updateCapture();
}

void VoiceController::startListening() {
    if (!m_enabled) return;
    if (m_reviewMode == QStringLiteral("trigger")) return;
    m_wantsListening = true;
    setMicrophoneArmed(true);
    updateCapture();
}

void VoiceController::stopListening() {
    cancelTranscriptCorrection();
    m_wantsListening = false;
    setMicrophoneArmed(false);
    ++m_captureEpoch;
    m_processing = false;
    setListeningState(false);
    updateCapture();
    if (!m_speaking && m_enabled) setStatus(QStringLiteral("Ready"));
}

void VoiceController::triggerMicrophone() {
    if (!m_enabled) {
        setEnabled(true);
        if (!m_enabled) return;
        if (m_reviewMode == QStringLiteral("always-on")) return;
    }
    if (m_reviewMode != QStringLiteral("trigger")) {
        m_wantsListening = !m_wantsListening;
        setMicrophoneArmed(m_wantsListening);
        updateCapture();
        return;
    }
    cancelTranscriptCorrection();
    ++m_captureEpoch;
    m_processing = false;
    if (m_speaking) stopSpeaking();
    if (m_wantsListening) {
        m_wantsListening = false;
        setMicrophoneArmed(false);
        setListeningState(false);
        updateCapture();
        if (!m_speaking) setStatus(QStringLiteral("Ready"));
        return;
    }
    m_wantsListening = true;
    setMicrophoneArmed(true);
    updateCapture();
}

void VoiceController::processTranscript(const QString &transcript) {
    if (m_modelTestBusy) return;
    const QString text = transcript.trimmed();
    if (text.isEmpty()) return;
    if (text.size() > 16384) {
        setError(QStringLiteral("VOICE_TRANSCRIPT_TOO_LONG"), QStringLiteral("That transcript is too long. Answer in a shorter utterance."));
        return;
    }
    cancelTranscriptCorrection();
    m_lastRawTranscript = text;
    emit lastRawTranscriptChanged();
    m_lastTranscript = text;
    emit lastTranscriptChanged();
    const struct VoiceWorker::ParsedUtterance parsed = VoiceWorker::parseUtterance(text);
    if (parsed.isCommand) emit commandRecognized(parsed.command, parsed.argument);
    else if (m_corrector->enabled() && !m_corrector->ready()) {
        setError(QStringLiteral("TRANSCRIPT_CORRECTION_NOT_READY"), QStringLiteral("Transcript correction is enabled but unavailable. Configure the selected language model in Language model settings, then try again."));
        emit answerRecognized(text);
    } else if (m_corrector->enabled() && m_corrector->ready()) {
        cancelTranscriptCorrection();
        ++m_captureEpoch;
        m_correctionEpoch = m_captureEpoch;
        m_correctionPending = true;
        m_processing = true;
        updateCapture();
        const quint64 generation = m_correctionGeneration;
        m_correctionJob = m_corrector->correct(text, this, [this, generation, text](std::expected<QString, struct ProviderError> result) {
            if (!m_correctionPending || generation != m_correctionGeneration) return;
            m_correctionPending = false;
            m_correctionJob = 0;
            m_processing = false;
            if (result) {
                m_lastTranscript = *result;
                emit lastTranscriptChanged();
                emit answerRecognized(*result);
            } else if (result.error().code != QStringLiteral("TRANSCRIPT_CORRECTION_CANCELED")) {
                setError(result.error().code, result.error().message);
                emit answerRecognized(text);
            }
            updateCapture();
        });
    } else emit answerRecognized(text);
}

void VoiceController::speak(const QString &text) {
    if (!m_enabled || m_modelTestBusy || text.trimmed().isEmpty()) return;
    if (m_reviewMode == QStringLiteral("trigger")) {
        m_wantsListening = false;
        setMicrophoneArmed(false);
    }
    cancelTranscriptCorrection();
    stopSpeaking();
    m_echoTail = false;
    m_pendingSpeech = text.trimmed();
    m_ttsStarted = false;
    m_ttsIssued = false;
    setSpeakingState(true);
    setStatus(QStringLiteral("Speaking"));
    ++m_captureEpoch;
    m_processing = false;
    setListeningState(false);
    m_captureSuspended = !m_worker || !m_ready;
    updateCapture();
    if (m_captureSuspended) beginSpeech();
    if (m_ttsProvider == QStringLiteral("local") || m_ttsProvider == QStringLiteral("remote")) return;
    const quint64 generation = m_speechGeneration;
    QTimer::singleShot(5000, this, [this, generation] {
        if (generation == m_speechGeneration && m_speaking && !m_ttsStarted)
            speechError(QStringLiteral("The system speech engine did not initialize."));
    });
}

void VoiceController::beginSpeech() {
    if ((!m_enabled && !m_ttsTestBusy) || !m_speaking || !m_captureSuspended || m_ttsIssued || m_pendingSpeech.isEmpty()) return;
    if (m_ttsProvider == QStringLiteral("local")) { beginLocalSpeech(); return; }
    if (m_ttsProvider == QStringLiteral("remote")) {
        if (!m_remoteTts->ready()) { speechError(m_remoteTts->status()); return; }
        m_ttsIssued = true;
        const quint64 request = ++m_speechGeneration;
        setStatus(QStringLiteral("Preparing remote speech"));
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.preferredFormat().isValid()) {
            speechError(QStringLiteral("Connect an audio output and select it in system sound settings.")); return;
        }
        m_remoteTts->synthesize(m_pendingSpeech, device.preferredFormat(), request);
        return;
    }
    if (!m_speech) {
        m_speech = new QTextToSpeech(this);
        connect(m_speech, &QTextToSpeech::stateChanged, this, [this](const auto) { onSpeechStateChanged(); });
        connect(m_speech, &QTextToSpeech::errorOccurred, this, [this](const auto, const QString &message) {
            if (!m_stoppingSpeech) speechError(message);
        });
    }
    if (m_speech->state() == QTextToSpeech::Error || QTextToSpeech::availableEngines().isEmpty()) {
        speechError(m_speech->errorString());
        return;
    }
    if (m_speech->state() != QTextToSpeech::Ready) return;
    m_ttsIssued = true;
    const quint64 generation = ++m_speechGeneration;
    m_speech->say(m_pendingSpeech);
    QTimer::singleShot(3000, this, [this, generation] {
        if (generation == m_speechGeneration && m_speaking && !m_ttsStarted)
            speechError(QStringLiteral("The system speech engine did not start playback."));
    });
}

void VoiceController::beginLocalSpeech() {
    if (m_retiringTtsThread && m_retiringTtsThread->isRunning()) {
        setStatus(QStringLiteral("Finishing the previous speech request"));
        return;
    }
    if (!ttsReady()) {
        setStatus(QStringLiteral("Preparing the local reading voice"));
        downloadTtsModel();
        return;
    }
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) { speechError(QStringLiteral("Connect an audio output and select it in system sound settings.")); return; }
    m_playbackFormat = device.preferredFormat();
    if (!m_playbackFormat.isValid() || !device.isFormatSupported(m_playbackFormat)) {
        speechError(QStringLiteral("The default audio output has no supported PCM format. Select another output in system sound settings."));
        return;
    }
    if (m_pendingSpeech.size() > 8192) {
        speechError(QStringLiteral("This prompt exceeds the local voice limit of 8192 characters. Shorten the card or read it in parts."));
        return;
    }
    if (!m_ttsWorker) {
        auto *const thread = new QThread;
        const QString modelFile = m_ttsProfile == QStringLiteral("amy-low") ? QStringLiteral("en_US-amy-low.onnx") : QStringLiteral("en_US-ljspeech-medium.onnx");
        auto *const worker = new LocalTtsWorker(QDir(m_ttsDownload->directory()).filePath(QStringLiteral("runtime")),
            QDir(m_ttsDownload->directory()).filePath(QStringLiteral("models/") + m_ttsProfile), modelFile);
        worker->moveToThread(thread);
        m_ttsWorker = worker; m_ttsThread = thread;
        m_threads.append(thread);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        connect(worker, &LocalTtsWorker::pcmReady, this, &VoiceController::startLocalPlayback);
        connect(worker, &LocalTtsWorker::failed, this, [this](const QString &message, const quint64 request) {
            if (request == m_speechGeneration && m_speaking) speechError(message);
        });
        thread->start();
    }
    m_ttsIssued = true;
    const quint64 request = ++m_speechGeneration;
    m_synthesisCancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_synthesisCancelled;
    const QPointer<LocalTtsWorker> worker = m_ttsWorker;
    const QString text = m_pendingSpeech;
    const QAudioFormat format = m_playbackFormat;
    setStatus(QStringLiteral("Preparing speech"));
    QMetaObject::invokeMethod(worker, [worker, text, format, request, cancelled] {
        if (worker) worker->synthesize(text, format, request, cancelled);
    }, Qt::QueuedConnection);
    QTimer::singleShot(45000, this, [this, request] {
        if (request == m_speechGeneration && m_speaking && !m_ttsStarted)
            speechError(QStringLiteral("The local voice took too long to synthesize this prompt. Shorten it or choose Amy low."));
    });
}

void VoiceController::startLocalPlayback(const QByteArray &pcm, const quint64 request, const double seconds) {
    if (request != m_speechGeneration || !m_speaking || (!m_enabled && !m_ttsTestBusy)) return;
    if (pcm.isEmpty() || pcm.size() % m_playbackFormat.bytesPerFrame() != 0) {
        speechError(QStringLiteral("The local voice returned malformed audio. Try another voice.")); return;
    }
    if (m_ttsTestBusy) {
        m_ttsTestReport.insert(QStringLiteral("output"), QStringLiteral("Synthesized %1 bytes at %2 Hz for %3 seconds; playback started.")
            .arg(pcm.size()).arg(m_playbackFormat.sampleRate()).arg(seconds, 0, 'f', 2));
        emit modelTestChanged();
    }
    if (m_audioSink) { m_audioSink->disconnect(this); m_audioSink->stop(); m_audioSink->deleteLater(); }
    if (m_playbackBuffer) m_playbackBuffer->deleteLater();
    m_playbackBuffer = new QBuffer(this);
    m_playbackBuffer->setData(pcm);
    if (!m_playbackBuffer->open(QIODevice::ReadOnly)) { speechError(QStringLiteral("The generated speech buffer could not open.")); return; }
    m_audioSink = new QAudioSink(QMediaDevices::defaultAudioOutput(), m_playbackFormat, this);
    const QPointer<QAudioSink> sink = m_audioSink;
    connect(m_audioSink, &QAudioSink::stateChanged, this, [this, request, sink](const QAudio::State state) {
        if (request != m_speechGeneration || !m_speaking || !sink || m_audioSink != sink) return;
        if (sink->error() != QAudio::NoError && !(state == QAudio::IdleState && sink->error() == QAudio::UnderrunError)) {
            speechError(QStringLiteral("Audio playback failed. Check the selected sound output and try again.")); return;
        }
        switch (state) {
        case QAudio::ActiveState:
            m_ttsStarted = true;
            setStatus(QStringLiteral("Speaking"));
            break;
        case QAudio::IdleState:
            if (m_ttsStarted && m_playbackBuffer && m_playbackBuffer->atEnd()) {
                sink->disconnect(this); sink->stop(); sink->deleteLater(); m_audioSink = nullptr;
                m_playbackBuffer->deleteLater(); m_playbackBuffer = nullptr;
                finishSpeech();
            }
            break;
        case QAudio::StoppedState:
            speechError(QStringLiteral("Audio playback stopped before the sample finished. Check the sound output."));
            break;
        case QAudio::SuspendedState:
            break;
        }
    });
    m_audioSink->start(m_playbackBuffer);
    QTimer::singleShot(static_cast<int>(seconds * 1000.0) + 10000, this, [this, request] {
        if (request == m_speechGeneration && m_speaking)
            speechError(QStringLiteral("Audio playback did not finish. Check the sound output and retry."));
    });
}

void VoiceController::retireTtsWorker() {
    m_synthesisCancelled->store(true);
    const QPointer<LocalTtsWorker> worker = m_ttsWorker;
    const QPointer<QThread> thread = m_ttsThread;
    m_ttsWorker = nullptr; m_ttsThread = nullptr;
    if (thread) {
        m_retiringTtsThread = thread;
        connect(thread, &QThread::finished, this, [this, thread] {
            if (m_retiringTtsThread == thread) m_retiringTtsThread = nullptr;
            continuePendingOperations();
        });
    }
    if (thread) thread->requestInterruption();
    if (worker && thread) QMetaObject::invokeMethod(worker, [worker, thread] {
        if (worker) worker->shutdown();
        if (thread) thread->quit();
    }, Qt::QueuedConnection);
}

void VoiceController::onSpeechStateChanged() {
    if (!m_speech || m_stoppingSpeech) return;
    switch (m_speech->state()) {
    case QTextToSpeech::Speaking:
    case QTextToSpeech::Synthesizing:
        if (m_speaking && m_ttsIssued) m_ttsStarted = true;
        break;
    case QTextToSpeech::Ready:
        if (m_speaking && m_ttsStarted) finishSpeech();
        else if (m_speaking && !m_ttsIssued) beginSpeech();
        break;
    case QTextToSpeech::Error:
        speechError(m_speech->errorString());
        break;
    case QTextToSpeech::Paused:
        break;
    }
}

void VoiceController::finishSpeech() {
    if (!m_speaking) return;
    if (m_reviewMode == QStringLiteral("trigger")) {
        m_wantsListening = false;
        setMicrophoneArmed(false);
    }
    m_remoteTts->cancel();
    m_pendingSpeech.clear();
    m_ttsStarted = false;
    m_ttsIssued = false;
    setSpeakingState(false);
    m_echoTail = true;
    if (m_ttsTestBusy) {
        if (m_ttsProvider == QStringLiteral("system")) {
            m_ttsTestReport.insert(QStringLiteral("output"), QStringLiteral("System speech engine playback completed."));
        } else {
            QString output = m_ttsTestReport.value(QStringLiteral("output")).toString();
            if (!output.isEmpty()) m_ttsTestReport.insert(QStringLiteral("output"), output.replace(QStringLiteral("playback started."), QStringLiteral("playback completed.")));
        }
        finishTtsTest(true, QStringLiteral("Passed: synthesized audio played successfully"));
    }
    const quint64 generation = ++m_speechGeneration;
    QTimer::singleShot(350, this, [this, generation] {
        if (generation != m_speechGeneration) return;
        m_echoTail = false;
        updateCapture();
        if (m_enabled && !m_wantsListening) setStatus(QStringLiteral("Ready"));
    });
    emit utteranceFinished();
}

void VoiceController::stopSpeaking() {
    if (m_reviewMode == QStringLiteral("trigger")) {
        m_wantsListening = false;
        setMicrophoneArmed(false);
    }
    ++m_speechGeneration;
    m_remoteTts->cancel();
    m_synthesisCancelled->store(true);
    if (m_audioSink) { m_audioSink->disconnect(this); m_audioSink->stop(); m_audioSink->deleteLater(); m_audioSink = nullptr; }
    if (m_playbackBuffer) { m_playbackBuffer->deleteLater(); m_playbackBuffer = nullptr; }
    m_stoppingSpeech = true;
    if (m_speech) m_speech->stop(QTextToSpeech::BoundaryHint::Immediate);
    m_stoppingSpeech = false;
    m_pendingSpeech.clear();
    m_ttsStarted = false;
    m_ttsIssued = false;
    m_echoTail = false;
    const bool wasSpeaking = m_speaking;
    setSpeakingState(false);
    if (wasSpeaking && m_enabled) {
        m_echoTail = true;
        const quint64 generation = m_speechGeneration;
        QTimer::singleShot(350, this, [this, generation] {
            if (generation != m_speechGeneration) return;
            m_echoTail = false;
            updateCapture();
        });
    }
}

void VoiceController::speechError(const QString &message) {
    stopSpeaking();
    if (m_ttsTestBusy) finishTtsTest(false, QStringLiteral("Speech output validation failed"), message);
    const QString detail = message.trimmed();
    const bool remoteFailure = m_ttsProvider == QStringLiteral("remote")
        && (detail.startsWith(QStringLiteral("Remote speech service"))
            || detail.startsWith(QStringLiteral("Remote speech output")));
    setError(QStringLiteral("VOICE_TTS_UNAVAILABLE"), detail.isEmpty()
        ? QStringLiteral("Speech output is unavailable. Choose a local voice in Settings > Voice and check the audio output.")
        : remoteFailure ? detail
        : QStringLiteral("Speech output failed: %1 Check the selected voice and audio output.").arg(detail));
}

void VoiceController::setReady(const bool value) {
    if (m_ready == value) return;
    m_ready = value;
    emit readyChanged();
}
void VoiceController::setListeningState(const bool value) {
    if (m_listening == value) return;
    m_listening = value;
    emit listeningChanged();
}
void VoiceController::setSpeakingState(const bool value) {
    if (m_speaking == value) return;
    m_speaking = value;
    emit speakingChanged();
}
void VoiceController::setStatus(const QString &value) {
    if (m_status == value) return;
    m_status = value;
    emit statusChanged();
}
void VoiceController::setError(const QString &code, const QString &message) {
    m_lastError = code + QStringLiteral(": ") + message;
    emit lastErrorChanged();
    setStatus(code + QStringLiteral(": ") + message);
    emit errorOccurred(code, message);
}
