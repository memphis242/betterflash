#include <whisper.h>
#include <ggml-backend.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace {
bool sufficientGpuMemory(const QString &modelPath) {
    const quint64 required = static_cast<quint64>(QFileInfo(modelPath).size()) + 512ULL * 1024ULL * 1024ULL;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        const ggml_backend_dev_t device = ggml_backend_dev_get(i);
        if (!device) continue;
        const enum ggml_backend_dev_type type = ggml_backend_dev_type(device);
        if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU) continue;
        size_t freeMemory = 0, totalMemory = 0;
        ggml_backend_dev_memory(device, &freeMemory, &totalMemory);
        if (freeMemory >= required) return true;
    }
    return false;
}

struct Wav { QVector<float> samples; };
bool readWav(const QString &path, struct Wav *const result) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    constexpr qsizetype maxWavBytes = 16000 * 2 * 60 + 44;
    if (QFileInfo(file).size() > maxWavBytes) return false;
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44 || bytes.mid(0, 4) != QByteArrayLiteral("RIFF") || bytes.mid(8, 4) != QByteArrayLiteral("WAVE")) return false;
    int offset = 12; quint16 format = 0, channels = 0, bits = 0; quint32 rate = 0; int dataOffset = -1; quint32 dataBytes = 0;
    while (offset + 8 <= bytes.size()) {
        const QByteArray id = bytes.mid(offset, 4);
        const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + offset + 4)); offset += 8;
        if (size > static_cast<quint32>(bytes.size() - offset)) return false;
        if (id == QByteArrayLiteral("fmt ") && size >= 16) { format = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(bytes.constData() + offset)); channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(bytes.constData() + offset + 2)); rate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData() + offset + 4)); bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(bytes.constData() + offset + 14)); }
        if (id == QByteArrayLiteral("data")) { dataOffset = offset; dataBytes = size; break; }
        offset += static_cast<int>(size) + (size & 1U);
    }
    if (format != 1 || channels != 1 || bits != 16 || rate != 16000 || dataOffset < 0 || dataBytes == 0 || dataBytes % 2 != 0) return false;
    result->samples.reserve(static_cast<qsizetype>(dataBytes / 2));
    for (quint32 i = 0; i < dataBytes; i += 2) result->samples.append(qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(bytes.constData() + dataOffset + i)) / 32768.0F);
    return !result->samples.isEmpty();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) return 2;
    const QByteArray requestBytes = input.read(65537);
    QTextStream output(stdout);
    if (requestBytes.size() > 65536) { output << QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("Transcription request is too large.")}}).toJson(QJsonDocument::Compact) << Qt::endl; return 2; }
    const QJsonDocument request = QJsonDocument::fromJson(requestBytes.trimmed());
    const QJsonObject object = request.object();
    const QString modelPath = object.value(QStringLiteral("modelPath")).toString();
    const QString audioPath = object.value(QStringLiteral("audioPath")).toString();
    if (modelPath.isEmpty() || audioPath.isEmpty()) { output << QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("Invalid transcription request.")}}).toJson(QJsonDocument::Compact) << Qt::endl; return 2; }
    struct Wav wav;
    if (!readWav(audioPath, &wav)) { output << QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("Unsupported WAV input.")}}).toJson(QJsonDocument::Compact) << Qt::endl; return 3; }
    const QByteArray vocabulary = object.value(QStringLiteral("vocabulary")).toString().left(4096).toUtf8();
    QString transcript;
    const QString acceleration = object.value(QStringLiteral("acceleration")).toString().toLower();
    const auto run = [&modelPath, &wav, &vocabulary, &transcript](const bool useGpu) {
        struct whisper_context_params contextParams = whisper_context_default_params();
        contextParams.use_gpu = useGpu;
        struct whisper_context *const context = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), contextParams);
        if (!context) return false;
        struct whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
        params.n_threads = 8;
        params.beam_search.beam_size = 5;
        params.print_progress = false; params.print_realtime = false; params.print_timestamps = false;
        params.translate = false; params.language = "en"; params.no_context = true; params.no_timestamps = true; params.single_segment = false;
        params.initial_prompt = vocabulary.isEmpty() ? nullptr : vocabulary.constData();
        const int result = whisper_full(context, params, wav.samples.constData(), static_cast<int>(wav.samples.size()));
        transcript.clear();
        if (result == 0) for (int i = 0; i < whisper_full_n_segments(context); ++i) transcript += QString::fromUtf8(whisper_full_get_segment_text(context, i));
        whisper_free(context);
        return result == 0 && !transcript.trimmed().isEmpty();
    };
    const bool automatic = acceleration != QStringLiteral("cpu");
    const bool useGpu = automatic && sufficientGpuMemory(modelPath);
    bool succeeded = run(useGpu);
    if (!succeeded && useGpu) succeeded = run(false);
    transcript = transcript.simplified();
    if (!succeeded || transcript.isEmpty()) { output << QJsonDocument(QJsonObject{{QStringLiteral("error"), QStringLiteral("Whisper could not transcribe the recording on the selected accelerator or CPU fallback.")}}).toJson(QJsonDocument::Compact) << Qt::endl; return 5; }
    output << QJsonDocument(QJsonObject{{QStringLiteral("transcript"), transcript}}).toJson(QJsonDocument::Compact) << Qt::endl;
    return 0;
}
