#pragma once

#include "DbGainEnvelope.h"
#include "SoftKneeGainComputer.h"

class StereoCompressor
{
public:
    void prepare (double sampleRate, const CompressorParameters::Values& values) noexcept;
    void setParameters (const CompressorParameters::Values& values) noexcept;
    void process (juce::AudioBuffer<float>& buffer, int numInputChannels) noexcept;

    float getGainReductionDb() const noexcept { return envelope.getGainReductionDb(); }

private:
    SoftKneeGainComputer gainComputer;
    DbGainEnvelope envelope;
    juce::SmoothedValue<float> inputDb;
    juce::SmoothedValue<float> makeupDb;
};
