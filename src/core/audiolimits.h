#pragma once

#include <QtGlobal>
#include <limits>

namespace betterflash::audio {
inline constexpr int CanonicalSampleRate = 16000;
inline constexpr int CanonicalChannels = 1;
inline constexpr int CanonicalBytesPerSample = 2;
inline constexpr int NarrationMaxSeconds = 5 * 60;
inline constexpr qsizetype NarrationMaxPcmBytes = static_cast<qsizetype>(CanonicalSampleRate)
    * CanonicalChannels * CanonicalBytesPerSample * NarrationMaxSeconds;
inline constexpr qsizetype WavHeaderBytes = 44;
inline constexpr qsizetype MaxConversionBytes = 64 * 1024 * 1024;

static_assert(NarrationMaxPcmBytes == 9'600'000);
static_assert(NarrationMaxPcmBytes + WavHeaderBytes <= 20 * 1024 * 1024);
static_assert(static_cast<quint64>(WavHeaderBytes + NarrationMaxPcmBytes) <= std::numeric_limits<quint32>::max());
}
