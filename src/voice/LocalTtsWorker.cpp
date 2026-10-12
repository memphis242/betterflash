#include "LocalTtsWorker.h"

#include <c-api.h>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr int MaximumSeconds = 90;
constexpr qsizetype MaximumTextLength = 8192;

struct SynthesisLimit {
    const std::atomic_bool *cancelled = nullptr;
    qint64 generatedSamples = 0;
    qint64 maximumSamples = 0;
    bool exceeded = false;
};

int32_t continueSynthesis(const float *, const int32_t count, void *context) {
    auto *const limit = static_cast<struct SynthesisLimit *>(context);
    limit->generatedSamples += std::max(count, 0);
    limit->exceeded = limit->generatedSamples > limit->maximumSamples;
    return limit->cancelled->load(std::memory_order_relaxed) || limit->exceeded
        || QThread::currentThread()->isInterruptionRequested() ? 0 : 1;
}

QString boundedSentences(const QString &text) {
    QString result;
    for (qsizetype offset = 0; offset < text.size();) {
        qsizetype end = std::min(offset + 320, text.size());
        if (end < text.size()) {
            const qsizetype space = text.lastIndexOf(QLatin1Char(' '), end);
            if (space > offset + 160) end = space;
            if (text.at(end).isLowSurrogate() && text.at(end - 1).isHighSurrogate()) --end;
        }
        const QString chunk = text.mid(offset, end - offset).trimmed();
        result.append(chunk);
        if (!chunk.isEmpty() && !QStringLiteral(".!?").contains(chunk.back())) result.append(QLatin1Char('.'));
        result.append(QLatin1Char(' '));
        offset = end;
    }
    return result;
}

QByteArray playbackPcm(const struct SherpaOnnxGeneratedAudio &audio,
    const QAudioFormat &format, const std::shared_ptr<std::atomic_bool> &cancelled) {
    const qint64 frames = static_cast<qint64>(audio.n) * format.sampleRate() / audio.sample_rate;
    const qint64 byteCount = frames * format.bytesPerFrame();
    if (byteCount <= 0 || byteCount > 64 * 1024 * 1024) return {};
    QByteArray output(static_cast<qsizetype>(byteCount), '\0');
    char *destination = output.data();
    for (qint64 frame = 0; frame < frames; ++frame) {
        if ((frame & 4095) == 0 && cancelled->load(std::memory_order_relaxed)) return {};
        const double position = static_cast<double>(frame) * audio.sample_rate / format.sampleRate();
        const int index = std::min(static_cast<int>(position), audio.n - 1);
        const int next = std::min(index + 1, audio.n - 1);
        const float fraction = static_cast<float>(position - index);
        const float sample = std::clamp(audio.samples[index] * (1.0F - fraction) + audio.samples[next] * fraction, -1.0F, 1.0F);
        for (int channel = 0; channel < format.channelCount(); ++channel) {
            switch (format.sampleFormat()) {
            case QAudioFormat::UInt8: {
                const quint8 value = static_cast<quint8>(std::lround((sample + 1.0F) * 127.5F));
                std::memcpy(destination, &value, sizeof(value)); destination += sizeof(value); break;
            }
            case QAudioFormat::Int16: {
                const qint16 value = static_cast<qint16>(std::lround(sample * 32767.0F));
                std::memcpy(destination, &value, sizeof(value)); destination += sizeof(value); break;
            }
            case QAudioFormat::Int32: {
                const qint32 value = static_cast<qint32>(std::llround(static_cast<double>(sample) * std::numeric_limits<qint32>::max()));
                std::memcpy(destination, &value, sizeof(value)); destination += sizeof(value); break;
            }
            case QAudioFormat::Float:
                std::memcpy(destination, &sample, sizeof(sample)); destination += sizeof(sample); break;
            case QAudioFormat::Unknown:
            default:
                return {};
            }
        }
    }
    Q_ASSERT(destination == output.constData() + output.size());
    return output;
}
}

struct LocalTtsWorker::Engine {
    explicit Engine(const QString &path) : library(path) {}
    QLibrary library;
    const struct SherpaOnnxOfflineTts *model = nullptr;
    decltype(&SherpaOnnxCreateOfflineTts) create = nullptr;
    decltype(&SherpaOnnxDestroyOfflineTts) destroy = nullptr;
    decltype(&SherpaOnnxOfflineTtsGenerateWithCallbackWithArg) generate = nullptr;
    decltype(&SherpaOnnxDestroyOfflineTtsGeneratedAudio) destroyAudio = nullptr;
    decltype(&SherpaOnnxOfflineTtsSampleRate) sampleRate = nullptr;
    ~Engine() { if (model && destroy) destroy(model); library.unload(); }
};

LocalTtsWorker::LocalTtsWorker(QString runtimeDirectory, QString modelDirectory,
    QString modelFilename, QObject *parent)
    : QObject(parent), m_runtimeDirectory(std::move(runtimeDirectory)),
      m_modelDirectory(std::move(modelDirectory)), m_modelFilename(std::move(modelFilename)) {}

LocalTtsWorker::~LocalTtsWorker() { release(); }

bool LocalTtsWorker::load(QString *const error) {
    Q_ASSERT(error);
    if (m_engine) return true;
    auto engine = std::make_unique<struct Engine>(QDir(m_runtimeDirectory).filePath(QStringLiteral("lib/libsherpa-onnx-c-api.so")));
    if (!engine->library.load()) {
        *error = QStringLiteral("The local speech runtime could not load. Download the speech files again. Details: %1").arg(engine->library.errorString());
        return false;
    }
    engine->create = reinterpret_cast<decltype(engine->create)>(engine->library.resolve("SherpaOnnxCreateOfflineTts"));
    engine->destroy = reinterpret_cast<decltype(engine->destroy)>(engine->library.resolve("SherpaOnnxDestroyOfflineTts"));
    engine->generate = reinterpret_cast<decltype(engine->generate)>(engine->library.resolve("SherpaOnnxOfflineTtsGenerateWithCallbackWithArg"));
    engine->destroyAudio = reinterpret_cast<decltype(engine->destroyAudio)>(engine->library.resolve("SherpaOnnxDestroyOfflineTtsGeneratedAudio"));
    engine->sampleRate = reinterpret_cast<decltype(engine->sampleRate)>(engine->library.resolve("SherpaOnnxOfflineTtsSampleRate"));
    if (!engine->create || !engine->destroy || !engine->generate || !engine->destroyAudio || !engine->sampleRate) {
        *error = QStringLiteral("The downloaded speech runtime has an unsupported interface. Download the speech files again.");
        return false;
    }
    const QDir directory(m_modelDirectory);
    const QByteArray model = QFile::encodeName(directory.filePath(m_modelFilename));
    const QByteArray tokens = QFile::encodeName(directory.filePath(QStringLiteral("tokens.txt")));
    const QByteArray data = QFile::encodeName(directory.filePath(QStringLiteral("espeak-ng-data")));
    struct SherpaOnnxOfflineTtsConfig config{};
    config.model.vits.model = model.constData();
    config.model.vits.tokens = tokens.constData();
    config.model.vits.data_dir = data.constData();
    config.model.vits.noise_scale = 0.667F;
    config.model.vits.noise_scale_w = 0.8F;
    config.model.vits.length_scale = 1.0F;
    config.model.provider = "cpu";
    config.model.num_threads = std::clamp(QThread::idealThreadCount() / 4, 1, 4);
    config.max_num_sentences = 1;
    config.silence_scale = 0.2F;
    engine->model = engine->create(&config);
    if (!engine->model) {
        *error = QStringLiteral("Piper could not load the selected voice. Download the speech files again.");
        return false;
    }
    m_engine = std::move(engine);
    if (!m_idle) {
        m_idle = new QTimer(this);
        m_idle->setSingleShot(true);
        connect(m_idle, &QTimer::timeout, this, &LocalTtsWorker::release);
    }
    return true;
}

void LocalTtsWorker::synthesize(const QString &text, const QAudioFormat &format,
    const quint64 request, const std::shared_ptr<std::atomic_bool> &cancelled) {
    Q_ASSERT(QThread::currentThread() == thread());
    Q_ASSERT(cancelled);
    if (m_shuttingDown || cancelled->load(std::memory_order_relaxed)) return;
    const QString value = text.trimmed();
    if (value.isEmpty() || value.size() > MaximumTextLength || !format.isValid()
        || format.sampleRate() < 8000 || format.sampleRate() > 192000 || format.channelCount() > 8) {
        emit failed(QStringLiteral("Speech needs a nonempty prompt of at most 8192 characters and an available audio output."), request);
        return;
    }
    if (m_idle) m_idle->stop();
    QString error;
    if (!load(&error)) { emit failed(error, request); return; }
    if (cancelled->load(std::memory_order_relaxed)) { m_idle->start(30000); return; }
    const int sampleRate = m_engine->sampleRate(m_engine->model);
    if (sampleRate < 8000 || sampleRate > 48000) {
        emit failed(QStringLiteral("The selected voice has an unsupported sample rate. Choose another local voice."), request);
        m_idle->start(30000);
        return;
    }
    const QByteArray utf8 = boundedSentences(value).toUtf8();
    struct SynthesisLimit limit{cancelled.get(), 0, static_cast<qint64>(sampleRate) * MaximumSeconds, false};
    const struct SherpaOnnxGeneratedAudio *const audio = m_engine->generate(m_engine->model,
        utf8.constData(), 0, 1.0F, continueSynthesis, &limit);
    QByteArray pcm;
    double seconds = 0.0;
    if (audio && audio->samples && audio->sample_rate >= 8000 && audio->sample_rate <= 48000
        && audio->n >= audio->sample_rate / 10 && audio->n <= audio->sample_rate * MaximumSeconds) {
        double energy = 0.0;
        bool valid = true;
        for (int i = 0; i < audio->n; ++i) {
            const float sample = audio->samples[i];
            if (!std::isfinite(sample) || std::abs(sample) > 1.1F) { valid = false; break; }
            energy += static_cast<double>(sample) * sample;
        }
        if (valid && energy / audio->n > 0.000001) {
            seconds = static_cast<double>(audio->n) / audio->sample_rate;
            pcm = playbackPcm(*audio, format, cancelled);
        }
    }
    if (audio) m_engine->destroyAudio(audio);
    m_idle->start(30000);
    if (cancelled->load(std::memory_order_relaxed)) return;
    if (limit.exceeded) {
        emit failed(QStringLiteral("This prompt produces more than 90 seconds of speech. Shorten the card or read it in parts."), request);
        return;
    }
    if (pcm.isEmpty()) {
        emit failed(QStringLiteral("The local voice returned invalid or silent audio. Download the speech files again or select another voice."), request);
        return;
    }
    emit pcmReady(pcm, request, seconds);
}

void LocalTtsWorker::release() { m_engine.reset(); }

void LocalTtsWorker::shutdown() {
    Q_ASSERT(QThread::currentThread() == thread());
    m_shuttingDown = true;
    if (m_idle) m_idle->stop();
    release();
}
