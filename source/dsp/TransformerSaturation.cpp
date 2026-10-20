#include "TransformerSaturation.h"
#include <juce_dsp/juce_dsp.h>

double TransformerSaturation::driveDbFor (float amountPercent) noexcept
{
    return TransformerSettings::minimumDriveDb
        + (TransformerSettings::maximumDriveDb - TransformerSettings::minimumDriveDb)
            * static_cast<double> (amountPercent) / 100.0;
}

void TransformerSaturation::configureBank (Bank& bank, std::uint32_t word) noexcept
{
    const auto voice = TransformerVoicing::decode (word);
    // ArrayCoefficients returns fixed-size storage, so changing voicing requires
    // no allocation. Coefficients change only in the inactive filter bank.
    const auto raw = juce::dsp::IIR::ArrayCoefficients<double>::makeLowShelf (rate,
        static_cast<double> (voice.cornerHz), TransformerSettings::shelfQ,
        juce::Decibels::decibelsToGain (static_cast<double> (voice.emphasisDb)));
    bank.pre = { raw[0] / raw[3], raw[1] / raw[3], raw[2] / raw[3], raw[4] / raw[3], raw[5] / raw[3] };
    const auto inverseB0 = 1.0 / bank.pre.b0;
    bank.post = { inverseB0, bank.pre.a1 * inverseB0, bank.pre.a2 * inverseB0,
        bank.pre.b1 * inverseB0, bank.pre.b2 * inverseB0 };
    bank.driveScale = juce::Decibels::decibelsToGain (static_cast<double> (voice.driveCalibrationDb));
    bank.voicing = word;
    bank.state = {};
}

void TransformerSaturation::resetHistory() noexcept
{
    for (auto& bank : banks)
        bank.state = {};
    inputPower = outputPower = 0.0;
    matchGain = 1.0;
}

void TransformerSaturation::prepare (double sampleRate, float amountPercent, bool out,
    TransformerVoicing::Values voicing, bool levelMatch) noexcept
{
    rate = sampleRate;
    requestedVoicing = TransformerVoicing::encode (voicing);
    currentBank = 0;
    configureBank (banks[0], requestedVoicing);
    configureBank (banks[1], requestedVoicing);
    resetHistory();
    fading = false;
    wasProcessing = false;
    matchRequested = levelMatch;
    powerCoefficient = std::exp (-1.0 / (sampleRate * TransformerSettings::matchWindowSeconds));
    gainCoefficient = std::exp (-1.0 / (sampleRate * TransformerSettings::matchGainSeconds));
    maximumMatchGain = juce::Decibels::decibelsToGain (TransformerSettings::matchMaximumDb);
    driveDb.reset (sampleRate, TransformerSettings::amountRampSeconds);
    active.reset (sampleRate, TransformerSettings::bypassRampSeconds);
    voicingFade.reset (sampleRate, TransformerSettings::voicingFadeSeconds);
    voicingFade.setCurrentAndTargetValue (0.0f);
    driveDb.setCurrentAndTargetValue (driveDbFor (amountPercent));
    active.setCurrentAndTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}

void TransformerSaturation::setVoicing (TransformerVoicing::Values voicing, bool levelMatch) noexcept
{
    requestedVoicing = TransformerVoicing::encode (voicing);
    matchRequested = levelMatch;
}

float TransformerSaturation::processBank (Bank& bank, int channel, float original, double baseDrive) noexcept
{
    auto& s = bank.state[static_cast<std::size_t> (channel)];
    const auto drive = baseDrive * bank.driveScale;
    const auto emphasized = DoubleBiquad::processSample (original, bank.pre, s.pre);
    const auto driven = drive * emphasized;
    if (! s.primed)
    {
        s.previousDriven = driven;
        s.primed = true;
    }
    // ADAA of the nonlinear residual, retaining the undelayed linear path.
    const auto average = driven * 0.5 + s.previousDriven * 0.5;
    const auto saturated = emphasized + (TanhADAA::evaluate (driven, s.previousDriven) - average) / drive;
    s.previousDriven = driven;
    return static_cast<float> (DoubleBiquad::processSample (saturated, bank.post, s.post));
}

void TransformerSaturation::setParameters (float amountPercent, bool out) noexcept
{
    driveDb.setTargetValue (driveDbFor (amountPercent));
    active.setTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}

void TransformerSaturation::process (juce::AudioBuffer<float>& buffer, int channels) noexcept
{
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto currentDriveDb = driveDb.getNextValue();
        const auto amount = active.getNextValue();
        if (amount <= 0.0f)
        {
            if (wasProcessing)
                resetHistory();
            wasProcessing = false;
            fading = false;
            continue; // Exact untouched samples, including level-match audition.
        }
        if (! fading && requestedVoicing != banks[currentBank].voicing)
        {
            if (! wasProcessing)
                configureBank (banks[currentBank], requestedVoicing);
            else
            {
                configureBank (banks[1 - currentBank], requestedVoicing);
                voicingFade.setCurrentAndTargetValue (0.0f);
                voicingFade.setTargetValue (1.0f);
                fading = true;
            }
        }
        wasProcessing = true;
        const auto drive = juce::Decibels::decibelsToGain (currentDriveDb);
        const auto blend = fading ? voicingFade.getNextValue() : 0.0f;
        std::array<float, 2> processed {}, original {};
        double sampleInputPower = 0.0, sampleOutputPower = 0.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            original[index] = buffer.getSample (channel, sample);
            processed[index] = processBank (banks[currentBank], channel, original[index], drive);
            if (fading)
            {
                const auto other = processBank (banks[1 - currentBank], channel, original[index], drive);
                processed[index] = blend >= 1.0f ? other : processed[index] + blend * (other - processed[index]);
            }
            if (matchRequested || matchGain < 1.0 || matchGain > 1.0)
            {
                sampleInputPower += static_cast<double> (original[index]) * original[index];
                sampleOutputPower += static_cast<double> (processed[index]) * processed[index];
            }
        }
        if (matchRequested || matchGain < 1.0 || matchGain > 1.0)
        {
            inputPower = powerCoefficient * inputPower + (1.0 - powerCoefficient) * sampleInputPower / channels;
            outputPower = powerCoefficient * outputPower + (1.0 - powerCoefficient) * sampleOutputPower / channels;
            const auto target = matchRequested && inputPower > TransformerSettings::matchSilencePower
                && outputPower > TransformerSettings::matchSilencePower
                ? juce::jlimit (1.0 / maximumMatchGain, maximumMatchGain, std::sqrt (inputPower / outputPower)) : 1.0;
            matchGain = target + gainCoefficient * (matchGain - target);
            if (! matchRequested && std::abs (matchGain - 1.0) < 1.0e-9)
                matchGain = 1.0;
            for (int channel = 0; channel < channels; ++channel)
                processed[static_cast<std::size_t> (channel)] = static_cast<float> (
                    processed[static_cast<std::size_t> (channel)] * matchGain);
        }
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto index = static_cast<std::size_t> (channel);
            buffer.setSample (channel, sample, amount >= 1.0f ? processed[index]
                : original[index] + amount * (processed[index] - original[index]));
        }
        if (fading && ! voicingFade.isSmoothing())
        {
            currentBank = 1 - currentBank;
            fading = false;
        }
    }
}
