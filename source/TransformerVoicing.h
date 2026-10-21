#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>

namespace TransformerVoicing
{
inline constexpr int version = 4;
inline constexpr const char* model = "lumiere-voicing-workbench-v4";
inline constexpr const char* legacyWorkbenchModel = "lumiere-voicing-workbench-v3";
inline constexpr const char* legacyModel = "transformer-tanh-residual-adaa-v1";
inline constexpr const char* legacyMagneticModel = "transformer-magnetic-play-v2";
struct Descriptor
{
    const char* id;
    const char* label;
    const char* unit;
    double minimum, maximum, step, defaultValue, skew;
};
// The workbench schema is independent of host automation and DSP implementation.
// Drive stays first. New controls require an explicit DSP/state migration decision.
inline constexpr std::array descriptors {
    Descriptor { "driveCalibrationDb", "Drive calibration", "dB", -12.0, 12.0, 0.1, 0.0, 1.0 },
    Descriptor { "magnetizationWeight", "Magnetization weight k", "", 0.0, 16.0, 0.1, 4.0, 1.0 },
    Descriptor { "memory", "Memory", "", 0.0, 1.0, 0.01, 0.35, 1.0 },
    Descriptor { "magneticCornerHz", "Magnetic corner", "Hz", 0.5, 30.0, 0.5, 5.0, 0.5 }
};
inline constexpr std::size_t driveCalibration = 0;
inline constexpr std::size_t magnetizationWeight = 1, memory = 2, magneticCorner = 3;
using Word = std::uint64_t;
constexpr Word steps (const Descriptor& d) noexcept
{
    return static_cast<Word> ((d.maximum - d.minimum) / d.step + 0.5);
}
constexpr unsigned bits (const Descriptor& d) noexcept { return static_cast<unsigned> (std::bit_width (steps (d))); }
inline constexpr unsigned valueBits = []
{
    unsigned total = 0;
    for (const auto& d : descriptors) total += bits (d);
    return total;
}();
// Both slots and four flags currently fit in one atomic. Refuse silent truncation
// if a later schema outgrows this publication format.
static_assert (valueBits > 0 && 2 * valueBits + 4 <= 64);
inline constexpr Word valueMask = (Word { 1 } << valueBits) - 1;
struct Values
{
    std::array<float, descriptors.size()> data = []
    {
        std::array<float, descriptors.size()> result {};
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = static_cast<float> (descriptors[i].defaultValue);
        return result;
    }();
    Values() noexcept = default;
    // Partial research/test values retain descriptor defaults for other fields.
    Values (std::initializer_list<float> initial) noexcept
    {
        std::copy_n (initial.begin(), std::min (initial.size(), data.size()), data.begin());
    }
    float& operator[] (std::size_t i) noexcept { return data[i]; }
    float operator[] (std::size_t i) const noexcept { return data[i]; }
};
inline double dspValue (Values values, std::size_t i) noexcept
{
    // The public snapshot uses float; retain the original double constants at
    // defaults (notably memory=0.35) so the default engine remains bit-exact.
    return std::bit_cast<std::uint32_t> (values[i]) == std::bit_cast<std::uint32_t> (static_cast<float> (descriptors[i].defaultValue))
        ? descriptors[i].defaultValue : static_cast<double> (values[i]);
}
inline float bounded (float value, const Descriptor& d) noexcept
{
    if ((std::bit_cast<std::uint32_t> (value) & 0x7f800000U) == 0x7f800000U)
        return static_cast<float> (d.defaultValue);
    return static_cast<float> (std::clamp (static_cast<double> (value), d.minimum, d.maximum));
}
inline Word encode (Values values) noexcept
{
    Word word = 0;
    unsigned offset = 0;
    for (std::size_t i = 0; i < descriptors.size(); ++i)
    {
        const auto& d = descriptors[i];
        const auto index = std::min (steps (d), static_cast<Word> (std::round ((bounded (values[i], d) - d.minimum) / d.step)));
        word |= index << offset;
        offset += bits (d);
    }
    return word;
}
inline Values decode (Word word) noexcept
{
    Values result;
    unsigned offset = 0;
    for (std::size_t i = 0; i < descriptors.size(); ++i)
    {
        const auto& d = descriptors[i];
        const auto index = std::min (steps (d), (word >> offset) & ((Word { 1 } << bits (d)) - 1));
        // Add integral step offsets before multiplying, retaining exact 0 dB.
        const auto origin = std::round (d.minimum / d.step);
        const auto value = std::abs (origin * d.step - d.minimum) < 1.0e-10
            ? (origin + static_cast<double> (index)) * d.step : d.minimum + static_cast<double> (index) * d.step;
        result[i] = static_cast<float> (value);
        offset += bits (d);
    }
    return result;
}
inline Values sanitized (Values values) noexcept { return decode (encode (values)); }
inline bool equal (Values a, Values b) noexcept { return encode (a) == encode (b); }
}
