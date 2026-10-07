#include "VoiceWorker.h"
#include "VoiceTranscriber.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QMediaDevices>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <optional>

namespace {
QString normalize(const QString &value) { return value.trimmed().toCaseFolded().simplified(); }

std::optional<int> spokenNumber(QString text) {
    text = normalize(text);
    bool isInteger = false;
    const int integer = text.toInt(&isInteger);
    if (isInteger) return integer;
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    static const QStringList units = {QStringLiteral("zero"), QStringLiteral("one"), QStringLiteral("two"),
        QStringLiteral("three"), QStringLiteral("four"), QStringLiteral("five"), QStringLiteral("six"),
        QStringLiteral("seven"), QStringLiteral("eight"), QStringLiteral("nine"), QStringLiteral("ten"),
        QStringLiteral("eleven"), QStringLiteral("twelve"), QStringLiteral("thirteen"), QStringLiteral("fourteen"),
        QStringLiteral("fifteen"), QStringLiteral("sixteen"), QStringLiteral("seventeen"), QStringLiteral("eighteen"),
        QStringLiteral("nineteen")};
    static const QStringList tens = {QStringLiteral("twenty"), QStringLiteral("thirty"), QStringLiteral("forty"),
        QStringLiteral("fifty"), QStringLiteral("sixty"), QStringLiteral("seventy"), QStringLiteral("eighty"),
        QStringLiteral("ninety")};
    if (text == QStringLiteral("a") || text == QStringLiteral("an")) return 1;
    if (text == QStringLiteral("one hundred") || text == QStringLiteral("hundred")) return 100;
    const qsizetype unit = units.indexOf(text);
    if (unit >= 0) return static_cast<int>(unit);
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty() || words.size() > 2) return std::nullopt;
    const qsizetype ten = tens.indexOf(words.first());
    if (ten < 0) return std::nullopt;
    if (words.size() == 1) return (static_cast<int>(ten) + 2) * 10;
    const qsizetype last = units.indexOf(words.last());
    if (last < 1 || last > 9) return std::nullopt;
    return (static_cast<int>(ten) + 2) * 10 + static_cast<int>(last);
}
}

VoiceWorker::VoiceWorker(QString modelPath, QObject *parent)
    : QObject(parent), m_modelPath(std::move(modelPath)) {}

VoiceWorker::~VoiceWorker() { shutdown(); }

struct VoiceWorker::ParsedUtterance VoiceWorker::parseUtterance(const QString &utterance) {
    QString original = utterance.trimmed().simplified();
    static const QRegularExpression punctuation(QStringLiteral(R"([.!?,]+$)"));
    static const QRegularExpression wakeWord(QStringLiteral(R"(^flashcard[,:]?\s+)"), QRegularExpression::CaseInsensitiveOption);
    original.remove(punctuation);
    original.remove(wakeWord);
    const QString text = normalize(original);
    if (text.isEmpty()) return {};

    static const QRegularExpression fraction(QStringLiteral(R"(^(?:partial(?: recall)?\s+)?([a-z0-9 -]+?) (?:out of|of) ([a-z0-9 -]+?)(?: points?)?$)"));
    const QRegularExpressionMatch fractionMatch = fraction.match(text);
    if (fractionMatch.hasMatch()) {
        const auto recalled = spokenNumber(fractionMatch.captured(1));
        const auto total = spokenNumber(fractionMatch.captured(2));
        if (recalled && total && *total > 0 && *total <= 100 && *recalled >= 0 && *recalled <= *total)
            return {true, QStringLiteral("partial"), static_cast<double>(*recalled) / *total};
    }

    static const QRegularExpression postpone(QStringLiteral(R"(^(?:postpone|delay)(?: card| review)?(?: for)? ([a-z0-9 -]+?) (days?|weeks?)$)"));
    const QRegularExpressionMatch postponeMatch = postpone.match(text);
    if (postponeMatch.hasMatch()) {
        const auto count = spokenNumber(postponeMatch.captured(1));
        const int multiplier = postponeMatch.captured(2).startsWith(QStringLiteral("week")) ? 7 : 1;
        if (count && *count > 0 && *count <= 3650 / multiplier)
            return {true, QStringLiteral("postpone"), *count * multiplier};
    }

    struct Command { const char *phrase; const char *name; };
    static constexpr struct Command commands[] = {
        Command{"start review", "start_review"}, Command{"begin review", "start_review"},
        Command{"show answer", "show_answer"}, Command{"reveal answer", "show_answer"},
        Command{"missed", "missed"}, Command{"again", "again"}, Command{"partial", "partial"},
        Command{"partial recall", "partial"}, Command{"half", "partial"}, Command{"hard", "hard"},
        Command{"good", "good"}, Command{"easy", "easy"}, Command{"defer", "defer"},
        Command{"skip", "defer"}, Command{"defer card", "defer"}, Command{"skip card", "defer"},
        Command{"pause", "pause"}, Command{"pause review", "pause"}, Command{"resume", "resume"},
        Command{"resume review", "resume"}, Command{"stop", "stop"}, Command{"end review", "stop"},
        Command{"stop review", "stop"}, Command{"repeat", "repeat"}, Command{"read card", "repeat"},
        Command{"repeat card", "repeat"}, Command{"summarize remaining cards", "summarize"},
        Command{"next", "next"}, Command{"next card", "next"},
        Command{"previous", "previous"}, Command{"previous card", "previous"},
        Command{"confirm rating", "confirm_rating"}, Command{"change rating", "confirm_rating"},
        Command{"cancel rating", "cancel_rating"}, Command{"keep rating", "cancel_rating"}
    };
    for (const struct Command &command : commands) {
        if (text == QLatin1String(command.phrase))
            return {true, QString::fromLatin1(command.name),
                text.startsWith(QStringLiteral("partial")) || text == QStringLiteral("half") ? QVariant(0.5) : QVariant{}};
    }

    static const QRegularExpression startDeck(QStringLiteral(R"(^(?:(?:start|begin) review(?: (?:of|for|in|deck))?|review deck|start deck) (.+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch deck = startDeck.match(original);
    if (deck.hasMatch() && !deck.captured(1).trimmed().isEmpty())
        return {true, QStringLiteral("start_review"), deck.captured(1).trimmed()};
    return {};
}

QNetworkRequest VoiceWorker::groqRequest(const QString &apiKey) {
    return VoiceTranscriber::request(apiKey);
}

void VoiceWorker::initialize() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_initialized || m_shuttingDown) return;
    if (m_provider == QStringLiteral("groq")) {
        if (m_apiKey.trimmed().isEmpty()) {
            reportError(QStringLiteral("GROQ_KEY_MISSING"), QStringLiteral("Add a Groq API key in Voice settings to enable speech recognition."), true);
            return;
        }
        if (m_apiKey.size() > 4096 || m_apiKey.contains(QLatin1Char('\r')) || m_apiKey.contains(QLatin1Char('\n'))) {
            reportError(QStringLiteral("GROQ_KEY_INVALID"), QStringLiteral("Enter a single Groq API key without line breaks in Voice settings."), true);
            return;
        }
        m_transcriber = new VoiceTranscriber(m_apiKey, m_sttModel, this);
        connect(m_transcriber, &VoiceTranscriber::transcriptReady, this, [this](const QString &text, const quint64 epoch) {
            if (epoch == m_captureEpoch && m_requestedListening && !m_shuttingDown) emit transcriptReady(text, epoch);
        });
        connect(m_transcriber, &VoiceTranscriber::errorOccurred, this, [this](const QString &code, const QString &message, const bool fatal, const quint64 epoch) {
            if (epoch == m_captureEpoch && m_requestedListening && !m_shuttingDown) reportError(code, message, fatal);
        });
        connect(m_transcriber, &VoiceTranscriber::finished, this, [this](const quint64 epoch) {
            if (epoch != m_captureEpoch || !m_requestedListening || m_shuttingDown) return;
            m_processing = false;
            emit processingChanged(false, epoch);
            resumeCapture(epoch);
        });
    } else if (m_provider == QStringLiteral("local")) {
        if (!QDir(m_modelPath).exists() || !loadLibrary()) {
            if (!QDir(m_modelPath).exists())
                reportError(QStringLiteral("VOICE_MODEL_MISSING"), QStringLiteral("Choose an installed Vosk model directory in Voice settings, or select Groq."), true);
            return;
        }
        const QByteArray modelPath = QFile::encodeName(QDir::cleanPath(m_modelPath));
        m_model = m_modelNew(modelPath.constData());
        if (!m_model) {
            reportError(QStringLiteral("VOICE_MODEL_INVALID"), QStringLiteral("Vosk could not open that model. Choose a complete Vosk model directory."), true);
            return;
        }
        if (QThread::currentThread()->isInterruptionRequested()) { shutdown(); return; }
        m_recognizer = m_recognizerNew(m_model, VoiceUtteranceBuffer::SampleRate);
        if (!m_recognizer) {
            reportError(QStringLiteral("VOICE_RECOGNIZER_FAILED"), QStringLiteral("Vosk could not initialize recognition. Check the model and library versions."), true);
            return;
        }
    } else {
        reportError(QStringLiteral("VOICE_PROVIDER_INVALID"), QStringLiteral("Select Groq or the optional local test provider."), true);
        return;
    }
    if (QThread::currentThread()->isInterruptionRequested()) { shutdown(); return; }
    if (!prepareAudio()) return;
    m_initialized = true;
    emit ready();
}

bool VoiceWorker::loadLibrary() {
    const QStringList candidates = {qEnvironmentVariable("BETTERFLASH_VOSK_LIBRARY"),
        QDir(m_modelPath).filePath(QStringLiteral("libvosk.so")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lib/libvosk.so")),
        QStringLiteral("libvosk.so")};
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) continue;
        auto *const library = new QLibrary(candidate, this);
        if (library->load()) {
            m_modelNew = reinterpret_cast<ModelNew>(library->resolve("vosk_model_new"));
            m_modelFree = reinterpret_cast<ModelFree>(library->resolve("vosk_model_free"));
            m_recognizerNew = reinterpret_cast<RecognizerNew>(library->resolve("vosk_recognizer_new"));
            m_recognizerFree = reinterpret_cast<RecognizerFree>(library->resolve("vosk_recognizer_free"));
            m_recognizerReset = reinterpret_cast<RecognizerReset>(library->resolve("vosk_recognizer_reset"));
            m_acceptWaveform = reinterpret_cast<AcceptWaveform>(library->resolve("vosk_recognizer_accept_waveform"));
            m_result = reinterpret_cast<Result>(library->resolve("vosk_recognizer_result"));
            if (m_modelNew && m_modelFree && m_recognizerNew && m_recognizerFree
                && m_recognizerReset && m_acceptWaveform && m_result) {
                m_library = library;
                return true;
            }
            library->unload();
        }
        delete library;
    }
    reportError(QStringLiteral("VOICE_LIBRARY_MISSING"), QStringLiteral("Install the optional Vosk library with scripts/setup-voice.py --vosk, or select Groq."), true);
    return false;
}

bool VoiceWorker::prepareAudio() {
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        reportError(QStringLiteral("VOICE_MICROPHONE_MISSING"), QStringLiteral("Connect a microphone and select a default input in the system sound settings."), true);
        return false;
    }
    QAudioFormat format;
    format.setSampleRate(VoiceUtteranceBuffer::SampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) format = device.preferredFormat();
    if (!device.isFormatSupported(format) || !m_converter.configure(format)) {
        reportError(QStringLiteral("VOICE_AUDIO_FORMAT_UNSUPPORTED"), QStringLiteral("The microphone format is unsupported. Choose a PCM audio input in the system sound settings."), true);
        return false;
    }
    m_audio = new QAudioSource(device, format, this);
    m_audio->setBufferSize(format.bytesForDuration(200000));
    connect(m_audio, &QAudioSource::stateChanged, this, [this](const auto) {
        if (m_listening && m_audio && m_audio->error() != QAudio::NoError)
            reportError(QStringLiteral("VOICE_MICROPHONE_FAILED"), QStringLiteral("Microphone capture stopped. Check audio input access, then enable voice again."), true);
    });
    return true;
}

void VoiceWorker::startCapture() {
    if (!m_initialized || !m_requestedListening || m_processing || m_shuttingDown || m_listening) return;
    Q_ASSERT(m_audio);
    m_converter.reset();
    m_utterance.reset();
    m_audioDevice = m_audio->start();
    if (!m_audioDevice || m_audio->error() != QAudio::NoError) {
        reportError(QStringLiteral("VOICE_MICROPHONE_FAILED"), QStringLiteral("Microphone capture failed. Check the default input and microphone permission, then enable voice again."), true);
        return;
    }
    m_listening = true;
    connect(m_audioDevice, &QIODevice::readyRead, this, &VoiceWorker::readAudio, Qt::UniqueConnection);
    emit listeningChanged(true, m_captureEpoch);
}

void VoiceWorker::stopCapture() {
    m_listening = false;
    if (m_audioDevice) disconnect(m_audioDevice, nullptr, this, nullptr);
    m_audioDevice = nullptr;
    if (m_audio) m_audio->stop();
    m_converter.reset();
    m_utterance.reset();
    if (m_recognizer && m_recognizerReset) m_recognizerReset(m_recognizer);
    emit listeningChanged(false, m_captureEpoch);
}

void VoiceWorker::readAudio() {
    if (!m_listening || !m_audioDevice || m_shuttingDown) return;
    const QByteArray bytes = m_audioDevice->readAll();
    if (bytes.isEmpty()) return;
    const QByteArray pcm = m_converter.convert(bytes);
    if (pcm.isEmpty()) return;
    if (m_provider == QStringLiteral("groq")) {
        const auto recording = m_utterance.append(pcm);
        if (recording) submitRecording(*recording);
        return;
    }
    Q_ASSERT(m_recognizer && m_acceptWaveform && m_result);
    if (m_acceptWaveform(m_recognizer, pcm.constData(), static_cast<int>(pcm.size())) != 0)
        processLocalResult(QByteArray(m_result(m_recognizer)));
}

void VoiceWorker::submitRecording(const QByteArray &pcm) {
    if (!m_initialized || !m_requestedListening || m_shuttingDown || pcm.isEmpty()) return;
    Q_ASSERT(m_provider == QStringLiteral("groq") && m_transcriber && !m_transcriber->busy());
    m_processing = true;
    stopCapture();
    emit processingChanged(true, m_captureEpoch);
    m_transcriber->submit(pcm, m_captureEpoch);
}

void VoiceWorker::resumeCapture(const quint64 epoch) {
    QTimer::singleShot(100, this, [this, epoch] {
        if (epoch == m_captureEpoch && m_requestedListening && !m_shuttingDown) startCapture();
    });
}

void VoiceWorker::processLocalResult(const QByteArray &json) {
    const QString text = QJsonDocument::fromJson(json).object().value(QStringLiteral("text")).toString().trimmed();
    if (!text.isEmpty() && m_requestedListening && !m_shuttingDown) emit transcriptReady(text, m_captureEpoch);
}

void VoiceWorker::setListening(const bool listening, const quint64 epoch) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_shuttingDown) return;
    if (epoch < m_captureEpoch) return;
    if (epoch != m_captureEpoch || !listening) {
        m_captureEpoch = epoch;
        m_requestedListening = false;
        cancelRequest();
        stopCapture();
    }
    m_requestedListening = listening && m_initialized;
    if (m_requestedListening) startCapture();
    else emit listeningChanged(false, m_captureEpoch);
}

void VoiceWorker::cancelRequest() {
    if (m_transcriber) m_transcriber->cancel();
    if (m_processing) {
        m_processing = false;
        emit processingChanged(false, m_captureEpoch);
    }
}

void VoiceWorker::reportError(const QString &code, const QString &message, const bool fatal) {
    if (fatal) {
        m_requestedListening = false;
        m_initialized = false;
        cancelRequest();
        stopCapture();
    }
    emit errorOccurred(code, message, fatal);
}

void VoiceWorker::shutdown() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_shuttingDown) return;
    m_shuttingDown = true;
    m_requestedListening = false;
    cancelRequest();
    stopCapture();
    delete m_audio;
    m_audio = nullptr;
    if (m_recognizer && m_recognizerFree) m_recognizerFree(m_recognizer);
    if (m_model && m_modelFree) m_modelFree(m_model);
    m_recognizer = nullptr;
    m_model = nullptr;
    if (m_library) {
        m_library->unload();
        delete m_library;
        m_library = nullptr;
    }
    m_apiKey.clear();
    m_initialized = false;
}
