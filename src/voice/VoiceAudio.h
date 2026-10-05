#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QByteArrayView>
#include <QVector>
#include <optional>

// Device samples are converted on the voice thread to the wire format.
class VoicePcmConverter final {
public:
    [[nodiscard]] bool configure(const QAudioFormat &format);
    [[nodiscard]] QByteArray convert(QByteArrayView input);
    void reset();
private:
    QAudioFormat m_format;
    QByteArray m_remainder;
    QVector<float> m_samples;
    double m_position = 0.0;
};

// Silence occupies a bounded pre-roll; an utterance ends after 900 ms of silence.
class VoiceUtteranceBuffer final {
public:
    static constexpr int SampleRate = 16000;
    static constexpr int FrameBytes = 640;
    static constexpr int MaxBytes = SampleRate * 2 * 60;
    [[nodiscard]] std::optional<QByteArray> append(QByteArrayView pcm);
    [[nodiscard]] qsizetype bufferedBytes() const;
    void reset();
private:
    QByteArray m_pending;
    QByteArray m_preRoll;
    QByteArray m_recording;
    int m_voicedFrames = 0;
    int m_silentFrames = 0;
};

[[nodiscard]] QByteArray voiceWav(QByteArrayView pcm);
