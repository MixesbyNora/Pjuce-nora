#include "StereoCompressor.h"

void StereoCompressor::prepare (double sampleRate, const CompressorParameters::Values& values) noexcept
{
    envelope.prepare (sampleRate);
    sidechainFilter.prepare (sampleRate);
    inputDb.reset (sampleRate, 0.02);
    makeupDb.reset (sampleRate, 0.02);
    feedbackAmount.reset (sampleRate, CompressorParameters::fbffSmoothingSeconds);
    calibrationDb.reset (sampleRate, 0.02);
    calibrationDb.setCurrentAndTargetValue (values.calibrationDb);
    elementFade.reset (sampleRate, 0.02);
    // Both tubes' tables are built here, never on the audio thread.
    GainTube::curve (GainTube::Tube::ge5670);
    GainTube::curve (GainTube::Tube::ge6386);
    active.configure (requested);
    fading = false;
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
    calibrationDb.setTargetValue (values.calibrationDb);
    gainComputer.setThresholdDb (values.thresholdDb);
    envelope.setTimes (values.attackMs, values.releaseMs);
    sidechainFilter.setMode (values.scFilterMode);
}

void StereoCompressor::Element::configure (GainElement element) noexcept
{
    settings = element;
    for (auto& cell : cells)
        cell.configure (element.settings);
}

void StereoCompressor::setGainElement (GainElement element) noexcept
{
    requested = element;
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
        // Calibration moves only the level the detector sees.
        const auto levelDb = juce::Decibels::gainToDecibels (peak, detectorFloorDb) + calibrationDb.getNextValue();
        const auto detectDb = juce::jmax (detectorFloorDb,
            levelDb - feedbackAmount.getNextValue() * envelope.getGainReductionDb());
        const auto reductionDb = envelope.process (gainComputer.getGainReductionDb (detectDb));
        blockPeakDb = juce::jmax (blockPeakDb, reductionDb);
        const auto makeup = makeupDb.getNextValue();
        const auto outputGain = juce::Decibels::decibelsToGain (makeup - reductionDb);
        if (! fading && ! (requested == active.settings))
        {
            next.configure (requested);
            elementFade.setCurrentAndTargetValue (0.0f);
            elementFade.setTargetValue (1.0f);
            fading = true;
        }
        if (! fading && ! active.settings.tube)
        {
            for (int channel = 0; channel < numChannels; ++channel)
                channels[channel][sample] *= outputGain;
            continue;
        }
        const auto makeupGain = static_cast<double> (juce::Decibels::decibelsToGain (makeup));
        const auto fade = fading ? static_cast<double> (elementFade.getNextValue()) : 0.0;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto ch = static_cast<std::size_t> (channel);
            const auto x = static_cast<double> (channels[channel][sample]);
            auto y = active.render (ch, x, reductionDb, outputGain, makeupGain);
            if (fading)
                y += fade * (next.render (ch, x, reductionDb, outputGain, makeupGain) - y);
            channels[channel][sample] = static_cast<float> (y);
        }
        if (fading && ! elementFade.isSmoothing())
        {
            std::swap (active, next);
            fading = false;
        }
    }
    return blockPeakDb;
}
