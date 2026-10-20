#pragma once

#include "Biquad.h"
#include "TanhADAA.h"
#include "../TransformerVoicing.h"
#include <juce_audio_basics/juce_audio_basics.h>

class TransformerSaturation
{
public:
    void prepare (double sampleRate, float amountPercent, bool out,
        TransformerVoicing::Values voicing = {}, bool levelMatch = false) noexcept;
    void setParameters (float amountPercent, bool out) noexcept;
    void setVoicing (TransformerVoicing::Values voicing, bool levelMatch = false) noexcept;
    void process (juce::AudioBuffer<float>& buffer, int channels) noexcept;

    const DoubleBiquad::Coefficients& getPreEmphasisCoefficients() const noexcept { return banks[currentBank].pre; }
    const DoubleBiquad::Coefficients& getDeEmphasisCoefficients() const noexcept { return banks[currentBank].post; }

private:
    struct Channel
    {
        DoubleBiquad::State pre, post;
        double previousDriven = 0.0;
        bool primed = false;
    };
    struct Bank
    {
        DoubleBiquad::Coefficients pre, post;
        std::array<Channel, 2> state {};
        double driveScale = 1.0;
        std::uint32_t voicing = 0;
    };
    static double driveDbFor (float amountPercent) noexcept;
    void configureBank (Bank& bank, std::uint32_t voicing) noexcept;
    static float processBank (Bank& bank, int channel, float original, double drive) noexcept;
    void resetHistory() noexcept;
    std::array<Bank, 2> banks;
    std::size_t currentBank = 0;
    std::uint32_t requestedVoicing = 0;
    double rate = 48000.0;
    juce::SmoothedValue<double> driveDb;
    juce::SmoothedValue<float> active;
    juce::SmoothedValue<float> voicingFade;
    double inputPower = 0.0, outputPower = 0.0, matchGain = 1.0;
    double powerCoefficient = 0.0, gainCoefficient = 0.0, maximumMatchGain = 1.0;
    bool fading = false;
    bool matchRequested = false;
    bool wasProcessing = false;
};
