#include "StereoCompressor.h"

void StereoCompressor::prepare (double sampleRate, const CompressorParameters::Values& values) noexcept
{
    envelope.prepare (sampleRate);
    sidechainFilter.prepare (sampleRate);
    inputDb.reset (sampleRate, 0.02);
    makeupDb.reset (sampleRate, 0.02);
    feedbackAmount.reset (sampleRate, CompressorParameters::fbffSmoothingSeconds);
    inputDb.setCurrentAndTargetValue (values.inputDb);
    makeupDb.setCurrentAndTargetValue (values.makeupDb);
    feedbackAmount.setCurrentAndTargetValue ((100.0f - values.fbffPercent) * 0.01f);
    setParameters (values);
}

void StereoCompressor::setParameters (const CompressorParameters::Values& values) noexcept
{
    inputDb.setTargetValue (values.inputDb);
    makeupDb.setTargetValue (values.makeupDb);
    feedbackAmount.setTargetValue ((100.0f - values.fbffPercent) * 0.01f);
    gainComputer.setThresholdDb (values.thresholdDb);
    envelope.setTimes (values.attackMs, values.releaseMs);
    sidechainFilter.setMode (values.scFilterMode);
}

float StereoCompressor::process (juce::AudioBuffer<float>& buffer, int numInputChannels) noexcept
{
    const auto numChannels = juce::jmin (numInputChannels, buffer.getNumChannels());
    auto* const* channels = buffer.getArrayOfWritePointers();
    auto blockPeakDb = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto inputGain = juce::Decibels::decibelsToGain (inputDb.getNextValue());
        auto peak = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            channels[channel][sample] *= inputGain;
            // Filter only a detector copy. Off returns the original sample
            // exactly, preserving the previous float peak/log calculation.
            const auto detectionSample = static_cast<float> (
                sidechainFilter.processSample (channel, channels[channel][sample]));
            peak = juce::jmax (peak, std::abs (detectionSample));
        }

        // Blend in the detector using the previous sample's applied attenuation.
        // A single curve and envelope serve every FB/FF setting, before makeup.
        constexpr auto detectorFloorDb = -160.0f;
        const auto levelDb = juce::Decibels::gainToDecibels (peak, detectorFloorDb);
        const auto detectDb = juce::jmax (detectorFloorDb,
            levelDb - feedbackAmount.getNextValue() * envelope.getGainReductionDb());
        const auto reductionDb = envelope.process (gainComputer.getGainReductionDb (detectDb));
        blockPeakDb = juce::jmax (blockPeakDb, reductionDb);
        const auto outputGain = juce::Decibels::decibelsToGain (makeupDb.getNextValue() - reductionDb);

        for (int channel = 0; channel < numChannels; ++channel)
            channels[channel][sample] *= outputGain;
    }
    return blockPeakDb;
}
