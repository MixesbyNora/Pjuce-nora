#pragma once

#include "MagneticOversampling.h"
#include "VtbTransformerCore.h"

// Standalone variant C transformer stage with its own oversampling (4/2/1,
// vtb1148OutputFactor), Amount/Out bypass and bit-exact delayed dry when
// bypassed. Used for offline measurements and stage tests, for either
// calibration (VTB1148 output, VTB9046 line input). The live plugin runs the
// same VtbTransformerCore inside the shared oversampled chain in BaSignalPath.
class VtbOutputTransformer
{
public:
    void prepare (double sampleRate, float amountPercent, bool out,
        TransformerVoicing::Values voicing = {}, bool levelMatch = false,
        ModuleSpike::TransformerParameters calibration = ModuleSpike::vtb1148OutputC());
    void setParameters (float amountPercent, bool out) noexcept;
    void setVoicing (TransformerVoicing::Values voicing, bool levelMatch = false) noexcept { core.setVoicing (voicing, levelMatch); }
    void process (juce::AudioBuffer<float>& buffer, int channels) noexcept;
    int getLatencySamples() const noexcept { return latency; }
    int getOversamplingFactor() const noexcept { return factor; }
    // Audio-thread/offline inspection only; UI continues to use atomic voicing.
    bool isVoicingTransitioning() const noexcept { return core.isVoicingTransitioning(); }
    TransformerVoicing::Values getAppliedVoicing() const noexcept { return core.getAppliedVoicing(); }
    const ModuleSpike::SolverDiagnostics& getDiagnostics (int channel = 0) const noexcept { return core.getDiagnostics (channel); }
    // Extra drive over the calibrated operating point (0 dBFS = +18 dBu).
    static double driveDbFor (float amountPercent) noexcept;
    static ModuleSpike::TransformerParameters parametersFor (TransformerVoicing::Values voice) noexcept
    { return VtbTransformerCore::parametersFor (ModuleSpike::vtb1148OutputC(), voice); }

private:
    VtbTransformerCore core;
    juce::dsp::Oversampling<double> filters { 2 };
    juce::AudioBuffer<double> scratch;
    juce::AudioBuffer<float> delayedDry;
    MagneticAudio::StereoDelay dryDelay;
    double rate = 48000.0;
    int latency = 0, factor = 4;
    juce::SmoothedValue<float> active;
    std::array<float, MagneticAudio::capacity> activity {};
    bool wasProcessing = false;
};
