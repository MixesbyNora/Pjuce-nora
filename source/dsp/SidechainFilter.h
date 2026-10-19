#pragma once

#include "SidechainFilterSettings.h"

class SidechainFilter
{
public:
    struct Coefficients
    {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
    };
    using ModeCoefficients = std::array<Coefficients, SidechainFilterSettings::maxStages>;

    void prepare (double sampleRate) noexcept;
    void setMode (int mode) noexcept;
    double processSample (int channel, double sample) noexcept;

    // Read-only access for measuring the prepared transfer functions.
    const ModeCoefficients& getCoefficients (int mode) const noexcept;

private:
    struct State
    {
        double z1 = 0.0;
        double z2 = 0.0;
    };

    static int boundedMode (int mode) noexcept;
    std::array<ModeCoefficients, SidechainFilterSettings::modes.size()> coefficients {};
    std::array<std::array<State, SidechainFilterSettings::maxStages>, 2> state {};
    int activeMode = SidechainFilterSettings::defaultMode;
};
