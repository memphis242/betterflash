#include "VoiceController.h"

#include "VoiceWorker.h"
#include "CustomSttTests.h"
#include "LocalTtsWorker.h"
#include "integrations/SecretStore.h"
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
QStringList validationWords(const QString &text) {
    return text.trimmed().toCaseFolded().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
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

VoiceController::VoiceController(QObject *parent)
    : QObject(parent), m_secret(new SecretStore(QStringLiteral("groq"), QStringLiteral("GROQ_API_KEY"), this)),
      m_speech(nullptr), m_customSttTests(new CustomSttTests(this)) {
    m_localDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("models/voice"));
    m_sttDownload = new ModelDownload(QDir(m_localDirectory).filePath(QStringLiteral("stt")), this);
    m_ttsDownload = new ModelDownload(QDir(m_localDirectory).filePath(QStringLiteral("tts")), this);
    m_fixtureDownload = new ModelDownload(QDir(m_localDirectory).filePath(QStringLiteral("validation")), this);
#ifdef Q_OS_ANDROID
    m_ttsProfile = QStringLiteral("amy-low");
#endif
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice"));
    m_modelPath = settings.value(QStringLiteral("modelPath")).toString();
    if (m_modelPath.isEmpty()) m_modelPath = QDir(m_sttDownload->directory()).filePath(QStringLiteral("model"));
    const QString provider = settings.value(QStringLiteral("provider"), QStringLiteral("local")).toString().trimmed().toLower();
    if (provider == QStringLiteral("local") || provider == QStringLiteral("groq")) m_sttProvider = provider;
    const QString model = settings.value(QStringLiteral("sttModel"), m_sttModel).toString().trimmed();
    static const QRegularExpression validModel(QStringLiteral(R"(^[A-Za-z0-9._:-]{1,128}$)"));
    if (validModel.match(model).hasMatch()) m_sttModel = model;
    const QString profile = settings.value(QStringLiteral("sttProfile"), m_sttProfile).toString().trimmed();
    if (profile == QStringLiteral("small-en")) m_sttProfile = profile;
    const QString ttsProvider = settings.value(QStringLiteral("ttsProvider"), m_ttsProvider).toString().trimmed().toLower();
    if (ttsProvider == QStringLiteral("local") || ttsProvider == QStringLiteral("system")) m_ttsProvider = ttsProvider;
    const QString ttsProfile = settings.value(QStringLiteral("ttsProfile"), m_ttsProfile).toString().trimmed();
    if (ttsProfile == QStringLiteral("amy-low") || ttsProfile == QStringLiteral("ljspeech-medium")) m_ttsProfile = ttsProfile;
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
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key());
    connect(m_customSttTests, &CustomSttTests::stateChanged, this, [this] {
        const bool active = m_customSttTests->busy() || m_customSttTests->recording();
        if (active && !m_customVoiceSuspended) {
            m_customVoiceSuspended = true;
            m_testSavedWantsListening = m_wantsListening;
            stopListening();
            stopSpeaking();
        }
        if (active) setModelTestState(true, m_customSttTests->status(), m_customSttTests->error());
        else if (m_customVoiceSuspended) {
            m_customVoiceSuspended = false;
            setModelTestState(m_sttTestBusy || m_ttsTestBusy, m_customSttTests->status(), m_customSttTests->error());
            if (m_enabled) { m_wantsListening = m_testSavedWantsListening; updateCapture(); }
        }
    });
    connect(m_secret, &SecretStore::changed, this, [this] {
        emit hasApiKeyChanged();
        emit keyStorageStatusChanged();
        const QString key = m_secret->key();
        if (key != m_lastKey) {
            m_lastKey = key;
            m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, key);
            if (m_sttProvider == QStringLiteral("groq")) {
                if (m_sttTestBusy)
                    finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech provider key changed."));
                resetSttTestReport();
            }
            if (m_enabled && m_sttProvider == QStringLiteral("groq")) restartWorker();
        }
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
QString VoiceController::keyStorageStatus() const { return m_secret->status(); }
QObject *VoiceController::customSttTests() const { return m_customSttTests; }

void VoiceController::persistConfiguration() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice"));
    settings.setValue(QStringLiteral("modelPath"), m_modelPath);
    settings.setValue(QStringLiteral("provider"), m_sttProvider);
    settings.setValue(QStringLiteral("sttModel"), m_sttModel);
    settings.setValue(QStringLiteral("sttProfile"), m_sttProfile);
    settings.setValue(QStringLiteral("ttsProvider"), m_ttsProvider);
    settings.setValue(QStringLiteral("ttsProfile"), m_ttsProfile);
}

void VoiceController::setModelPath(const QString &path) {
    const QString value = path.trimmed();
    if (m_modelPath == value) return;
    if (m_sttTestBusy) finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech model path changed."));
    m_modelPath = value;
    resetSttTestReport();
    persistConfiguration();
    emit modelPathChanged();
    emit modelReadinessChanged();
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key());
    if (m_enabled && m_sttProvider == QStringLiteral("local")) restartWorker();
}

void VoiceController::setSttProvider(const QString &provider) {
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
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key());
    if (m_enabled) restartWorker();
}

void VoiceController::setSttModel(const QString &model) {
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
    m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key());
    if (m_enabled && m_sttProvider == QStringLiteral("groq")) restartWorker();
}

void VoiceController::setSttProfile(const QString &profile) {
    const QString value = profile.trimmed();
    if (value != QStringLiteral("small-en")) {
        setError(QStringLiteral("VOICE_PROFILE_INVALID"), QStringLiteral("Choose the small English recognition profile."));
        return;
    }
    if (m_sttProfile == value) return;
    if (m_sttTestBusy) finishSttTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech profile changed."));
    m_sttProfile = value;
    resetSttTestReport();
    persistConfiguration();
    emit sttProfileChanged();
}

void VoiceController::setTtsProvider(const QString &provider) {
    const QString value = provider.trimmed().toLower();
    if (value != QStringLiteral("local") && value != QStringLiteral("system")) {
        setError(QStringLiteral("VOICE_TTS_PROVIDER_INVALID"), QStringLiteral("Choose the managed local voice or the operating system speech engine."));
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
        m_sttDownload->configure({
            {QUrl(QStringLiteral("https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip")),
                QStringLiteral("vosk-model-small-en-us-0.15.zip"), 41205931,
                QByteArrayLiteral("30f26242c4eb449f948e42cb302dd7a686cb29a3423a8367f99ff41780942498"),
                QStringLiteral("vosk-model-small-en-us-0.15/"), QStringLiteral("model")},
            {QUrl(QStringLiteral("https://files.pythonhosted.org/packages/fc/ca/83398cfcd557360a3d7b2d732aee1c5f6999f68618d1645f38d53e14c9ff/vosk-0.3.45-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.whl")),
                QStringLiteral("vosk-0.3.45-linux-x64.whl"), 7173758,
                QByteArrayLiteral("25e025093c4399d7278f543568ed8cc5460ac3a4bf48c23673ace1e25d26619f"),
                QStringLiteral("vosk/"), QStringLiteral("runtime")},
            {QUrl(QStringLiteral("https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.45/COPYING")),
                QStringLiteral("licenses/Vosk-Apache-2.0.txt"), 10173,
                QByteArrayLiteral("bb618af833c817ab01c1564bd3ec75ba4b4f9cf30910a4a2168e30a674ee8cc6"), {}, {}}});
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

bool VoiceController::sttModelReady() const {
    if (m_sttProvider == QStringLiteral("groq")) return m_secret->hasKey();
    if (m_modelPath == QDir(m_sttDownload->directory()).filePath(QStringLiteral("model")) && !m_sttDownload->ready()) return false;
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
    if (sttModelReady()) return QStringLiteral("Small English recognition is ready for offline use");
#if defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64) && !defined(Q_OS_ANDROID)
    return m_sttDownload->error().isEmpty() ? m_sttDownload->status() : m_sttDownload->error();
#else
    return QStringLiteral("Local recognition for this platform is a draft; use Groq until a native kit is verified");
#endif
}

bool VoiceController::sttModelDownloading() const { return m_sttDownload->downloading() || m_fixtureDownload->downloading(); }
double VoiceController::sttModelProgress() const { return m_sttDownload->downloading() ? m_sttDownload->progress() : m_fixtureDownload->progress(); }

bool VoiceController::ttsReady() const {
    return m_ttsProvider == QStringLiteral("local") ? m_ttsDownload->ready() : !QTextToSpeech::availableEngines().isEmpty();
}

QString VoiceController::ttsStatus() const {
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
    const QString managedPath = QDir(m_sttDownload->directory()).filePath(QStringLiteral("model"));
    if (m_modelPath != managedPath) { m_modelPath = managedPath; persistConfiguration(); m_customSttTests->setConfiguration(m_sttProvider, m_modelPath, m_sttModel, m_secret->key()); emit modelPathChanged(); }
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
        {QStringLiteral("expected"), QStringLiteral("one zero zero zero one nine oh two one oh zero one eight zero three")}};
    m_sttTestStatus = QStringLiteral("Preparing a known spoken phrase");
    m_testSavedWantsListening = m_wantsListening;
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
    m_testSavedWantsListening = m_wantsListening;
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
            int substitutions = 0, deletions = 0, insertions = 0;
            double wordErrorRate = 0.0;
            validationScore(m_sttTestReport.value(QStringLiteral("expected")).toString(), transcript,
                &substitutions, &deletions, &insertions, &wordErrorRate);
            m_sttTestReport.insert(QStringLiteral("substitutions"), substitutions);
            m_sttTestReport.insert(QStringLiteral("deletions"), deletions);
            m_sttTestReport.insert(QStringLiteral("insertions"), insertions);
            m_sttTestReport.insert(QStringLiteral("wordErrorRate"), wordErrorRate);
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
    if (m_enabled) { m_wantsListening = m_testSavedWantsListening; updateCapture(); continuePendingOperations(); }
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
    if (m_enabled) { m_wantsListening = m_testSavedWantsListening; updateCapture(); }
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

void VoiceController::setApiKey(const QString &key) { m_secret->setKey(key); }
void VoiceController::clearApiKey() { m_secret->clear(); }

void VoiceController::setEnabled(const bool enabled) {
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
        stopSpeaking();
        retireWorker();
        retireTtsWorker();
        if (m_ttsTestBusy) finishTtsTest(false, QStringLiteral("Validation canceled"), QStringLiteral("Speech output was stopped."));
        setStatus(QStringLiteral("Voice disabled"));
        return;
    }
    m_wantsListening = true;
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
    if (!m_enabled || m_worker || m_modelTestBusy) return;
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
    worker->setApiKey(m_secret->key());
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
        m_processing = processing;
        if (!m_speaking) setStatus(processing ? QStringLiteral("Transcribing") : QStringLiteral("Ready"));
    });
    connect(worker, &VoiceWorker::transcriptReady, this, [this, generation](const QString &text, const quint64 epoch) {
        if (m_enabled && generation == m_workerGeneration && epoch == m_captureEpoch
            && m_wantsListening && !m_speaking && !m_echoTail) processTranscript(text);
    });
    connect(worker, &VoiceWorker::errorOccurred, this, [this, generation](const QString &code, const QString &message, const bool fatal) {
        if (!m_enabled || generation != m_workerGeneration) return;
        if (fatal) setEnabled(false);
        setError(code, message);
    });
    setStatus(m_sttProvider == QStringLiteral("groq") ? QStringLiteral("Preparing microphone") : QStringLiteral("Loading local speech recognition"));
    thread->start();
}

void VoiceController::retireWorker() {
    ++m_workerGeneration;
    ++m_captureEpoch;
    const QPointer<VoiceWorker> worker = m_worker;
    const QPointer<QThread> thread = m_workerThread;
    m_worker = nullptr;
    m_workerThread = nullptr;
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
    const bool wanted = m_wantsListening;
    setEnabled(false);
    setEnabled(true);
    m_wantsListening = wanted;
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
    ++m_captureEpoch;
    m_processing = false;
    setListeningState(false);
    updateCapture();
}

void VoiceController::startListening() {
    if (!m_enabled) return;
    m_wantsListening = true;
    updateCapture();
}

void VoiceController::stopListening() {
    m_wantsListening = false;
    ++m_captureEpoch;
    m_processing = false;
    setListeningState(false);
    updateCapture();
    if (!m_speaking && m_enabled) setStatus(QStringLiteral("Ready"));
}

void VoiceController::processTranscript(const QString &transcript) {
    if (m_modelTestBusy) return;
    const QString text = transcript.trimmed();
    if (text.isEmpty()) return;
    if (text.size() > 16384) {
        setError(QStringLiteral("VOICE_TRANSCRIPT_TOO_LONG"), QStringLiteral("That transcript is too long. Answer in a shorter utterance."));
        return;
    }
    m_lastTranscript = text;
    emit lastTranscriptChanged();
    const struct VoiceWorker::ParsedUtterance parsed = VoiceWorker::parseUtterance(text);
    if (parsed.isCommand) emit commandRecognized(parsed.command, parsed.argument);
    else emit answerRecognized(text);
}

void VoiceController::speak(const QString &text) {
    if (!m_enabled || m_modelTestBusy || text.trimmed().isEmpty()) return;
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
    if (m_ttsProvider == QStringLiteral("local")) return;
    const quint64 generation = m_speechGeneration;
    QTimer::singleShot(5000, this, [this, generation] {
        if (generation == m_speechGeneration && m_speaking && !m_ttsStarted)
            speechError(QStringLiteral("The system speech engine did not initialize."));
    });
}

void VoiceController::beginSpeech() {
    if ((!m_enabled && !m_ttsTestBusy) || !m_speaking || !m_captureSuspended || m_ttsIssued || m_pendingSpeech.isEmpty()) return;
    if (m_ttsProvider == QStringLiteral("local")) { beginLocalSpeech(); return; }
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
    ++m_speechGeneration;
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
    setError(QStringLiteral("VOICE_TTS_UNAVAILABLE"), detail.isEmpty()
        ? QStringLiteral("Speech output is unavailable. Choose a local voice in Settings > Voice and check the audio output.")
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
