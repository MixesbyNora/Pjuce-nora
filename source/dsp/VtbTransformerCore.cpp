#include "VtbTransformerCore.h"

ModuleSpike::TransformerParameters VtbTransformerCore::parametersFor (ModuleSpike::TransformerParameters p, TransformerVoicing::Values voice) noexcept
{
    const auto ratio = [&voice] (std::size_t i) { return std::pow (10.0, TransformerVoicing::dspValue (voice, i) / 20.0); };
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
void VtbTransformerCore::configureBank (Bank& bank, TransformerVoicing::Word word) noexcept
{
    const auto p = parametersFor (base, TransformerVoicing::decode (word));
    bank.voicing = word;
    for (auto& circuit : bank.circuits)
        circuit.prepare (rate, ModuleSpike::Variant::play3Outer, ModuleSpike::Topology::shunt, p);
}
void VtbTransformerCore::reset() noexcept
{
    for (auto& bank : banks)
        for (auto& circuit : bank.circuits)
            circuit.reset();
    fading = started = false;
    warmRemaining = restRemaining = 0;
    inputPower = outputPower = 0.0;
    matchGain = 1.0;
}
void VtbTransformerCore::prepare (double internalRate, ModuleSpike::TransformerParameters calibration, double extraDriveDb,
    TransformerVoicing::Values voicing, bool levelMatch)
{
    rate = internalRate;
    base = calibration;
    requestedVoicing = TransformerVoicing::encode (voicing);
    currentBank = 0;
    configureBank (banks[0], requestedVoicing);
    configureBank (banks[1], requestedVoicing);
    safety.reset();
    reset();
    warmSamples = juce::roundToInt (rate * TransformerSettings::voicingWarmSeconds);
    restSamples = juce::roundToInt (rate * TransformerSettings::voicingRestSeconds);
    matchRequested = levelMatch;
    powerCoefficient = std::exp (-1.0 / (rate * TransformerSettings::matchWindowSeconds));
    gainCoefficient = std::exp (-1.0 / (rate * TransformerSettings::matchGainSeconds));
    maximumMatchGain = juce::Decibels::decibelsToGain (TransformerSettings::matchMaximumDb);
    driveGain.reset (rate, TransformerSettings::amountRampSeconds);
    voicingFade.reset (rate, TransformerSettings::voicingFadeSeconds);
    voicingFade.setCurrentAndTargetValue (0.0f);
    driveGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (base.driveDb + extraDriveDb));
}
void VtbTransformerCore::setExtraDriveDb (double extraDriveDb) noexcept
{
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (base.driveDb + extraDriveDb));
}
void VtbTransformerCore::setVoicing (TransformerVoicing::Values voicing, bool levelMatch) noexcept
{
    requestedVoicing = TransformerVoicing::encode (voicing);
    matchRequested = levelMatch;
}
void VtbTransformerCore::processFrame (std::array<double, 2>& frame, int channels) noexcept
{
    const auto drive = driveGain.getNextValue();
    // A history reset (bypass, transport) applies a pending voicing directly.
    if (! started && requestedVoicing != banks[currentBank].voicing)
        configureBank (banks[currentBank], requestedVoicing);
    started = true;
    if (restRemaining > 0) --restRemaining;
    if (! isVoicingTransitioning() && restRemaining == 0 && requestedVoicing != banks[currentBank].voicing)
    {
        configureBank (banks[1 - currentBank], requestedVoicing);
        warmRemaining = warmSamples;
    }
    const auto blend = fading ? voicingFade.getNextValue() : 0.0f;
    double frameInputPower = 0.0, frameOutputPower = 0.0;
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto ch = static_cast<std::size_t> (channel);
        const auto original = frame[ch];
        const auto x = safety.process (original) * drive;
        auto output = banks[currentBank].circuits[ch].process (x) / drive;
        if (isVoicingTransitioning())
        {
            const auto other = banks[1 - currentBank].circuits[ch].process (x) / drive;
            output += blend * (other - output);
        }
        frameInputPower += original * original;
        frameOutputPower += output * output;
        frame[ch] = output;
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
    if (matchRequested || std::abs (matchGain - 1.0) > 1.0e-9)
    {
        inputPower = powerCoefficient * inputPower + (1.0 - powerCoefficient) * frameInputPower / channels;
        outputPower = powerCoefficient * outputPower + (1.0 - powerCoefficient) * frameOutputPower / channels;
        const auto target = matchRequested && inputPower > TransformerSettings::matchSilencePower && outputPower > TransformerSettings::matchSilencePower
            ? juce::jlimit (1.0 / maximumMatchGain, maximumMatchGain, std::sqrt (inputPower / outputPower)) : 1.0;
        matchGain = target + gainCoefficient * (matchGain - target);
        if (! matchRequested && std::abs (matchGain - 1.0) < 1.0e-9) matchGain = 1.0;
        for (int channel = 0; channel < channels; ++channel)
            frame[static_cast<std::size_t> (channel)] *= matchGain;
    }
}
