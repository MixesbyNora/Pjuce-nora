#include "TransformerSaturation.h"

double TransformerSaturation::driveGainFor (float amountPercent) noexcept
{
    const auto db = TransformerSettings::minimumDriveDb
        + (TransformerSettings::maximumDriveDb - TransformerSettings::minimumDriveDb)
            * static_cast<double> (amountPercent) / 100.0;
    return juce::Decibels::decibelsToGain (db);
}
void TransformerSaturation::configureBank (Bank& bank, std::uint32_t word) noexcept
{
    const auto voice = TransformerVoicing::decode (word);
    const auto raw = juce::dsp::IIR::ArrayCoefficients<double>::makeLowShelf (internalRate,
        static_cast<double> (voice.cornerHz), TransformerSettings::shelfQ,
        juce::Decibels::decibelsToGain (static_cast<double> (voice.emphasisDb)));
    bank.pre = { raw[0] / raw[3], raw[1] / raw[3], raw[2] / raw[3], raw[4] / raw[3], raw[5] / raw[3] };
    const auto inverseB0 = 1.0 / bank.pre.b0;
    bank.post = { inverseB0, bank.pre.a1 * inverseB0, bank.pre.a2 * inverseB0,
        bank.pre.b1 * inverseB0, bank.pre.b2 * inverseB0 };
    bank.driveScale = juce::Decibels::decibelsToGain (static_cast<double> (voice.driveCalibrationDb));
    bank.voicing = word;
    for (auto& state : bank.state)
    {
        state.pre = {}; state.post = {};
        state.core.reset();
    }
}
void TransformerSaturation::resetCoreHistory() noexcept
{
    for (auto& bank : banks)
        for (auto& state : bank.state)
        {
            state.pre = {}; state.post = {};
            state.core.reset();
        }
}
void TransformerSaturation::resetHistory() noexcept
{
    resetCoreHistory();
    inputPower = outputPower = 0.0;
    matchGain = 1.0;
}
void TransformerSaturation::prepare (double sampleRate, float amountPercent, bool out,
    TransformerVoicing::Values voicing, bool levelMatch)
{
    rate = sampleRate;
    factor = MagneticPrototype::AudioSettings::factorForRate (rate);
    internalRate = rate * factor;
    MagneticAudio::prepareFilters (filters, factor);
    latency = juce::roundToInt (filters.getLatencyInSamples());
    jassert (latency <= MagneticAudio::maximumLatency && std::abs (filters.getLatencyInSamples() - latency) < 1.0e-10);
    scratch.setSize (2, MagneticAudio::capacity);
    delayedDry.setSize (2, MagneticAudio::capacity);
    dryDelay.prepare (latency);
    for (auto& bank : banks)
        for (auto& state : bank.state)
            state.core.prepare (internalRate, {});
    requestedVoicing = TransformerVoicing::encode (voicing);
    currentBank = 0;
    configureBank (banks[0], requestedVoicing);
    configureBank (banks[1], requestedVoicing);
    resetHistory();
    fading = wasProcessing = false;
    matchRequested = levelMatch;
    powerCoefficient = std::exp (-1.0 / (rate * TransformerSettings::matchWindowSeconds));
    gainCoefficient = std::exp (-1.0 / (rate * TransformerSettings::matchGainSeconds));
    maximumMatchGain = juce::Decibels::decibelsToGain (TransformerSettings::matchMaximumDb);
    driveGain.reset (internalRate, TransformerSettings::amountRampSeconds);
    active.reset (rate, TransformerSettings::bypassRampSeconds);
    voicingFade.reset (internalRate, TransformerSettings::voicingFadeSeconds);
    voicingFade.setCurrentAndTargetValue (0.0f);
    driveGain.setCurrentAndTargetValue (driveGainFor (amountPercent));
    active.setCurrentAndTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}
void TransformerSaturation::setVoicing (TransformerVoicing::Values voicing, bool levelMatch) noexcept
{
    requestedVoicing = TransformerVoicing::encode (voicing);
    matchRequested = levelMatch;
}
void TransformerSaturation::setParameters (float amountPercent, bool out) noexcept
{
    driveGain.setTargetValue (driveGainFor (amountPercent));
    active.setTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}
double TransformerSaturation::processBank (Bank& bank, int channel, double original, double drive) noexcept
{
    auto& state = bank.state[static_cast<std::size_t> (channel)];
    state.core.setDriveGain (drive * bank.driveScale);
    const auto emphasized = DoubleBiquad::processSample (original, bank.pre, state.pre);
    return DoubleBiquad::processSample (state.core.processSample (emphasized), bank.post, state.post);
}
void TransformerSaturation::process (juce::AudioBuffer<float>& buffer, int channels) noexcept
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
            // Keep only the FIR history warm. Restarting it on an arbitrary
            // waveform edge would inject an impulse during re-engagement.
            auto identity = juce::dsp::AudioBlock<double> (scratch).getSubsetChannelBlock (0, static_cast<std::size_t> (channels)).getSubBlock (0, static_cast<std::size_t> (count));
            filters.processSamplesUp (identity);
            filters.processSamplesDown (identity);
            for (int channel = 0; channel < channels; ++channel)
                buffer.copyFrom (channel, start, delayedDry, channel, 0, count);
            continue;
        }
        if (! fading && requestedVoicing != banks[currentBank].voicing)
        {
            if (! wasProcessing) configureBank (banks[currentBank], requestedVoicing);
            else
            {
                configureBank (banks[1 - currentBank], requestedVoicing);
                voicingFade.setCurrentAndTargetValue (0.0f);
                voicingFade.setTargetValue (1.0f);
                fading = true;
            }
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
                continue; // Identity FIR input; magnetic work stops sample-exactly.
            }
            wasProcessing = true;
            const auto blend = fading ? voicingFade.getNextValue() : 0.0f;
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto ch = static_cast<std::size_t> (channel);
                const auto original = high.getSample (static_cast<int> (ch), static_cast<int> (n));
                auto output = processBank (banks[currentBank], channel, original, drive);
                if (fading)
                {
                    const auto other = processBank (banks[1 - currentBank], channel, original, drive);
                    output += blend * (other - output);
                }
                high.setSample (static_cast<int> (ch), static_cast<int> (n), output);
            }
            if (fading && ! voicingFade.isSmoothing())
            {
                currentBank = 1 - currentBank;
                fading = false;
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
