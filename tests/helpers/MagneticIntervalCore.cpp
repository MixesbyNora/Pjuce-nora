#include "MagneticIntervalCore.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace MagneticOversampling
{
namespace
{
bool finite (double value) noexcept
{
    return (std::bit_cast<std::uint64_t> (value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
double logCosh (double value) noexcept
{
    const auto magnitude = std::abs (value);
    // Stable at zero, and no exp overflow at large fields.
    if (magnitude < 1.0)
    {
        const auto halfSinh = std::sinh (value * 0.5);
        return std::log1p (2.0 * halfSinh * halfSinh);
    }
    return magnitude + std::log1p (std::exp (-2.0 * magnitude)) - 0.69314718055994530942;
}
double mean (double from, double to, double primitiveFrom, double primitiveTo) noexcept
{
    const auto delta = to - from;
    if (std::abs (delta) < 1.0e-5)
        return std::tanh (0.5 * (from + to));
    return (primitiveTo - primitiveFrom) / delta;
}
}

IntervalCore::IntervalCore (MagneticPrototype::PlayPreisachCore::Parameters parameters) noexcept : shape (parameters) { reset(); }
void IntervalCore::reset() noexcept
{
    states.fill (0.0);
    density.fill (0.0);
    primitive.fill (0.0);
    previousField = previousPrimitive = magnetization = 0.0;
}
double IntervalCore::averageTanh (double from, double to) noexcept
{
    return mean (from, to, logCosh (from), logCosh (to));
}
double IntervalCore::processField (double field) noexcept
{
    using namespace MagneticPrototype;
    if (! finite (field))
        return magnetization;
    field = std::clamp (field, -Settings::maximumField, Settings::maximumField);
    const auto delta = field - previousField;
    const auto nextPrimitive = logCosh (field / Settings::saturationField);
    const auto reversible = mean (previousField / Settings::saturationField, field / Settings::saturationField,
        previousPrimitive, nextPrimitive);
    auto irreversible = 0.0;
    auto endpointIrreversible = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto old = states[i];
        const auto next = std::clamp (old, field - shape.getRadii()[i], field + shape.getRadii()[i]);
        auto average = density[i];
        if (next > old || next < old)
        {
            const auto nextPrimitiveState = logCosh (next / Settings::saturationField);
            const auto activeFraction = std::clamp ((next - old) / delta, 0.0, 1.0);
            average += activeFraction * (mean (old / Settings::saturationField, next / Settings::saturationField,
                primitive[i], nextPrimitiveState) - density[i]);
            density[i] = std::tanh (next / Settings::saturationField);
            primitive[i] = nextPrimitiveState;
            states[i] = next;
        }
        irreversible += shape.getWeights()[i] * average;
        endpointIrreversible += shape.getWeights()[i] * density[i];
    }
    const auto memory = shape.getParameters().memory;
    magnetization = std::clamp ((1.0 - memory) * std::tanh (field / Settings::saturationField)
        + memory * endpointIrreversible, -1.0, 1.0);
    previousField = field;
    previousPrimitive = nextPrimitive;
    return std::clamp ((1.0 - memory) * reversible + memory * irreversible, -1.0, 1.0);
}
}
