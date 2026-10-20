#pragma once

#include "TransformerSettings.h"
#include <cmath>

// Stateless first-order ADAA primitive. The caller owns the previous driven
// sample, keeping channels independent and allowing an undelayed linear path.
struct TanhADAA
{
    static double antiderivative (double x) noexcept
    {
        constexpr auto logTwo = 0.69314718055994530942;
        const auto magnitude = std::abs (x);
        return magnitude + std::log1p (std::exp (-2.0 * magnitude)) - logTwo;
    }

    static double evaluate (double current, double previous) noexcept
    {
        const auto average = current * 0.5 + previous * 0.5;
        const auto halfDifference = current * 0.5 - previous * 0.5;
        if (std::abs (halfDifference) < TransformerSettings::adaaDifferenceThreshold * 0.5)
            return std::tanh (average);
        // Halving before subtracting also handles opposite near-DBL_MAX
        // inputs without overflowing either difference.
        return (antiderivative (current) * 0.5 - antiderivative (previous) * 0.5) / halfDifference;
    }
};
