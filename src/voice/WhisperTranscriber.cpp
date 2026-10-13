#include "WhisperTranscriber.h"

#include "VoiceAudio.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QDir>

namespace {
constexpr qsizetype kMaximumResponseBytes = 65536;
constexpr int kTimeoutMs = 45000;
}

WhisperTranscriber::WhisperTranscriber(QString modelPath, QString vocabulary, QObject *parent)
    : QObject(parent), m_modelPath(std::move(modelPath)), m_vocabulary(std::move(vocabulary)) {}

WhisperTranscriber::~WhisperTranscriber() { cancel(); }

void WhisperTranscriber::submit(const QByteArray &pcm, const quint64 epoch) {
    Q_ASSERT(!pcm.isEmpty() && pcm.size() % 2 == 0 && pcm.size() <= VoiceUtteranceBuffer::MaxBytes);
    cancel();
    auto *const file = new QTemporaryFile(QDir(QDir::tempPath()).filePath(QStringLiteral("betterflash-whisper-XXXXXX.wav")), this);
    file->setAutoRemove(true);
    if (!file->open() || file->write(voiceWav(pcm)) != 44 + pcm.size() || !file->flush()) {
        file->deleteLater();
        emit errorOccurred(QStringLiteral("VOICE_WHISPER_AUDIO"), QStringLiteral("The temporary speech recording could not be prepared."), false, epoch);
        emit finished(epoch);
        return;
    }
    m_audioFile = file;
    const QString runner = qEnvironmentVariable("BETTERFLASH_WHISPER_EXECUTABLE").trimmed().isEmpty()
        ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("betterflash-whisper"))
        : qEnvironmentVariable("BETTERFLASH_WHISPER_EXECUTABLE").trimmed();
    if (!QFileInfo(runner).isFile() || !QFileInfo(runner).isExecutable()) {
        emit errorOccurred(QStringLiteral("VOICE_WHISPER_RUNTIME"), QStringLiteral("The local Whisper runtime is unavailable. Build or install betterflash-whisper."), true, epoch);
        file->deleteLater();
        m_audioFile = nullptr;
        emit finished(epoch);
        return;
    }
    auto *const process = new QProcess(this);
    m_process = process;
    const quint64 generation = ++m_generation;
    m_stdout.clear();
    m_stderr.clear();
    m_reportedOverflow = false;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, generation] {
        if (generation != m_generation) return;
        m_stdout.append(process->readAllStandardOutput());
        if (m_stdout.size() > kMaximumResponseBytes && !m_reportedOverflow) {
            m_reportedOverflow = true;
            process->kill();
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process, generation] {
        if (generation != m_generation) return;
        m_stderr.append(process->readAllStandardError());
        if (m_stderr.size() > kMaximumResponseBytes) m_stderr.truncate(kMaximumResponseBytes);
    });
    connect(process, &QProcess::finished, this, [this, process, generation, epoch](const int exitCode, const QProcess::ExitStatus status) {
        if (generation != m_generation || m_process != process) return;
        m_stdout.append(process->readAllStandardOutput());
        m_stderr.append(process->readAllStandardError());
        const QByteArray output = m_stdout;
        const QByteArray errors = m_stderr;
        if (m_reportedOverflow || output.size() > kMaximumResponseBytes) {
            emit errorOccurred(QStringLiteral("VOICE_WHISPER_RESPONSE"), QStringLiteral("The local Whisper runtime returned too much output."), false, epoch);
        } else if (status != QProcess::NormalExit || exitCode != 0) {
            Q_UNUSED(errors);
            emit errorOccurred(QStringLiteral("VOICE_WHISPER_FAILED"), QStringLiteral("The local Whisper runtime failed to transcribe the recording. Check the model file and try again."), false, epoch);
        } else {
            struct QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
            const QString transcript = document.isObject() ? document.object().value(QStringLiteral("transcript")).toString().trimmed() : QString();
            if (parseError.error != QJsonParseError::NoError || !document.isObject() || transcript.isEmpty())
                emit errorOccurred(QStringLiteral("VOICE_WHISPER_OUTPUT"), QStringLiteral("The local Whisper runtime returned no transcript."), false, epoch);
            else emit transcriptReady(transcript, epoch);
        }
        finish(generation, epoch);
    });
    QTimer::singleShot(kTimeoutMs, process, [this, process, generation, epoch] {
        if (generation == m_generation && m_process == process) {
            process->kill();
            emit errorOccurred(QStringLiteral("VOICE_WHISPER_TIMEOUT"), QStringLiteral("Local Whisper transcription timed out."), false, epoch);
            finish(generation, epoch);
        }
    });
    QJsonObject request{{QStringLiteral("modelPath"), m_modelPath}, {QStringLiteral("audioPath"), file->fileName()}, {QStringLiteral("vocabulary"), m_vocabulary}};
    const QByteArray requestJson = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    connect(process, &QProcess::started, this, [process, requestJson] {
        process->write(requestJson);
        process->closeWriteChannel();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation, epoch](const QProcess::ProcessError error) {
        if (generation != m_generation || m_process != process || error != QProcess::FailedToStart) return;
        emit errorOccurred(QStringLiteral("VOICE_WHISPER_START"), QStringLiteral("The local Whisper runtime could not be started."), true, epoch);
        finish(generation, epoch);
    });
    process->start(runner, {});
}

void WhisperTranscriber::finish(const quint64 generation, const quint64 epoch) {
    if (generation != m_generation) return;
    const QPointer<QProcess> process = m_process;
    m_process = nullptr;
    if (process) process->deleteLater();
    if (m_audioFile) { m_audioFile->deleteLater(); m_audioFile = nullptr; }
    emit finished(epoch);
}

void WhisperTranscriber::cancel() {
    ++m_generation;
    const QPointer<QProcess> process = m_process;
    m_process = nullptr;
    if (process) { process->kill(); process->deleteLater(); }
    if (m_audioFile) { m_audioFile->deleteLater(); m_audioFile = nullptr; }
}
