#include "PlayPreisachCore.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace MagneticPrototype
{
namespace
{
bool finiteValue (double value) noexcept
{
    static_assert (std::numeric_limits<double>::is_iec559);
    // Do not rely on isfinite being preserved by the project's fast-math flags.
    return (std::bit_cast<std::uint64_t> (value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
double boundedParameter (double value, double low, double high, double fallback) noexcept
{
    return finiteValue (value) ? std::clamp (value, low, high) : fallback;
}
}

PlayPreisachCore::PlayPreisachCore() noexcept { prepare (Parameters {}); }
PlayPreisachCore::PlayPreisachCore (Parameters values) noexcept { prepare (values); }

void PlayPreisachCore::prepare (Parameters values) noexcept
{
    parameters.memory = boundedParameter (values.memory, Settings::minimumMemory, Settings::maximumMemory, Settings::defaultMemory);
    parameters.width = boundedParameter (values.width, Settings::minimumWidth, Settings::maximumWidth, Settings::defaultWidth);
    double totalWeight = 0.0;
    for (std::size_t i = 0; i < Settings::operatorCount; ++i)
    {
        const auto position = (static_cast<double> (i) + 0.5) / static_cast<double> (Settings::operatorCount);
        const auto radius = Settings::minimumRadius * std::pow (Settings::maximumRadius / Settings::minimumRadius, position);
        radii[i] = radius * parameters.width;
        weights[i] = std::exp (-radius / Settings::weightFalloff);
        totalWeight += weights[i];
    }
    for (auto& weight : weights)
        weight /= totalWeight;
    reset();
}

void PlayPreisachCore::reset() noexcept
{
    states.fill (0.0);
    magnetization = 0.0;
}

double PlayPreisachCore::processField (double field) noexcept
{
    if (! finiteValue (field))
        return magnetization; // Ignore a corrupt sample without poisoning valid history.
    field = std::clamp (field, -Settings::maximumField, Settings::maximumField);
    double irreversible = 0.0;
    for (std::size_t i = 0; i < Settings::operatorCount; ++i)
    {
        // Continuous play update, exact on each monotone field segment.
        states[i] = std::clamp (states[i], field - radii[i], field + radii[i]);
        // A monotone smooth density in the play-coordinate Preisach formulation.
        irreversible += weights[i] * std::tanh (states[i] / Settings::saturationField);
    }
    const auto reversible = std::tanh (field / Settings::saturationField);
    magnetization = std::clamp ((1.0 - parameters.memory) * reversible + parameters.memory * irreversible, -1.0, 1.0);
    return magnetization;
}
}
