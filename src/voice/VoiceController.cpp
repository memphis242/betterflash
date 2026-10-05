#include "VoiceController.h"

#include "VoiceWorker.h"
#include "integrations/SecretStore.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QTextToSpeech>
#include <QThread>
#include <QTimer>
#include <utility>
#ifdef Q_OS_ANDROID
#include <QMicrophonePermission>
#endif

VoiceController::VoiceController(QObject *parent)
    : QObject(parent), m_secret(new SecretStore(QStringLiteral("groq"), QStringLiteral("GROQ_API_KEY"), this)) {
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice"));
    m_modelPath = settings.value(QStringLiteral("modelPath")).toString();
    if (m_modelPath.isEmpty()) m_modelPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("voice/model"));
    const QString provider = settings.value(QStringLiteral("provider"), QStringLiteral("groq")).toString();
    if (provider == QStringLiteral("local")) m_sttProvider = provider;
    const QString model = settings.value(QStringLiteral("sttModel"), m_sttModel).toString().trimmed();
    static const QRegularExpression validModel(QStringLiteral(R"(^[A-Za-z0-9._:-]{1,128}$)"));
    if (validModel.match(model).hasMatch()) m_sttModel = model;
    m_lastKey = m_secret->key();
    connect(m_secret, &SecretStore::changed, this, [this] {
        emit hasApiKeyChanged();
        emit keyStorageStatusChanged();
        const QString key = m_secret->key();
        if (key != m_lastKey) {
            m_lastKey = key;
            if (m_enabled && m_sttProvider == QStringLiteral("groq")) restartWorker();
        }
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { setEnabled(false); });
}

VoiceController::~VoiceController() {
    m_enabled = false;
    retireWorker();
    if (m_speech) m_speech->stop(QTextToSpeech::BoundaryHint::Immediate);
    // A worker owns all decoding and audio resources until its event loop exits.
    const QDeadlineTimer deadline(250);
    for (const QPointer<QThread> &thread : std::as_const(m_threads))
        if (thread && thread->isRunning() && !deadline.hasExpired()) thread->wait(deadline);
}

bool VoiceController::hasApiKey() const { return m_secret->hasKey(); }
QString VoiceController::keyStorageStatus() const { return m_secret->status(); }

void VoiceController::persistConfiguration() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice"));
    settings.setValue(QStringLiteral("modelPath"), m_modelPath);
    settings.setValue(QStringLiteral("provider"), m_sttProvider);
    settings.setValue(QStringLiteral("sttModel"), m_sttModel);
}

void VoiceController::setModelPath(const QString &path) {
    const QString value = path.trimmed();
    if (m_modelPath == value) return;
    m_modelPath = value;
    persistConfiguration();
    emit modelPathChanged();
    if (m_enabled && m_sttProvider == QStringLiteral("local")) restartWorker();
}

void VoiceController::setSttProvider(const QString &provider) {
    QString value = provider.trimmed().toLower();
    if (value == QStringLiteral("local test")) value = QStringLiteral("local");
    if (value != QStringLiteral("groq") && value != QStringLiteral("local")) {
        setError(QStringLiteral("VOICE_PROVIDER_INVALID"), QStringLiteral("Select Groq or the optional local test provider."));
        return;
    }
    if (m_sttProvider == value) return;
    m_sttProvider = value;
    persistConfiguration();
    emit sttProviderChanged();
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
    m_sttModel = value;
    persistConfiguration();
    emit sttModelChanged();
    if (m_enabled && m_sttProvider == QStringLiteral("groq")) restartWorker();
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
    if (!m_enabled || m_worker) return;
    const quint64 generation = ++m_workerGeneration;
    auto *const thread = new QThread;
    auto *const worker = new VoiceWorker(m_modelPath);
    worker->setProvider(m_sttProvider);
    worker->setSttModel(m_sttModel);
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
    setStatus(m_sttProvider == QStringLiteral("groq") ? QStringLiteral("Preparing microphone") : QStringLiteral("Loading local test model"));
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
    if (!m_enabled || text.trimmed().isEmpty()) return;
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
    const quint64 generation = m_speechGeneration;
    QTimer::singleShot(5000, this, [this, generation] {
        if (generation == m_speechGeneration && m_speaking && !m_ttsStarted)
            speechError(QStringLiteral("The system speech engine did not initialize."));
    });
}

void VoiceController::beginSpeech() {
    if (!m_enabled || !m_speaking || !m_captureSuspended || m_ttsIssued || m_pendingSpeech.isEmpty()) return;
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
    const QString detail = message.trimmed();
    setError(QStringLiteral("VOICE_TTS_UNAVAILABLE"), detail.isEmpty()
        ? QStringLiteral("System speech is unavailable. Install and start Speech Dispatcher on Fedora, or enable a text to speech engine in Android settings.")
        : QStringLiteral("System speech failed: %1 Check the system text to speech engine and audio output.").arg(detail));
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
