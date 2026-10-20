#pragma once

#include "MagneticCoreSettings.h"
#include <array>

namespace MagneticPrototype
{
// Offline experimental constitutive law: dimensionless H -> dimensionless M.
// No voltage/flux integration, circuit loading, audio-path hookup or sample rate.
class PlayPreisachCore
{
public:
    struct Parameters
    {
        double memory = Settings::defaultMemory;
        double width = Settings::defaultWidth;
    };
    PlayPreisachCore() noexcept;
    explicit PlayPreisachCore (Parameters) noexcept;
    void prepare (Parameters) noexcept; // Bounds parameters, prepares weights, resets history.
    void reset() noexcept; // Demagnetized initial condition; silence does not reset remanence.
    double processField (double field) noexcept;

    Parameters getParameters() const noexcept { return parameters; }
    double getMagnetization() const noexcept { return magnetization; }
    const auto& getRadii() const noexcept { return radii; }
    const auto& getWeights() const noexcept { return weights; }
    const auto& getPlayStates() const noexcept { return states; }
    static constexpr std::size_t maximumPlayUpdates = Settings::operatorCount;
    static constexpr std::size_t maximumTanhCalls = Settings::operatorCount + 1;

private:
    Parameters parameters;
    std::array<double, Settings::operatorCount> radii {}, weights {}, states {};
    double magnetization = 0.0;
};
}
