#pragma once

#include "MagneticOversampling.h"
#include "TransformerSettings.h"
#include "../modules/TransformerModule.h"

// Live VTB1148 output transformer: shunt variant C at the owner-chosen calibration
// (vtb1148OutputC, +17 dBu onset) and oversampling (vtb1148OutputFactor, 4/2/1).
// Reports the latency it is given, padding its own FIR delay, so switching
// between transformer models never changes the host's delay compensation.
class VtbOutputTransformer
{
public:
    // latencyTarget must be at least this stage's own FIR latency at the rate.
    void prepare (double sampleRate, float amountPercent, bool out, int latencyTarget);
    void setParameters (float amountPercent, bool out) noexcept;
    void process (juce::AudioBuffer<float>& buffer, int channels) noexcept;
    void resetHistory() noexcept;
    int getLatencySamples() const noexcept { return latency; }
    int getOversamplingFactor() const noexcept { return factor; }
    // Audio-thread/offline inspection only.
    const ModuleSpike::SolverDiagnostics& getDiagnostics (int channel = 0) const noexcept
    { return circuits[static_cast<std::size_t> (channel)].diagnostics; }
    static double driveDbFor (float amountPercent) noexcept;

private:
    std::array<ModuleSpike::TransformerCircuit, 2> circuits;
    ModuleSpike::InputSafety safety;
    juce::dsp::Oversampling<double> filters { 2 };
    juce::AudioBuffer<double> scratch;
    juce::AudioBuffer<float> delayedDry;
    MagneticAudio::StereoDelay dryDelay, wetPadding;
    double rate = 48000.0;
    int latency = 0, factor = 4;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> driveGain;
    juce::SmoothedValue<float> active;
    std::array<float, MagneticAudio::capacity> activity {};
    bool wasProcessing = false;
};
