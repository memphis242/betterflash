#include "VoiceWorker.h"
#include "VoiceTranscriber.h"
#include "WhisperTranscriber.h"

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
#include <QtEndian>
#include <optional>
#include <algorithm>

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

bool validationDigitsMatch(QString text) {
    text = text.toCaseFolded();
    static const QStringList words = {QStringLiteral("zero"), QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three"),
        QStringLiteral("four"), QStringLiteral("five"), QStringLiteral("six"), QStringLiteral("seven"), QStringLiteral("eight"), QStringLiteral("nine")};
    for (int i = 0; i < words.size(); ++i)
        text.replace(QRegularExpression(QStringLiteral("\\b%1\\b").arg(words.at(i))), QString::number(i));
    text.replace(QRegularExpression(QStringLiteral("\\boh\\b")), QStringLiteral("0"));
    text.remove(QRegularExpression(QStringLiteral("[.,!?;:'\\\"-]")));
    if (text.contains(QRegularExpression(QStringLiteral("[A-Za-z]")))) return false;
    text.remove(QRegularExpression(QStringLiteral("\\s+")));
    return text == QStringLiteral("100019021001803");
}
}

VoiceWorker::VoiceWorker(QString modelPath, QObject *parent)
    : QObject(parent), m_modelPath(std::move(modelPath)) {}

VoiceWorker::~VoiceWorker() { shutdown(); }

bool VoiceWorker::usesWhisper() const { return m_provider == QStringLiteral("local") && m_modelPath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive); }

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
        Command{"end the review", "stop"}, Command{"finish review", "stop"}, Command{"finish the review", "stop"},
        Command{"end review session", "stop"}, Command{"stop the review", "stop"}, Command{"stop review", "stop"},
        Command{"repeat", "repeat"}, Command{"read card", "repeat"},
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
            if (epoch == m_captureEpoch && m_requestedListening && !m_shuttingDown) reportCaptureError(code, message, fatal, epoch);
        });
        connect(m_transcriber, &VoiceTranscriber::finished, this, [this](const quint64 epoch) {
            if (epoch != m_captureEpoch || !m_requestedListening || m_shuttingDown) return;
            m_processing = false;
            emit processingChanged(false, epoch);
            if (!m_oneShot) resumeCapture(epoch);
        });
    } else if (m_provider == QStringLiteral("local")) {
        if (usesWhisper()) {
            const QString executable = qEnvironmentVariable("BETTERFLASH_WHISPER_EXECUTABLE").trimmed().isEmpty()
                ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("betterflash-whisper"))
                : qEnvironmentVariable("BETTERFLASH_WHISPER_EXECUTABLE").trimmed();
            if (!QFileInfo(m_modelPath).isFile() || !QFileInfo(executable).isExecutable()) {
                reportError(QStringLiteral("VOICE_WHISPER_RUNTIME"), QStringLiteral("The local Whisper model or runtime is unavailable."), true);
                return;
            }
            m_whisper = new WhisperTranscriber(m_modelPath, m_recognitionVocabulary, m_whisperAcceleration, this);
            connect(m_whisper, &WhisperTranscriber::transcriptReady, this, [this](const QString &text, const quint64 epoch) {
                if (epoch == m_captureEpoch && m_requestedListening && !m_shuttingDown) emit transcriptReady(text, epoch);
            });
            connect(m_whisper, &WhisperTranscriber::errorOccurred, this, [this](const QString &code, const QString &message, const bool fatal, const quint64 epoch) {
                if (epoch == m_captureEpoch && m_requestedListening && !m_shuttingDown) reportCaptureError(code, message, fatal, epoch);
            });
            connect(m_whisper, &WhisperTranscriber::finished, this, [this](const quint64 epoch) {
                if (epoch != m_captureEpoch || !m_requestedListening || m_shuttingDown) return;
                m_processing = false;
                emit processingChanged(false, epoch);
                if (!m_oneShot) resumeCapture(epoch);
            });
        } else if (!loadLocalModel()) return;
    } else {
        reportError(QStringLiteral("VOICE_PROVIDER_INVALID"), QStringLiteral("Choose local speech recognition or Groq."), true);
        return;
    }
    if (QThread::currentThread()->isInterruptionRequested()) { shutdown(); return; }
    if (!prepareAudio()) return;
    m_initialized = true;
    emit ready();
}

bool VoiceWorker::loadLocalModel() {
    if (!QDir(m_modelPath).exists() || !loadLibrary()) {
        if (!QDir(m_modelPath).exists())
            reportError(QStringLiteral("VOICE_MODEL_MISSING"), QStringLiteral("The local speech model directory is missing."), true);
        return false;
    }
    const QByteArray modelPath = QFile::encodeName(QDir::cleanPath(m_modelPath));
    m_model = m_modelNew(modelPath.constData());
    if (!m_model) {
        reportError(QStringLiteral("VOICE_MODEL_INVALID"), QStringLiteral("Vosk could not open the local speech model."), true);
        return false;
    }
    if (QThread::currentThread()->isInterruptionRequested()) { shutdown(); return false; }
    m_recognizer = m_recognizerNew(m_model, VoiceUtteranceBuffer::SampleRate);
    if (!m_recognizer) {
        reportError(QStringLiteral("VOICE_RECOGNIZER_FAILED"), QStringLiteral("Vosk could not initialize recognition."), true);
        return false;
    }
    return true;
}

void VoiceWorker::initializeForTest() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_initialized || m_shuttingDown) return;
    m_testMode = true;
    if (m_provider == QStringLiteral("local")) {
        if (usesWhisper()) {
            const QString executable = qEnvironmentVariable("BETTERFLASH_WHISPER_EXECUTABLE").trimmed().isEmpty()
                ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("betterflash-whisper"))
                : qEnvironmentVariable("BETTERFLASH_WHISPER_EXECUTABLE").trimmed();
            if (!QFileInfo(m_modelPath).isFile() || !QFileInfo(executable).isExecutable()) {
                emit testFinished({}, false, QStringLiteral("VOICE_WHISPER_RUNTIME: The local Whisper model or runtime is unavailable."));
                return;
            }
            m_whisper = new WhisperTranscriber(m_modelPath, m_recognitionVocabulary, m_whisperAcceleration, this);
            connect(m_whisper, &WhisperTranscriber::transcriptReady, this, [this](const QString &text, const quint64) {
                if (!m_testMode || m_shuttingDown || m_testAnswered) return;
                m_testAnswered = true;
                emit testFinished(text, m_openTranscriptionTest || validationDigitsMatch(text), {});
            });
            connect(m_whisper, &WhisperTranscriber::errorOccurred, this, [this](const QString &code, const QString &message, const bool, const quint64) {
                if (m_shuttingDown || m_testAnswered) return;
                m_testAnswered = true;
                emit testFinished({}, false, code + QStringLiteral(": ") + message);
            });
            connect(m_whisper, &WhisperTranscriber::finished, this, [this](const quint64) {
                if (!m_shuttingDown && !m_testAnswered) { m_testAnswered = true; emit testFinished({}, false, QStringLiteral("The local Whisper runtime returned no transcript.")); }
            });
        } else if (!loadLocalModel()) return;
    } else if (m_provider == QStringLiteral("groq")) {
        if (m_apiKey.isEmpty()) { emit testFinished({}, false, QStringLiteral("Add a Groq API key before testing cloud speech recognition.")); return; }
        m_transcriber = new VoiceTranscriber(m_apiKey, m_sttModel, this);
        connect(m_transcriber, &VoiceTranscriber::transcriptReady, this, [this](const QString &text, const quint64) {
            if (!m_testMode || m_shuttingDown || m_testAnswered) return;
            m_testAnswered = true;
            if (m_openTranscriptionTest) { emit testFinished(text, false, {}); return; }
            QString digits = text.toCaseFolded();
            static const QStringList words = {QStringLiteral("zero"), QStringLiteral("one"), QStringLiteral("two"), QStringLiteral("three"),
                QStringLiteral("four"), QStringLiteral("five"), QStringLiteral("six"), QStringLiteral("seven"), QStringLiteral("eight"), QStringLiteral("nine")};
            for (int i = 0; i < words.size(); ++i)
                digits.replace(QRegularExpression(QStringLiteral("\\b%1\\b").arg(words[i])), QString::number(i));
            digits.replace(QRegularExpression(QStringLiteral("\\boh\\b")), QStringLiteral("0"));
            digits.remove(QRegularExpression(QStringLiteral("[^0-9]")));
            const bool passed = digits == QStringLiteral("100019021001803");
            emit testFinished(text, passed, passed ? QString() : QStringLiteral("The response did not match the known spoken digits. Try another transcription model."));
        });
        connect(m_transcriber, &VoiceTranscriber::errorOccurred, this, [this](const QString &code, const QString &message, const bool, const quint64) {
            if (m_shuttingDown || m_testAnswered) return;
            m_testAnswered = true;
            emit testFinished({}, false, code + QStringLiteral(": ") + message);
        });
        connect(m_transcriber, &VoiceTranscriber::finished, this, [this](const quint64) {
            if (!m_shuttingDown && !m_testAnswered) { m_testAnswered = true; emit testFinished({}, false, QStringLiteral("The transcription provider returned no speech.")); }
        });
    } else { emit testFinished({}, false, QStringLiteral("Choose local recognition or Groq.")); return; }
    m_initialized = true;
    emit ready();
}

bool VoiceWorker::decodeWav(const QByteArray &wav, QByteArray *pcm, int *sampleRate) const {
    if (!pcm || !sampleRate || wav.size() < 44 || wav.mid(0, 4) != QByteArrayLiteral("RIFF") || wav.mid(8, 4) != QByteArrayLiteral("WAVE")) return false;
    int offset = 12; quint16 channels = 0; quint16 bits = 0; quint16 format = 0; quint32 rate = 0; int dataOffset = -1; quint32 dataSize = 0;
    while (offset + 8 <= wav.size()) {
        const QByteArray id = wav.mid(offset, 4); const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(wav.constData() + offset + 4)); offset += 8;
        if (size > static_cast<quint32>(wav.size() - offset)) return false;
        if (id == QByteArrayLiteral("fmt ") && size >= 16) { format = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(wav.constData() + offset)); channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(wav.constData() + offset + 2)); rate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(wav.constData() + offset + 4)); bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(wav.constData() + offset + 14)); }
        if (id == QByteArrayLiteral("data")) { dataOffset = offset; dataSize = size; break; }
        offset += static_cast<int>(size) + (size & 1U);
    }
    if (format != 1 || channels != 1 || bits != 16 || rate == 0 || rate > 192000 || dataOffset < 0
        || dataSize % 2 != 0 || dataSize > VoiceUtteranceBuffer::MaxBytes) return false;
    *pcm = wav.mid(dataOffset, static_cast<qsizetype>(dataSize)); *sampleRate = static_cast<int>(rate); return !pcm->isEmpty();
}

void VoiceWorker::testAudio(const QByteArray &wav) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_testMode || m_testSubmitted) return;
    m_testSubmitted = true;
    QByteArray pcm; int sampleRate = 0;
    if (!m_initialized || !decodeWav(wav, &pcm, &sampleRate) || sampleRate != VoiceUtteranceBuffer::SampleRate) { emit testFinished({}, false, QStringLiteral("The validation audio must be mono 16-bit PCM at 16000 Hz.")); return; }
    if (m_provider == QStringLiteral("groq")) { m_transcriber->submit(pcm, 0); return; }
    if (usesWhisper()) { m_whisper->submit(pcm, 0); return; }
    if (!m_recognizer || !m_acceptWaveform || !m_result || !m_finalResult) { emit testFinished({}, false, QStringLiteral("The local speech recognizer is unavailable.")); return; }
    m_recognizerReset(m_recognizer);
    QStringList parts;
    constexpr qsizetype ChunkBytes = 6400;
    for (qsizetype offset = 0; offset < pcm.size(); offset += ChunkBytes) {
        if (QThread::currentThread()->isInterruptionRequested()) return;
        if (m_acceptWaveform(m_recognizer, pcm.constData() + offset, static_cast<int>(std::min(ChunkBytes, pcm.size() - offset))))
            parts.append(QJsonDocument::fromJson(QByteArray(m_result(m_recognizer))).object().value(QStringLiteral("text")).toString());
    }
    parts.append(QJsonDocument::fromJson(QByteArray(m_finalResult(m_recognizer))).object().value(QStringLiteral("text")).toString());
    const QString transcript = parts.join(QLatin1Char(' ')).simplified();
    if (m_openTranscriptionTest) { emit testFinished(transcript, false, {}); return; }
    const QString expected = QStringLiteral("one zero zero zero one nine oh two one oh zero one eight zero three");
    const bool passed = transcript == expected;
    emit testFinished(transcript, passed, passed ? QString() : QStringLiteral("Expected the bundled Vosk validation phrase."));
}

bool VoiceWorker::loadLibrary() {
    const QStringList candidates = {qEnvironmentVariable("BETTERFLASH_VOSK_LIBRARY"),
        QDir(m_runtimePath).filePath(QStringLiteral("libvosk.so")),
        QDir(m_modelPath).filePath(QStringLiteral("libvosk.so")),
        QDir(m_modelPath).filePath(QStringLiteral("libvosk.dll")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lib/libvosk.so")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("lib/libvosk.dll")),
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
            m_finalResult = reinterpret_cast<Result>(library->resolve("vosk_recognizer_final_result"));
            if (m_modelNew && m_modelFree && m_recognizerNew && m_recognizerFree
                && m_recognizerReset && m_acceptWaveform && m_result && m_finalResult) {
                m_library = library;
                return true;
            }
            library->unload();
        }
        delete library;
    }
    reportError(QStringLiteral("VOICE_LIBRARY_MISSING"), QStringLiteral("The local recognition runtime is missing. Download the speech recognition files in Settings > Voice."), true);
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
            reportCaptureError(QStringLiteral("VOICE_MICROPHONE_FAILED"), QStringLiteral("Microphone capture stopped. Check audio input access, then enable voice again."), true, m_captureEpoch);
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
        reportCaptureError(QStringLiteral("VOICE_MICROPHONE_FAILED"), QStringLiteral("Microphone capture failed. Check the default input and microphone permission, then enable voice again."), true, m_captureEpoch);
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
    if (m_provider == QStringLiteral("groq") || usesWhisper()) {
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
    Q_ASSERT((m_provider == QStringLiteral("groq") && m_transcriber) || (usesWhisper() && m_whisper));
    m_processing = true;
    stopCapture();
    emit processingChanged(true, m_captureEpoch);
    if (usesWhisper()) m_whisper->submit(pcm, m_captureEpoch);
    else m_transcriber->submit(pcm, m_captureEpoch);
}

void VoiceWorker::resumeCapture(const quint64 epoch) {
    QTimer::singleShot(100, this, [this, epoch] {
        if (epoch == m_captureEpoch && m_requestedListening && !m_shuttingDown) startCapture();
    });
}

void VoiceWorker::processLocalResult(const QByteArray &json) {
    const QString text = QJsonDocument::fromJson(json).object().value(QStringLiteral("text")).toString().trimmed();
    if (!m_requestedListening || m_shuttingDown) return;
    const quint64 epoch = m_captureEpoch;
    if (m_oneShot) {
        stopCapture();
    }
    if (!text.isEmpty()) emit transcriptReady(text, epoch);
    if (m_oneShot) emit oneShotFinished(epoch);
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
    if (m_whisper) m_whisper->cancel();
    if (m_processing) {
        m_processing = false;
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

void VoiceWorker::reportCaptureError(const QString &code, const QString &message, const bool fatal, const quint64 epoch) {
    if (fatal) {
        m_requestedListening = false;
        m_initialized = false;
        cancelRequest();
        stopCapture();
    }
    emit captureError(code, message, fatal, epoch);
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
    if (m_whisper) { m_whisper->deleteLater(); m_whisper = nullptr; }
    m_apiKey.clear();
    m_initialized = false;
}
