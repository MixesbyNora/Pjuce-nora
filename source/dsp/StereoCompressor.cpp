#include "StereoCompressor.h"
#include <bit>

void StereoCompressor::prepare (double sampleRate, const CompressorParameters::Values& values) noexcept
{
    rate = sampleRate;
    envelope.prepare (sampleRate);
    detector.prepare (sampleRate);
    reduction = 0.0f;
    analogGain = 1.0;
    sidechainFilter.prepare (sampleRate);
    inputDb.reset (sampleRate, 0.02);
    makeupDb.reset (sampleRate, 0.02);
    headroomDriveDb.reset (sampleRate, 0.02);
    feedbackAmount.reset (sampleRate, CompressorParameters::fbffSmoothingSeconds);
    calibrationDb.reset (sampleRate, 0.02);
    calibrationDb.setCurrentAndTargetValue (values.calibrationDb);
    elementFade.reset (sampleRate, 0.02);
    // Both tubes' tables are built here, never on the audio thread.
    GainTube::curve (GainTube::Tube::ge5670);
    GainTube::curve (GainTube::Tube::ge6386);
    AmpTube::stage();
    active.configure (requested, rate);
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
    envelope.setReleaseMod (values.releaseModPercent * 0.01f);
    sidechainFilter.setMode (values.scFilterMode);
    analogSettings.thresholdDb = values.thresholdDb;
    analogSettings.attackMs = values.attackMs;
    analogSettings.releaseMs = values.releaseMs;
    analogSettings.releaseMod = values.releaseModPercent * 0.01;
    detector.configure (analogSettings);
}

void StereoCompressor::setDetector (Detector d) noexcept
{
    if (d.analog != analog)
    {
        // Carry the present gain reduction across: the control voltage that gives it,
        // or the envelope's state.
        const auto& tube = GainTube::curve (requested.settings.tube);
        if (d.analog)
        {
            detector.reset (tube.quiescentGrid() - tube.biasFor (reduction));
            analogGain = std::pow (10.0, -static_cast<double> (reduction) / 20.0);
        }
        else envelope.setGainReductionDb (reduction);
        analog = d.analog;
    }
    if (std::abs (d.sidechainGainDb - analogSettings.sidechainGainDb) > 1.0e-9)
    {
        analogSettings.sidechainGainDb = d.sidechainGainDb;
        detector.configure (analogSettings);
    }
}

void StereoCompressor::Element::configure (GainElement element, double sampleRate) noexcept
{
    settings = element;
    for (auto& cell : cells)
        cell.configure (element.settings);
    for (auto& grid : grids)
        grid.prepare (sampleRate);
}

void StereoCompressor::setGainElement (GainElement element) noexcept
{
    requested = element;
}

void StereoCompressor::setHeadroomDriveDb (double driveDb, bool immediately) noexcept
{
    if (immediately) headroomDriveDb.setCurrentAndTargetValue (driveDb);
    else headroomDriveDb.setTargetValue (driveDb);
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
        std::array<double, 2> sidechain {};

        for (int channel = 0; channel < numChannels; ++channel)
        {
            channels[channel][sample] *= inputGain;
            // Filter only a detector copy. Off returns the original sample
            // exactly, preserving the previous float peak/log calculation.
            const auto detectionSample = static_cast<float> (
                sidechainFilter.processSample (channel, channels[channel][sample]));
            peak = juce::jmax (peak, std::abs (detectionSample));
            if (channel < 2) sidechain[static_cast<std::size_t> (channel)] = static_cast<double> (detectionSample);
        }

        // Calibration moves only the level the detector sees.
        const auto calibration = calibrationDb.getNextValue();
        const auto feedback = feedbackAmount.getNextValue();
        float reductionDb;
        if (analog)
        {
            // FB senses the gain stage's output: the input at the previous sample's
            // gain. The diodes see the FF/FB blend of the two.
            const auto blend = 1.0 - static_cast<double> (feedback) * (1.0 - analogGain);
            for (auto& s : sidechain) s *= blend;
            if (std::bit_cast<std::uint32_t> (calibration) != calibrationBits)
            {
                calibrationBits = std::bit_cast<std::uint32_t> (calibration);
                calibrationGain = std::pow (10.0, static_cast<double> (calibration) / 20.0);
            }
            const auto control = detector.process (sidechain.data(), juce::jmin (numChannels, 2), calibrationGain);
            analogGain = GainTube::curve (requested.settings.tube).gainFor (control);
            reductionDb = static_cast<float> (-20.0 * std::log10 (analogGain));
        }
        else
        {
            // Blend in the detector using the previous sample's applied attenuation.
            // A single curve and envelope serve every FB/FF setting, before makeup.
            constexpr auto detectorFloorDb = -160.0f;
            const auto levelDb = juce::Decibels::gainToDecibels (peak, detectorFloorDb) + calibration;
            const auto detectDb = juce::jmax (detectorFloorDb, levelDb - feedback * envelope.getGainReductionDb());
            reductionDb = envelope.process (gainComputer.getGainReductionDb (detectDb));
        }
        reduction = reductionDb;
        blockPeakDb = juce::jmax (blockPeakDb, reductionDb);
        const auto makeup = makeupDb.getNextValue();
        const auto headroom = headroomDriveDb.getNextValue();
        const auto outputGain = juce::Decibels::decibelsToGain (makeup - reductionDb);
        if (! fading && ! (requested == active.settings))
        {
            next.configure (requested, rate);
            elementFade.setCurrentAndTargetValue (0.0f);
            elementFade.setTargetValue (1.0f);
            fading = true;
        }
        if (! fading && ! active.settings.tube && ! active.settings.amp)
        {
            for (int channel = 0; channel < numChannels; ++channel)
                channels[channel][sample] *= outputGain;
            continue;
        }
        const auto makeupGain = static_cast<double> (juce::Decibels::decibelsToGain (makeup));
        const auto headroomGain = std::pow (10.0, headroom / 20.0);
        const auto fade = fading ? static_cast<double> (elementFade.getNextValue()) : 0.0;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto ch = static_cast<std::size_t> (channel);
            const auto x = static_cast<double> (channels[channel][sample]);
            auto y = active.render (ch, x, reductionDb, outputGain, makeupGain, headroomGain);
            if (fading)
                y += fade * (next.render (ch, x, reductionDb, outputGain, makeupGain, headroomGain) - y);
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
