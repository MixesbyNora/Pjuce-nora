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
ModuleSpike::StageDesign VtbTransformerCore::designFor (TubeVoicing::Values voice) noexcept
{
    if (TubeVoicing::dspValue (voice, TubeVoicing::wiring) < 0.5)
        return ModuleSpike::pushPullDesign (TubeVoicing::dspValue (voice, TubeVoicing::plateCurrent) * 1.0e-3,
            TubeVoicing::dspValue (voice, TubeVoicing::coupling) * 1.0e-6, TubeVoicing::dspValue (voice, TubeVoicing::supply));
    return ModuleSpike::followerDesign (TubeVoicing::dspValue (voice, TubeVoicing::plateCurrent) * 1.0e-3,
        TubeVoicing::dspValue (voice, TubeVoicing::coupling) * 1.0e-6, TubeVoicing::dspValue (voice, TubeVoicing::supply),
        TubeVoicing::dspValue (voice, TubeVoicing::sections));
}
void VtbTransformerCore::configureBank (Bank& bank, Config config) noexcept
{
    const auto p = parametersFor (base, TransformerVoicing::decode (config.transformer));
    bank.config = config;
    for (auto& circuit : bank.circuits)
        circuit.prepare (rate, ModuleSpike::Variant::play3Outer, ModuleSpike::Topology::shunt, p);
    if (config.mode == Mode::transformer)
        return;
    const auto design = designFor (TubeVoicing::decode (config.tube));
    bank.unitVolts = ModuleSpike::transformerUnitVolts (p);
    // Midband load: the primary winding plus the referred load (C's branch open).
    bank.tubeGain = ModuleSpike::CathodeFollower<ModuleSpike::ResistiveLoad>::midbandGain (design,
        p.loss * ModuleSpike::TransformerCircuit::impedanceBase + p.loadOhms);
    if (config.mode == Mode::tubeAndTransformer)
        for (auto& stage : bank.joint) stage.prepare (rate, design, p);
    else
        for (auto& stage : bank.tubeOnly) stage.prepare (rate, design, p);
}
double VtbTransformerCore::render (Bank& bank, std::size_t channel, double x, double drive, double tubeScale) noexcept
{
    // x is the driven signal in C's normalized units; volts at the primary are x * unitVolts.
    // tubeScale is the tube's drive relative to the transformer's (1 when equal).
    switch (bank.config.mode)
    {
        case Mode::tubeAndTransformer:
            return bank.joint[channel].process (x * bank.unitVolts, tubeScale) / (bank.unitVolts * drive * bank.tubeGain);
        case Mode::tube:
            return bank.tubeOnly[channel].process (x * bank.unitVolts, tubeScale) / (bank.unitVolts * drive * bank.tubeGain);
        case Mode::transformer:
        default:
            return bank.circuits[channel].process (x) / drive;
    }
}
void VtbTransformerCore::reset() noexcept
{
    for (auto& bank : banks)
    {
        for (auto& circuit : bank.circuits) circuit.reset();
        for (auto& stage : bank.joint) stage.reset();
        for (auto& stage : bank.tubeOnly) stage.reset();
    }
    fading = started = false;
    warmRemaining = restRemaining = 0;
    inputPower = outputPower = 0.0;
    matchGain = 1.0;
}
void VtbTransformerCore::prepare (double internalRate, ModuleSpike::TransformerParameters calibration, double extraDriveDb,
    TransformerVoicing::Values voicing, bool levelMatch, Tube tube)
{
    rate = internalRate;
    base = calibration;
    requested = {};
    requested.transformer = TransformerVoicing::encode (voicing);
    setTube (tube);
    currentBank = 0;
    configureBank (banks[0], requested);
    configureBank (banks[1], requested);
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
    tubeDriveGain.reset (rate, TransformerSettings::amountRampSeconds);
    tubeDriveGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (base.driveDb + tube.tubeExtraDriveDb));
}
void VtbTransformerCore::setExtraDriveDb (double extraDriveDb) noexcept
{
    driveGain.setTargetValue (juce::Decibels::decibelsToGain (base.driveDb + extraDriveDb));
}
void VtbTransformerCore::setVoicing (TransformerVoicing::Values voicing, bool levelMatch) noexcept
{
    requested.transformer = TransformerVoicing::encode (voicing);
    matchRequested = levelMatch;
}
void VtbTransformerCore::setTube (Tube tube) noexcept
{
    requested.tube = TubeVoicing::encode (tube.voicing);
    tubeDriveGain.setTargetValue (juce::Decibels::decibelsToGain (base.driveDb + tube.tubeExtraDriveDb));
    if (tube.tubeIn || tube.transformerIn)
        requested.mode = ! tube.tubeIn ? Mode::transformer : tube.transformerIn ? Mode::tubeAndTransformer : Mode::tube;
}
void VtbTransformerCore::processFrame (std::array<double, 2>& frame, int channels) noexcept
{
    const auto drive = driveGain.getNextValue();
    const auto tubeScale = tubeDriveGain.getNextValue() / drive;
    // A history reset (bypass, transport) applies a pending voicing directly.
    if (! started && requested != banks[currentBank].config)
        configureBank (banks[currentBank], requested);
    started = true;
    if (restRemaining > 0) --restRemaining;
    if (! isVoicingTransitioning() && restRemaining == 0 && requested != banks[currentBank].config)
    {
        configureBank (banks[1 - currentBank], requested);
        warmRemaining = warmSamples;
    }
    const auto blend = fading ? voicingFade.getNextValue() : 0.0f;
    double frameInputPower = 0.0, frameOutputPower = 0.0;
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto ch = static_cast<std::size_t> (channel);
        const auto original = frame[ch];
        const auto x = safety.process (original) * drive;
        auto output = render (banks[currentBank], ch, x, drive, tubeScale);
        if (isVoicingTransitioning())
        {
            const auto other = render (banks[1 - currentBank], ch, x, drive, tubeScale);
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
