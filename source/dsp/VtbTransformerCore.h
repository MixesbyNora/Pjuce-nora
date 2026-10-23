#pragma once

#include "TransformerSettings.h"
#include "../TransformerVoicing.h"
#include "../modules/TransformerModule.h"
#include <juce_audio_basics/juce_audio_basics.h>

// One variant C transformer at the oversampled (internal) rate, a frame at a time:
// both channels, workbench voicing with a warmed bank crossfade (new circuit
// values need fresh state), drive smoothing with output level compensation, and
// optional level match. Oversampling, bypass and dry paths belong to the caller:
// the standalone VtbOutputTransformer and the shared chain in BaSignalPath.
class VtbTransformerCore
{
public:
    void prepare (double internalRate, ModuleSpike::TransformerParameters calibration, double extraDriveDb,
        TransformerVoicing::Values voicing = {}, bool levelMatch = false);
    void setExtraDriveDb (double extraDriveDb) noexcept;
    void setVoicing (TransformerVoicing::Values voicing, bool levelMatch = false) noexcept;
    // Advance time without processing (bypassed samples keep controls in step).
    void skip (int internalSamples) noexcept { driveGain.skip (internalSamples); }
    // Clears circuit history and level match; voicing and drive targets are kept.
    void reset() noexcept;
    void processFrame (std::array<double, 2>& frame, int channels) noexcept;

    bool isVoicingTransitioning() const noexcept { return fading || warmRemaining > 0; }
    TransformerVoicing::Values getAppliedVoicing() const noexcept { return TransformerVoicing::decode (banks[currentBank].voicing); }
    const ModuleSpike::SolverDiagnostics& getDiagnostics (int channel = 0) const noexcept
    { return banks[currentBank].circuits[static_cast<std::size_t> (channel)].diagnostics; }
    // Circuit values for a voicing; the default voicing returns the calibration exactly.
    static ModuleSpike::TransformerParameters parametersFor (ModuleSpike::TransformerParameters calibration, TransformerVoicing::Values) noexcept;

private:
    struct Bank
    {
        std::array<ModuleSpike::TransformerCircuit, 2> circuits;
        TransformerVoicing::Word voicing = 0;
    };
    void configureBank (Bank&, TransformerVoicing::Word) noexcept;
    std::array<Bank, 2> banks;
    ModuleSpike::TransformerParameters base;
    ModuleSpike::InputSafety safety;
    std::size_t currentBank = 0;
    TransformerVoicing::Word requestedVoicing = 0;
    double rate = 192000.0;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> driveGain;
    juce::SmoothedValue<float> voicingFade;
    double inputPower = 0.0, outputPower = 0.0, matchGain = 1.0;
    double powerCoefficient = 0.0, gainCoefficient = 0.0, maximumMatchGain = 1.0;
    bool fading = false, matchRequested = false, started = false;
    int warmRemaining = 0, restRemaining = 0, warmSamples = 0, restSamples = 0;
};
