#pragma once

#include "dsp/TransformerSettings.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace TransformerVoicing
{
static_assert ((TransformerSettings::calibrationMaximumDb - TransformerSettings::calibrationMinimumDb) * 10.0f <= 255.0f);
static_assert ((TransformerSettings::emphasisMaximumDb - TransformerSettings::emphasisMinimumDb) * 10.0f <= 255.0f);
static_assert (TransformerSettings::cornerMaximumHz - TransformerSettings::cornerMinimumHz <= 511.0f);
inline constexpr int version = 1;
inline constexpr const char* model = "transformer-tanh-residual-adaa-v1";
struct Values
{
    float driveCalibrationDb = 0.0f;
    float emphasisDb = static_cast<float> (TransformerSettings::shelfGainDb);
    float cornerHz = static_cast<float> (TransformerSettings::shelfFrequencyHz);
};

inline float bounded (float value, float minimum, float maximum, float fallback) noexcept
{
    if ((std::bit_cast<std::uint32_t> (value) & 0x7f800000U) == 0x7f800000U)
        return fallback;
    return std::clamp (value, minimum, maximum);
}

// 25 bits per snapshot, at the controls' displayed resolution. One lock-free
// atomic can publish both snapshots, their selection and audition flags together.
inline std::uint32_t encode (Values values) noexcept
{
    using namespace TransformerSettings;
    const auto drive = static_cast<std::uint32_t> (std::round ((bounded (values.driveCalibrationDb,
        calibrationMinimumDb, calibrationMaximumDb, 0.0f) - calibrationMinimumDb) * 10.0f));
    const auto gain = static_cast<std::uint32_t> (std::round (bounded (values.emphasisDb,
        emphasisMinimumDb, emphasisMaximumDb, static_cast<float> (shelfGainDb)) * 10.0f - emphasisMinimumDb * 10.0f));
    const auto frequency = static_cast<std::uint32_t> (std::round (bounded (values.cornerHz,
        cornerMinimumHz, cornerMaximumHz, static_cast<float> (shelfFrequencyHz)) - cornerMinimumHz));
    return drive | (gain << 8) | (frequency << 16);
}

inline Values decode (std::uint32_t word) noexcept
{
    // Add the offset in integer tenths first: an FMA of index*0.1-12
    // otherwise makes the nominal 0 dB decode to a tiny nonzero offset.
    constexpr auto minimumTenths = static_cast<int> (TransformerSettings::calibrationMinimumDb * 10.0f);
    return { static_cast<float> (static_cast<int> (word & 0xffU) + minimumTenths) * 0.1f,
        static_cast<float> ((word >> 8) & 0xffU) * 0.1f + TransformerSettings::emphasisMinimumDb,
        static_cast<float> ((word >> 16) & 0x1ffU) + TransformerSettings::cornerMinimumHz };
}

inline Values sanitized (Values values) noexcept { return decode (encode (values)); }
inline bool equal (Values a, Values b) noexcept { return encode (a) == encode (b); }
}
