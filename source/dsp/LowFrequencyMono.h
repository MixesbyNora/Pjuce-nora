#pragma once

#include "../BaParameters.h"
#include "Biquad.h"

class LowFrequencyMono
{
public:
    void prepare (double sampleRate, float initialHz) noexcept;
    void setCutoff (float hertz) noexcept;
    void process (float& left, float& right) noexcept;

private:
    void updateCoefficients (double hertz) noexcept;
    double rate = 48000.0;
    bool enabled = false;
    bool coefficientsNeedUpdate = false;
    juce::SmoothedValue<double> cutoff;
    juce::SmoothedValue<double> blend;
    DoubleBiquad::Coefficients coefficients;
    DoubleBiquad::State state;
};
