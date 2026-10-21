#pragma once

#include "MagneticCoreSettings.h"
#include <array>
#include <cmath>

namespace MagneticPrototype
{
// Preparation-only table construction; the hot path uses a fifth-order local
// Taylor polynomial of the same constitutive tanh, with double precision.
class ConstitutiveLookup
{
public:
    static constexpr int segments = 8192;
    static constexpr double spacing = Settings::maximumField / segments;
    using Polynomial = std::array<double, 6>;
    using Table = std::array<Polynomial, segments>;
    ConstitutiveLookup() noexcept;
    double value (double x) const noexcept
    {
        const auto magnitude = std::abs (x);
        if (! (magnitude > 0.0))
            return x;
        if (magnitude >= Settings::maximumField)
            return std::copysign (1.0, x);
        const auto index = static_cast<int> (magnitude / spacing);
        const auto delta = magnitude - (static_cast<double> (index) + 0.5) * spacing;
        const auto& p = (*table)[static_cast<std::size_t> (index)];
        return std::copysign (((((p[5] * delta + p[4]) * delta + p[3]) * delta + p[2]) * delta + p[1]) * delta + p[0], x);
    }
private:
    const Table* table;
};
}
