#include "VoiceAudio.h"

#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr int kPreRollBytes = VoiceUtteranceBuffer::FrameBytes * 10;
constexpr int kSilentFrames = 45;
constexpr int kMinVoicedFrames = 4;
constexpr int kSpeechThreshold = 450;
static_assert(VoiceUtteranceBuffer::MaxBytes % VoiceUtteranceBuffer::FrameBytes == 0);
static_assert(sizeof(qint16) == 2 && sizeof(float) == 4);

template <typename T> T nativeSample(const char *const data) {
    T value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

float sampleValue(const char *const data, const QAudioFormat::SampleFormat format) {
    switch (format) {
    case QAudioFormat::UInt8:
        return (static_cast<unsigned char>(*data) - 128) / 128.0F;
    case QAudioFormat::Int16:
        return nativeSample<qint16>(data) / 32768.0F;
    case QAudioFormat::Int32:
        return static_cast<float>(nativeSample<qint32>(data) / 2147483648.0);
    case QAudioFormat::Float: {
        const float value = nativeSample<float>(data);
        return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
    }
    case QAudioFormat::Unknown:
    default:
        Q_UNREACHABLE();
    }
    Q_UNREACHABLE();
}
}

bool VoicePcmConverter::configure(const QAudioFormat &format) {
    reset();
    m_format = {};
    if (!format.isValid() || format.sampleRate() < 8000 || format.sampleRate() > 192000
        || format.channelCount() < 1 || format.channelCount() > 8) return false;
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
    case QAudioFormat::Int16:
    case QAudioFormat::Int32:
    case QAudioFormat::Float:
        m_format = format;
        return true;
    case QAudioFormat::Unknown:
    default:
        return false;
    }
    return false;
}

QByteArray VoicePcmConverter::convert(const QByteArrayView input) {
    Q_ASSERT(m_format.isValid());
    QByteArray bytes = std::move(m_remainder);
    bytes.append(input.data(), input.size());
    const int frameSize = m_format.bytesPerFrame();
    const int sampleSize = m_format.bytesPerSample();
    Q_ASSERT(frameSize > 0 && sampleSize > 0);
    const qsizetype count = bytes.size() / frameSize;
    m_samples.reserve(m_samples.size() + count);
    for (qsizetype frame = 0; frame < count; ++frame) {
        float mono = 0.0F;
        const char *const start = bytes.constData() + frame * frameSize;
        for (int channel = 0; channel < m_format.channelCount(); ++channel)
            mono += sampleValue(start + channel * sampleSize, m_format.sampleFormat());
        m_samples.append(mono / m_format.channelCount());
    }
    m_remainder = bytes.mid(count * frameSize);
    Q_ASSERT(m_remainder.size() < frameSize);
    const double step = static_cast<double>(m_format.sampleRate()) / VoiceUtteranceBuffer::SampleRate;
    QByteArray result;
    result.reserve(static_cast<qsizetype>(m_samples.size() / step + 1) * 2);
    while (m_position + 1 < m_samples.size()) {
        const qsizetype index = static_cast<qsizetype>(m_position);
        const float fraction = static_cast<float>(m_position - index);
        const float sample = m_samples.at(index) * (1 - fraction) + m_samples.at(index + 1) * fraction;
        const qint16 value = static_cast<qint16>(std::clamp(std::lround(sample * 32768.0F), -32768L, 32767L));
        const qint16 little = qToLittleEndian(value);
        result.append(reinterpret_cast<const char *>(&little), sizeof(little));
        m_position += step;
    }
    const qsizetype consumed = std::min(static_cast<qsizetype>(m_position), std::max(qsizetype(0), m_samples.size() - 1));
    m_samples.remove(0, consumed);
    m_position -= consumed;
    Q_ASSERT(m_position >= 0.0 && m_samples.size() <= 25);
    return result;
}

void VoicePcmConverter::reset() {
    m_remainder.clear();
    m_samples.clear();
    m_position = 0.0;
}

std::optional<QByteArray> VoiceUtteranceBuffer::append(const QByteArrayView pcm) {
    m_pending.append(pcm.data(), pcm.size());
    qsizetype consumed = 0;
    while (m_pending.size() - consumed >= FrameBytes) {
        const QByteArrayView frame(m_pending.constData() + consumed, FrameBytes);
        consumed += FrameBytes;
        qint64 energy = 0;
        for (qsizetype i = 0; i < frame.size(); i += 2)
            energy += qAbs(static_cast<int>(qFromLittleEndian<qint16>(frame.data() + i)));
        const bool voiced = energy / (FrameBytes / 2) > kSpeechThreshold;
        if (m_recording.isEmpty() && !voiced) {
            m_preRoll.append(frame.data(), frame.size());
            if (m_preRoll.size() > kPreRollBytes) m_preRoll.remove(0, m_preRoll.size() - kPreRollBytes);
            continue;
        }
        if (m_recording.isEmpty()) {
            m_recording = std::move(m_preRoll);
            m_preRoll.clear();
        }
        m_recording.append(frame.data(), frame.size());
        if (voiced) { ++m_voicedFrames; m_silentFrames = 0; }
        else ++m_silentFrames;
        Q_ASSERT(m_recording.size() <= MaxBytes);
        if (m_silentFrames >= kSilentFrames || m_recording.size() == MaxBytes) {
            QByteArray utterance = std::move(m_recording);
            const bool hasSpeech = m_voicedFrames >= kMinVoicedFrames;
            reset();
            if (hasSpeech) return utterance;
            return std::nullopt;
        }
    }
    m_pending.remove(0, consumed);
    Q_ASSERT(m_pending.size() < FrameBytes);
    return std::nullopt;
}

qsizetype VoiceUtteranceBuffer::bufferedBytes() const {
    return m_pending.size() + m_preRoll.size() + m_recording.size();
}

void VoiceUtteranceBuffer::reset() {
    m_pending.clear();
    m_preRoll.clear();
    m_recording.clear();
    m_voicedFrames = 0;
    m_silentFrames = 0;
}

QByteArray voiceWav(const QByteArrayView pcm) {
    Q_ASSERT(pcm.size() % 2 == 0 && pcm.size() <= VoiceUtteranceBuffer::MaxBytes);
    QByteArray result;
    result.reserve(44 + pcm.size());
    const auto append32 = [&result](const quint32 value) {
        const quint32 little = qToLittleEndian(value);
        result.append(reinterpret_cast<const char *>(&little), sizeof(little));
    };
    const auto append16 = [&result](const quint16 value) {
        const quint16 little = qToLittleEndian(value);
        result.append(reinterpret_cast<const char *>(&little), sizeof(little));
    };
    result.append("RIFF", 4); append32(36 + static_cast<quint32>(pcm.size()));
    result.append("WAVEfmt ", 8); append32(16); append16(1); append16(1);
    append32(VoiceUtteranceBuffer::SampleRate); append32(VoiceUtteranceBuffer::SampleRate * 2);
    append16(2); append16(16); result.append("data", 4); append32(static_cast<quint32>(pcm.size()));
    result.append(pcm.data(), pcm.size());
    Q_ASSERT(result.size() == pcm.size() + 44);
    return result;
}
