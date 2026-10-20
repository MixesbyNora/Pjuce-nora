#pragma once

#include <experimental/PlayPreisachCore.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace MagneticOversampling
{
// Offline candidate: reconstruct each interval with five actual field samples.
// Two internal samples of lookahead improve extremum heights without changing
// the constitutive law or subtracting retained magnetization. Not bandlimited.
class QuarticCore
{
public:
    explicit QuarticCore (MagneticPrototype::PlayPreisachCore::Parameters parameters = {}) noexcept : core (parameters) {}
    void reset() noexcept { core.reset(); history.fill (0.0); count = 0; }
    double processField (double field) noexcept
    {
        if ((std::bit_cast<std::uint64_t> (field) & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL)
            return core.getMagnetization();
        field = std::clamp (field, -MagneticPrototype::Settings::maximumField, MagneticPrototype::Settings::maximumField);
        for (std::size_t i = 0; i < history.size() - 1; ++i)
            history[i] = history[i + 1];
        history.back() = field;
        count = std::min (count + 1, 5);
        if (count < 3)
            return 0.0;
        if (count == 5)
        {
            const auto a = (history[0] - 4.0 * history[1] + 6.0 * history[2] - 4.0 * history[3] + history[4]) / 24.0;
            const auto b = (-history[0] + 2.0 * history[1] - 2.0 * history[3] + history[4]) / 12.0;
            const auto c = (-history[0] + 16.0 * history[1] - 30.0 * history[2] + 16.0 * history[3] - history[4]) / 24.0;
            const auto d = (history[0] - 8.0 * history[1] + 8.0 * history[3] - history[4]) / 12.0;
            const auto derivative = [=] (double x) { return ((4.0 * a * x + 3.0 * b) * x + 2.0 * c) * x + d; };
            auto lo = -1.0, hi = 0.0;
            const auto left = derivative (lo), right = derivative (hi);
            if ((left < 0.0 && right > 0.0) || (left > 0.0 && right < 0.0))
            {
                // Fixed work; one extremum per oversampled interval is assumed.
                for (int iteration = 0; iteration < 20; ++iteration)
                {
                    const auto middle = 0.5 * (lo + hi);
                    if ((derivative (middle) < 0.0) == (left < 0.0))
                        lo = middle;
                    else
                        hi = middle;
                }
                const auto x = 0.5 * (lo + hi);
                core.processField ((((a * x + b) * x + c) * x + d) * x + history[2]);
            }
        }
        return core.processField (history[2]);
    }
    double getMagnetization() const noexcept { return core.getMagnetization(); }

private:
    MagneticPrototype::PlayPreisachCore core;
    std::array<double, 5> history {};
    int count = 0;
};
}
