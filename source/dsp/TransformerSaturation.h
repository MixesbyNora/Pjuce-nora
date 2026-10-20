#pragma once

#include "Biquad.h"
#include "TanhADAA.h"
#include <juce_audio_basics/juce_audio_basics.h>

class TransformerSaturation
{
public:
    void prepare (double sampleRate, float amountPercent, bool out) noexcept;
    void setParameters (float amountPercent, bool out) noexcept;
    void process (juce::AudioBuffer<float>& buffer, int channels) noexcept;

    const DoubleBiquad::Coefficients& getPreEmphasisCoefficients() const noexcept { return preEmphasis; }
    const DoubleBiquad::Coefficients& getDeEmphasisCoefficients() const noexcept { return deEmphasis; }

private:
    struct Channel
    {
        DoubleBiquad::State pre, post;
        double previousDriven = 0.0;
        bool primed = false;
    };
    static double driveDbFor (float amountPercent) noexcept;
    DoubleBiquad::Coefficients preEmphasis, deEmphasis;
    std::array<Channel, 2> state {};
    juce::SmoothedValue<double> driveDb;
    juce::SmoothedValue<float> active;
    bool wasProcessing = false;
};
