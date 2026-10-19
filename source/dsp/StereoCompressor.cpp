#include "StereoCompressor.h"

void StereoCompressor::prepare (double sampleRate, const CompressorParameters::Values& values) noexcept
{
    envelope.prepare (sampleRate);
    inputDb.reset (sampleRate, 0.02);
    makeupDb.reset (sampleRate, 0.02);
    inputDb.setCurrentAndTargetValue (values.inputDb);
    makeupDb.setCurrentAndTargetValue (values.makeupDb);
    setParameters (values);
}

void StereoCompressor::setParameters (const CompressorParameters::Values& values) noexcept
{
    inputDb.setTargetValue (values.inputDb);
    makeupDb.setTargetValue (values.makeupDb);
    gainComputer.setThresholdDb (values.thresholdDb);
    envelope.setTimes (values.attackMs, values.releaseMs);
}

void StereoCompressor::process (juce::AudioBuffer<float>& buffer, int numInputChannels) noexcept
{
    const auto numChannels = juce::jmin (numInputChannels, buffer.getNumChannels());
    auto* const* channels = buffer.getArrayOfWritePointers();

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto inputGain = juce::Decibels::decibelsToGain (inputDb.getNextValue());
        auto peak = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            channels[channel][sample] *= inputGain;
            peak = juce::jmax (peak, std::abs (channels[channel][sample]));
        }

        // Feedforward: detect the louder input channel, before gain reduction.
        // Convert the instantaneous peak to dB; smooth the curve's attenuation
        // in dB with a single shared attack/release envelope.
        const auto levelDb = juce::Decibels::gainToDecibels (peak, -160.0f);
        const auto reductionDb = envelope.process (gainComputer.getGainReductionDb (levelDb));
        const auto outputGain = juce::Decibels::decibelsToGain (makeupDb.getNextValue() - reductionDb);

        for (int channel = 0; channel < numChannels; ++channel)
            channels[channel][sample] *= outputGain;
    }
}
