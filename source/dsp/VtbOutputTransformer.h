#pragma once

#include "MagneticOversampling.h"
#include "TransformerSettings.h"
#include "../TransformerVoicing.h"
#include "../modules/TransformerModule.h"

// Live VTB1148 output transformer: shunt variant C at the owner-chosen calibration
// (vtb1148OutputC, +17 dBu onset) and oversampling (vtb1148OutputFactor, 4/2/1).
// Workbench voicing fine-tunes it relative to that calibration; a voicing change
// warms a second bank of circuits and crossfades, since new circuit values need
// fresh state. Shared by live audio and offline measurements.
class VtbOutputTransformer
{
public:
    void prepare (double sampleRate, float amountPercent, bool out,
        TransformerVoicing::Values voicing = {}, bool levelMatch = false);
    void setParameters (float amountPercent, bool out) noexcept;
    void setVoicing (TransformerVoicing::Values voicing, bool levelMatch = false) noexcept;
    void process (juce::AudioBuffer<float>& buffer, int channels) noexcept;
    int getLatencySamples() const noexcept { return latency; }
    int getOversamplingFactor() const noexcept { return factor; }
    // Audio-thread/offline inspection only; UI continues to use atomic voicing.
    bool isVoicingTransitioning() const noexcept { return fading || warmRemaining > 0; }
    TransformerVoicing::Values getAppliedVoicing() const noexcept { return TransformerVoicing::decode (banks[currentBank].voicing); }
    const ModuleSpike::SolverDiagnostics& getDiagnostics (int channel = 0) const noexcept
    { return banks[currentBank].circuits[static_cast<std::size_t> (channel)].diagnostics; }
    static double driveDbFor (float amountPercent) noexcept;
    // Circuit values for a voicing; the default voicing returns vtb1148OutputC() exactly.
    static ModuleSpike::TransformerParameters parametersFor (TransformerVoicing::Values) noexcept;

private:
    struct Bank
    {
        std::array<ModuleSpike::TransformerCircuit, 2> circuits;
        TransformerVoicing::Word voicing = 0;
    };
    void configureBank (Bank&, TransformerVoicing::Word) noexcept;
    void resetCoreHistory() noexcept;
    void resetHistory() noexcept;
    std::array<Bank, 2> banks;
    ModuleSpike::InputSafety safety;
    juce::dsp::Oversampling<double> filters { 2 };
    juce::AudioBuffer<double> scratch;
    juce::AudioBuffer<float> delayedDry;
    MagneticAudio::StereoDelay dryDelay;
    std::size_t currentBank = 0;
    TransformerVoicing::Word requestedVoicing = 0;
    double rate = 48000.0, internalRate = 192000.0;
    int latency = 0, factor = 4;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> driveGain;
    juce::SmoothedValue<float> active, voicingFade;
    std::array<float, MagneticAudio::capacity> activity {};
    double inputPower = 0.0, outputPower = 0.0, matchGain = 1.0;
    double powerCoefficient = 0.0, gainCoefficient = 0.0, maximumMatchGain = 1.0;
    bool fading = false, matchRequested = false, wasProcessing = false;
    int warmRemaining = 0, restRemaining = 0, warmSamples = 0, restSamples = 0;
};
