#include "VtbOutputTransformer.h"

double VtbOutputTransformer::driveDbFor (float amountPercent) noexcept
{
    // Extra drive over the calibrated VTB1148 operating point (0 dBFS = +18 dBu).
    return TransformerSettings::vtbMinimumDriveDb
        + (TransformerSettings::vtbMaximumDriveDb - TransformerSettings::vtbMinimumDriveDb)
            * static_cast<double> (amountPercent) / 100.0;
}
ModuleSpike::TransformerParameters VtbOutputTransformer::parametersFor (TransformerVoicing::Values voice) noexcept
{
    const auto ratio = [&voice] (std::size_t i) { return std::pow (10.0, TransformerVoicing::dspValue (voice, i) / 20.0); };
    auto p = ModuleSpike::vtb1148OutputC();
    // Saturation and knee together: flux capacity moves, small-signal slope and
    // hysteresis stay (shunt Step 3b). Exactly 1 at 0 dB.
    const auto onset = ratio (TransformerVoicing::onset);
    p.saturation *= onset;
    p.knee *= onset;
    p.asymmetry = std::min (0.9, p.asymmetry * ratio (TransformerVoicing::evenHarmonics));
    p.width *= ratio (TransformerVoicing::grain);
    p.memory = std::min (1.0, p.memory * (TransformerVoicing::dspValue (voice, TransformerVoicing::memory) / 100.0));
    return p;
}
void VtbOutputTransformer::configureBank (Bank& bank, TransformerVoicing::Word word) noexcept
{
    const auto p = parametersFor (TransformerVoicing::decode (word));
    bank.voicing = word;
    for (auto& circuit : bank.circuits)
        circuit.prepare (internalRate, ModuleSpike::Variant::play3Outer, ModuleSpike::Topology::shunt, p);
}
void VtbOutputTransformer::resetCoreHistory() noexcept
{
    for (auto& bank : banks)
        for (auto& circuit : bank.circuits)
            circuit.reset();
}
void VtbOutputTransformer::resetHistory() noexcept
{
    resetCoreHistory();
    inputPower = outputPower = 0.0;
    matchGain = 1.0;
}
void VtbOutputTransformer::prepare (double sampleRate, float amountPercent, bool out,
    TransformerVoicing::Values voicing, bool levelMatch)
{
    rate = sampleRate;
    factor = ModuleSpike::vtb1148OutputFactor (rate);
    internalRate = rate * factor;
    MagneticAudio::prepareFilters (filters, factor);
    latency = juce::roundToInt (filters.getLatencyInSamples());
    jassert (latency <= MagneticAudio::maximumLatency && std::abs (filters.getLatencyInSamples() - latency) < 1.0e-10);
    scratch.setSize (2, MagneticAudio::capacity);
    delayedDry.setSize (2, MagneticAudio::capacity);
    dryDelay.prepare (latency);
    requestedVoicing = TransformerVoicing::encode (voicing);
    currentBank = 0;
    configureBank (banks[0], requestedVoicing);
    configureBank (banks[1], requestedVoicing);
    safety.reset();
    resetHistory();
    fading = wasProcessing = false;
    warmRemaining = restRemaining = 0;
    warmSamples = juce::roundToInt (internalRate * TransformerSettings::voicingWarmSeconds);
    restSamples = juce::roundToInt (internalRate * TransformerSettings::voicingRestSeconds);
    matchRequested = levelMatch;
    powerCoefficient = std::exp (-1.0 / (rate * TransformerSettings::matchWindowSeconds));
    gainCoefficient = std::exp (-1.0 / (rate * TransformerSettings::matchGainSeconds));
    maximumMatchGain = juce::Decibels::decibelsToGain (TransformerSettings::matchMaximumDb);
    driveGain.reset (internalRate, TransformerSettings::amountRampSeconds);
    active.reset (rate, TransformerSettings::bypassRampSeconds);
    voicingFade.reset (internalRate, TransformerSettings::voicingFadeSeconds);
    voicingFade.setCurrentAndTargetValue (0.0f);
    driveGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (ModuleSpike::vtb1148OutputC().driveDb + driveDbFor (amountPercent)));
    active.setCurrentAndTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}
void VtbOutputTransformer::setVoicing (TransformerVoicing::Values voicing, bool levelMatch) noexcept
{
    requestedVoicing = TransformerVoicing::encode (voicing);
    matchRequested = levelMatch;
}
void VtbOutputTransformer::setParameters (float amountPercent, bool out) noexcept
{
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (ModuleSpike::vtb1148OutputC().driveDb + driveDbFor (amountPercent)));
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
        if (! needsProcessing)
        {
            driveGain.skip (count * factor);
            if (wasProcessing) resetHistory();
            wasProcessing = fading = false;
            warmRemaining = restRemaining = 0;
            // Keep only the FIR history warm. Restarting it on an arbitrary
            // waveform edge would inject an impulse during re-engagement.
            auto identity = juce::dsp::AudioBlock<double> (scratch).getSubsetChannelBlock (0, static_cast<std::size_t> (channels)).getSubBlock (0, static_cast<std::size_t> (count));
            filters.processSamplesUp (identity);
            filters.processSamplesDown (identity);
            for (int channel = 0; channel < channels; ++channel)
                buffer.copyFrom (channel, start, delayedDry, channel, 0, count);
            continue;
        }
        auto base = juce::dsp::AudioBlock<double> (scratch).getSubsetChannelBlock (0, static_cast<std::size_t> (channels)).getSubBlock (0, static_cast<std::size_t> (count));
        auto high = filters.processSamplesUp (base);
        for (std::size_t n = 0; n < high.getNumSamples(); ++n)
        {
            const auto drive = driveGain.getNextValue();
            if (activity[n / static_cast<std::size_t> (factor)] <= 0.0f)
            {
                if (wasProcessing) resetCoreHistory();
                wasProcessing = fading = false;
                warmRemaining = restRemaining = 0;
                continue; // Identity FIR input; magnetic work stops sample-exactly.
            }
            if (! wasProcessing && requestedVoicing != banks[currentBank].voicing)
                configureBank (banks[currentBank], requestedVoicing);
            wasProcessing = true;
            if (restRemaining > 0) --restRemaining;
            if (! isVoicingTransitioning() && restRemaining == 0 && requestedVoicing != banks[currentBank].voicing)
            {
                configureBank (banks[1 - currentBank], requestedVoicing);
                warmRemaining = warmSamples;
            }
            const auto blend = fading ? voicingFade.getNextValue() : 0.0f;
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto ch = static_cast<std::size_t> (channel);
                const auto x = safety.process (high.getSample (channel, static_cast<int> (n))) * drive;
                auto output = banks[currentBank].circuits[ch].process (x) / drive;
                if (isVoicingTransitioning())
                {
                    const auto other = banks[1 - currentBank].circuits[ch].process (x) / drive;
                    output += blend * (other - output);
                }
                high.setSample (channel, static_cast<int> (n), output);
            }
            if (warmRemaining > 0 && --warmRemaining == 0)
            {
                voicingFade.setCurrentAndTargetValue (0.0f);
                voicingFade.setTargetValue (1.0f);
                fading = true;
            }
            if (fading && ! voicingFade.isSmoothing())
            {
                currentBank = 1 - currentBank;
                fading = false;
                restRemaining = restSamples;
            }
        }
        filters.processSamplesDown (base);
        for (int n = 0; n < count; ++n)
        {
            if (activity[static_cast<std::size_t> (n)] <= 0.0f)
            {
                inputPower = outputPower = 0.0;
                matchGain = 1.0;
                for (int channel = 0; channel < channels; ++channel)
                    buffer.setSample (channel, start + n, delayedDry.getSample (channel, n));
                continue;
            }
            double sampleInputPower = 0.0, sampleOutputPower = 0.0;
            std::array<float, 2> processed {};
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto original = delayedDry.getSample (channel, n);
                processed[static_cast<std::size_t> (channel)] = static_cast<float> (scratch.getSample (channel, n));
                sampleInputPower += static_cast<double> (original) * original;
                sampleOutputPower += static_cast<double> (processed[static_cast<std::size_t> (channel)]) * processed[static_cast<std::size_t> (channel)];
            }
            if (matchRequested || std::abs (matchGain - 1.0) > 1.0e-9)
            {
                inputPower = powerCoefficient * inputPower + (1.0 - powerCoefficient) * sampleInputPower / channels;
                outputPower = powerCoefficient * outputPower + (1.0 - powerCoefficient) * sampleOutputPower / channels;
                const auto target = matchRequested && inputPower > TransformerSettings::matchSilencePower && outputPower > TransformerSettings::matchSilencePower
                    ? juce::jlimit (1.0 / maximumMatchGain, maximumMatchGain, std::sqrt (inputPower / outputPower)) : 1.0;
                matchGain = target + gainCoefficient * (matchGain - target);
                if (! matchRequested && std::abs (matchGain - 1.0) < 1.0e-9) matchGain = 1.0;
            }
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto original = delayedDry.getSample (channel, n);
                const auto wet = static_cast<float> (processed[static_cast<std::size_t> (channel)] * matchGain);
                const auto amount = activity[static_cast<std::size_t> (n)];
                buffer.setSample (channel, start + n, amount <= 0.0f ? original
                    : amount >= 1.0f ? wet : original + amount * (wet - original));
            }
        }
    }
}
