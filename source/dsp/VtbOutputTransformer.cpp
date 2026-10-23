#include "VtbOutputTransformer.h"

double VtbOutputTransformer::driveDbFor (float amountPercent) noexcept
{
    return TransformerSettings::vtbMinimumDriveDb
        + (TransformerSettings::vtbMaximumDriveDb - TransformerSettings::vtbMinimumDriveDb)
            * static_cast<double> (amountPercent) / 100.0;
}
void VtbOutputTransformer::prepare (double sampleRate, float amountPercent, bool out,
    TransformerVoicing::Values voicing, bool levelMatch, ModuleSpike::TransformerParameters calibration)
{
    rate = sampleRate;
    factor = ModuleSpike::vtb1148OutputFactor (rate);
    MagneticAudio::prepareLiveFilters (filters, factor, sampleRate);
    latency = juce::roundToInt (filters.getLatencyInSamples());
    jassert (latency <= MagneticAudio::maximumLatency && std::abs (filters.getLatencyInSamples() - latency) < 1.0e-10);
    scratch.setSize (2, MagneticAudio::capacity);
    delayedDry.setSize (2, MagneticAudio::capacity);
    dryDelay.prepare (latency);
    core.prepare (rate * factor, calibration, driveDbFor (amountPercent), voicing, levelMatch);
    wasProcessing = false;
    active.reset (rate, TransformerSettings::bypassRampSeconds);
    active.setCurrentAndTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}
void VtbOutputTransformer::setParameters (float amountPercent, bool out) noexcept
{
    core.setExtraDriveDb (driveDbFor (amountPercent));
    active.setTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}
void VtbOutputTransformer::process (juce::AudioBuffer<float>& buffer, int channels) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    for (int start = 0; start < buffer.getNumSamples(); start += MagneticAudio::capacity)
    {
        const auto count = juce::jmin (MagneticAudio::capacity, buffer.getNumSamples() - start);
        bool needsProcessing = false;
        for (int n = 0; n < count; ++n)
        {
            const auto left = buffer.getSample (0, start + n);
            const auto right = channels > 1 ? buffer.getSample (1, start + n) : left;
            const auto delayed = dryDelay.process (left, right);
            activity[static_cast<std::size_t> (n)] = active.getNextValue();
            needsProcessing |= activity[static_cast<std::size_t> (n)] > 0.0f;
            for (int channel = 0; channel < channels; ++channel)
            {
                scratch.setSample (channel, n, buffer.getSample (channel, start + n));
                delayedDry.setSample (channel, n, delayed[static_cast<std::size_t> (channel)]);
            }
        }
        auto base = juce::dsp::AudioBlock<double> (scratch).getSubsetChannelBlock (0, static_cast<std::size_t> (channels)).getSubBlock (0, static_cast<std::size_t> (count));
        auto high = filters.processSamplesUp (base);
        if (! needsProcessing)
        {
            // Keep only the FIR history warm; the output is the exact delayed input.
            core.skip (count * factor);
            if (wasProcessing) core.reset();
            wasProcessing = false;
            filters.processSamplesDown (base);
            for (int channel = 0; channel < channels; ++channel)
                buffer.copyFrom (channel, start, delayedDry, channel, 0, count);
            continue;
        }
        std::array<double, 2> frame {};
        for (std::size_t n = 0; n < high.getNumSamples(); ++n)
        {
            if (activity[n / static_cast<std::size_t> (factor)] <= 0.0f)
            {
                core.skip (1);
                if (wasProcessing) core.reset();
                wasProcessing = false;
                continue; // Identity FIR input; magnetic work stops sample-exactly.
            }
            wasProcessing = true;
            for (int channel = 0; channel < channels; ++channel)
                frame[static_cast<std::size_t> (channel)] = high.getSample (channel, static_cast<int> (n));
            core.processFrame (frame, channels);
            for (int channel = 0; channel < channels; ++channel)
                high.setSample (channel, static_cast<int> (n), frame[static_cast<std::size_t> (channel)]);
        }
        filters.processSamplesDown (base);
        for (int n = 0; n < count; ++n)
        {
            const auto amount = activity[static_cast<std::size_t> (n)];
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto original = delayedDry.getSample (channel, n);
                const auto wet = static_cast<float> (scratch.getSample (channel, n));
                buffer.setSample (channel, start + n, amount <= 0.0f ? original
                    : amount >= 1.0f ? wet : original + amount * (wet - original));
            }
        }
    }
}
