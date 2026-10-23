#pragma once

#include "TransformerSettings.h"
#include "../TransformerVoicing.h"
#include "../TubeVoicing.h"
#include "../modules/CathodeFollower.h"
#include <juce_audio_basics/juce_audio_basics.h>

// One variant C transformer at the oversampled (internal) rate, a frame at a time:
// both channels, workbench voicing with a warmed bank crossfade (new circuit
// values need fresh state), drive smoothing with output level compensation, and
// optional level match. Oversampling, bypass and dry paths belong to the caller:
// the standalone VtbOutputTransformer and the shared chain in BaSignalPath.
//
// The output stage may put a 12BH7 cathode follower in front, solved jointly with
// the transformer (docs/TubeStage.md), or alone into the transformer's load when
// the transformer is out. Tube and transformer switches and tube voicing use the
// same warmed crossfade as voicing. Joint and tube-only outputs are normalized by
// the follower's quiescent midband gain, so switching keeps the level.
struct VtbOutputStage
{
    TubeVoicing::Values voicing;
    bool tubeIn = false, transformerIn = true; // Neither: the caller bypasses; the mode stays.
    // Tube Amount: the tube's own drive, like the transformer's extra drive. The
    // iron's level stays set by the transformer drive (docs/TubeStage.md).
    double tubeExtraDriveDb = 0.0;
};
class VtbTransformerCore
{
public:
    enum class Mode { transformer, tubeAndTransformer, tube };
    using Tube = VtbOutputStage;
    void prepare (double internalRate, ModuleSpike::TransformerParameters calibration, double extraDriveDb,
        TransformerVoicing::Values voicing = {}, bool levelMatch = false, Tube tube = {});
    void setExtraDriveDb (double extraDriveDb) noexcept;
    void setVoicing (TransformerVoicing::Values voicing, bool levelMatch = false) noexcept;
    void setTube (Tube tube) noexcept;
    // Advance time without processing (bypassed samples keep controls in step).
    void skip (int internalSamples) noexcept { driveGain.skip (internalSamples); tubeDriveGain.skip (internalSamples); }
    // Clears circuit history and level match; voicing and drive targets are kept.
    void reset() noexcept;
    void processFrame (std::array<double, 2>& frame, int channels) noexcept;

    bool isVoicingTransitioning() const noexcept { return fading || warmRemaining > 0; }
    TransformerVoicing::Values getAppliedVoicing() const noexcept { return TransformerVoicing::decode (banks[currentBank].config.transformer); }
    TubeVoicing::Values getAppliedTubeVoicing() const noexcept { return TubeVoicing::decode (banks[currentBank].config.tube); }
    Mode getAppliedMode() const noexcept { return banks[currentBank].config.mode; }
    const ModuleSpike::SolverDiagnostics& getDiagnostics (int channel = 0) const noexcept
    { return banks[currentBank].circuits[static_cast<std::size_t> (channel)].diagnostics; }
    const ModuleSpike::StageDiagnostics& getTubeDiagnostics (int channel = 0) const noexcept
    {
        const auto& bank = banks[currentBank];
        const auto ch = static_cast<std::size_t> (channel);
        return bank.config.mode == Mode::tube ? bank.tubeOnly[ch].diagnostics : bank.joint[ch].diagnostics;
    }
    // Circuit values for a voicing; the default voicing returns the calibration exactly.
    static ModuleSpike::TransformerParameters parametersFor (ModuleSpike::TransformerParameters calibration, TransformerVoicing::Values) noexcept;
    // Follower design for a tube voicing (cathode at half the supply).
    static ModuleSpike::StageDesign designFor (TubeVoicing::Values) noexcept;

private:
    struct Config
    {
        TransformerVoicing::Word transformer = 0;
        TubeVoicing::Word tube = 0;
        Mode mode = Mode::transformer;
        bool operator== (const Config&) const = default;
    };
    struct Bank
    {
        std::array<ModuleSpike::TransformerCircuit, 2> circuits;
        std::array<ModuleSpike::CathodeFollower<ModuleSpike::MagneticLoad>, 2> joint;
        std::array<ModuleSpike::CathodeFollower<ModuleSpike::ResistiveLoad>, 2> tubeOnly;
        Config config;
        double unitVolts = 1.0, tubeGain = 1.0;
    };
    void configureBank (Bank&, Config) noexcept;
    static double render (Bank&, std::size_t channel, double x, double drive, double tubeScale) noexcept;
    std::array<Bank, 2> banks;
    ModuleSpike::TransformerParameters base;
    ModuleSpike::InputSafety safety;
    std::size_t currentBank = 0;
    Config requested;
    double rate = 192000.0;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> driveGain, tubeDriveGain;
    juce::SmoothedValue<float> voicingFade;
    double inputPower = 0.0, outputPower = 0.0, matchGain = 1.0;
    double powerCoefficient = 0.0, gainCoefficient = 0.0, maximumMatchGain = 1.0;
    bool fading = false, matchRequested = false, started = false;
    int warmRemaining = 0, restRemaining = 0, warmSamples = 0, restSamples = 0;
};
