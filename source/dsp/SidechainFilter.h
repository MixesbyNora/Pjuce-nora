#pragma once

#include "SidechainFilterSettings.h"
#include "Biquad.h"

class SidechainFilter
{
public:
    using Coefficients = DoubleBiquad::Coefficients;
    using ModeCoefficients = std::array<Coefficients, SidechainFilterSettings::maxStages>;

    void prepare (double sampleRate) noexcept;
    void setMode (int mode) noexcept;
    double processSample (int channel, double sample) noexcept;

    // Read-only access for measuring the prepared transfer functions.
    const ModeCoefficients& getCoefficients (int mode) const noexcept;

private:
    using State = DoubleBiquad::State;

    static int boundedMode (int mode) noexcept;
    std::array<ModeCoefficients, SidechainFilterSettings::modes.size()> coefficients {};
    std::array<std::array<State, SidechainFilterSettings::maxStages>, 2> state {};
    int activeMode = SidechainFilterSettings::defaultMode;
};
