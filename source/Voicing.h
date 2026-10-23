#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <initializer_list>

// Workbench voicing schemas. A schema is a struct with `descriptors` (an array of
// Descriptor), `version`, `model` and `retiredModels`. Values are stored as step
// indices so two slots and four flags publish through one lock-free 64-bit word.
namespace Voicing
{
struct Descriptor
{
    const char* id;
    const char* label;
    const char* unit;
    double minimum, maximum, step, defaultValue, skew;
};
using Word = std::uint64_t;
constexpr Word steps (const Descriptor& d) noexcept
{
    return static_cast<Word> ((d.maximum - d.minimum) / d.step + 0.5);
}
constexpr unsigned bits (const Descriptor& d) noexcept { return static_cast<unsigned> (std::bit_width (steps (d))); }
template <typename Schema>
inline constexpr unsigned valueBits = []
{
    unsigned total = 0;
    for (const auto& d : Schema::descriptors) total += bits (d);
    return total;
}();
template <typename Schema>
inline constexpr Word valueMask = (Word { 1 } << valueBits<Schema>) - 1;

template <typename Schema>
struct Values
{
    static constexpr std::size_t size = Schema::descriptors.size();
    // Both slots and four flags must fit in one atomic. Refuse silent truncation
    // if a schema outgrows this publication format.
    static_assert (valueBits<Schema> > 0 && 2 * valueBits<Schema> + 4 <= 64);
    std::array<float, size> data = []
    {
        std::array<float, size> result {};
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = static_cast<float> (Schema::descriptors[i].defaultValue);
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
template <typename Schema>
double dspValue (Values<Schema> values, std::size_t i) noexcept
{
    // The public snapshot uses float; retain the original double constants at
    // defaults so the default engine remains bit-exact.
    const auto& d = Schema::descriptors[i];
    return std::bit_cast<std::uint32_t> (values[i]) == std::bit_cast<std::uint32_t> (static_cast<float> (d.defaultValue))
        ? d.defaultValue : static_cast<double> (values[i]);
}
inline float bounded (float value, const Descriptor& d) noexcept
{
    if ((std::bit_cast<std::uint32_t> (value) & 0x7f800000U) == 0x7f800000U)
        return static_cast<float> (d.defaultValue);
    return static_cast<float> (std::clamp (static_cast<double> (value), d.minimum, d.maximum));
}
template <typename Schema>
Word encode (Values<Schema> values) noexcept
{
    Word word = 0;
    unsigned offset = 0;
    for (std::size_t i = 0; i < Schema::descriptors.size(); ++i)
    {
        const auto& d = Schema::descriptors[i];
        const auto index = std::min (steps (d), static_cast<Word> (std::round ((bounded (values[i], d) - d.minimum) / d.step)));
        word |= index << offset;
        offset += bits (d);
    }
    return word;
}
template <typename Schema>
Values<Schema> decode (Word word) noexcept
{
    Values<Schema> result;
    unsigned offset = 0;
    for (std::size_t i = 0; i < Schema::descriptors.size(); ++i)
    {
        const auto& d = Schema::descriptors[i];
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
template <typename Schema>
Values<Schema> sanitized (Values<Schema> values) noexcept { return decode<Schema> (encode<Schema> (values)); }
template <typename Schema>
bool equal (Values<Schema> a, Values<Schema> b) noexcept { return encode<Schema> (a) == encode<Schema> (b); }
}
