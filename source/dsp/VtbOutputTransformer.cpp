#include "VtbOutputTransformer.h"

double VtbOutputTransformer::driveDbFor (float amountPercent) noexcept
{
    // Extra drive over the calibrated VTB1148 operating point (0 dBFS = +18 dBu).
    return TransformerSettings::vtbMinimumDriveDb
        + (TransformerSettings::vtbMaximumDriveDb - TransformerSettings::vtbMinimumDriveDb)
            * static_cast<double> (amountPercent) / 100.0;
}
void VtbOutputTransformer::prepare (double sampleRate, float amountPercent, bool out, int latencyTarget)
{
    rate = sampleRate;
    factor = ModuleSpike::vtb1148OutputFactor (rate);
    MagneticAudio::prepareFilters (filters, factor);
    const auto own = juce::roundToInt (filters.getLatencyInSamples());
    jassert (std::abs (filters.getLatencyInSamples() - own) < 1.0e-10 && own <= latencyTarget && latencyTarget <= MagneticAudio::maximumLatency);
    latency = juce::jmax (own, latencyTarget);
    wetPadding.prepare (latency - own);
    dryDelay.prepare (latency);
    scratch.setSize (2, MagneticAudio::capacity);
    delayedDry.setSize (2, MagneticAudio::capacity);
    const auto calibration = ModuleSpike::vtb1148OutputC();
    for (auto& circuit : circuits)
        circuit.prepare (rate * factor, ModuleSpike::Variant::play3Outer, ModuleSpike::Topology::shunt, calibration);
    safety.reset();
    wasProcessing = false;
    driveGain.reset (rate * factor, TransformerSettings::amountRampSeconds);
    active.reset (rate, TransformerSettings::bypassRampSeconds);
    driveGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (calibration.driveDb + driveDbFor (amountPercent)));
    active.setCurrentAndTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}
void VtbOutputTransformer::setParameters (float amountPercent, bool out) noexcept
{
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (ModuleSpike::vtb1148OutputC().driveDb + driveDbFor (amountPercent)));
    active.setTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}
void VtbOutputTransformer::resetHistory() noexcept
{
    for (auto& circuit : circuits)
        circuit.reset();
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
            // Keep the FIR and padding history warm so re-engaging is seamless.
            driveGain.skip (count * factor);
            if (wasProcessing) resetHistory();
            wasProcessing = false;
        }
        else
        {
            wasProcessing = true;
            for (std::size_t n = 0; n < high.getNumSamples(); ++n)
            {
                const auto drive = driveGain.getNextValue();
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto x = safety.process (high.getSample (channel, static_cast<int> (n)));
                    high.setSample (channel, static_cast<int> (n), circuits[static_cast<std::size_t> (channel)].process (x * drive) / drive);
                }
            }
        }
        filters.processSamplesDown (base);
        for (int n = 0; n < count; ++n)
        {
            const auto padded = wetPadding.process (static_cast<float> (scratch.getSample (0, n)),
                static_cast<float> (channels > 1 ? scratch.getSample (1, n) : scratch.getSample (0, n)));
            const auto amount = activity[static_cast<std::size_t> (n)];
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto original = delayedDry.getSample (channel, n);
                const auto wet = padded[static_cast<std::size_t> (channel)];
                buffer.setSample (channel, start + n, amount <= 0.0f ? original
                    : amount >= 1.0f ? wet : original + amount * (wet - original));
            }
        }
    }
}
