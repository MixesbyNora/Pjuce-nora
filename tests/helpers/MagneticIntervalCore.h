#pragma once

#include <experimental/PlayPreisachCore.h>

namespace MagneticOversampling
{
// Offline candidate only. Integrates the unchanged constitutive law over a
// linearly reconstructed H interval, splitting held/moving play branches.
// Endpoint history matches PlayPreisachCore; the output is an interval mean.
class IntervalCore
{
public:
    explicit IntervalCore (MagneticPrototype::PlayPreisachCore::Parameters = {}) noexcept;
    void reset() noexcept;
    double processField (double field) noexcept;
    double getMagnetization() const noexcept { return magnetization; }
    const auto& getPlayStates() const noexcept { return states; }
    static double averageTanh (double from, double to) noexcept;

private:
    static constexpr auto count = MagneticPrototype::Settings::operatorCount;
    MagneticPrototype::PlayPreisachCore shape;
    std::array<double, count> states {}, density {}, primitive {};
    double previousField = 0.0, previousPrimitive = 0.0, magnetization = 0.0;
};
}
