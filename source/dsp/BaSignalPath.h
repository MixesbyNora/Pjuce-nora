#pragma once

#include "StereoCompressor.h"
#include "LowFrequencyMono.h"
#include "TransformerSaturation.h"
#include "VtbOutputTransformer.h"
#include "../PeakAccumulator.h"

class BaSignalPath
{
public:
    void prepare (double sampleRate, int maximumBlockSize,
        const CompressorParameters::Values& compressorValues, const BaParameters::Values& values);
    float process (juce::AudioBuffer<float>& buffer, int channels,
        const CompressorParameters::Values& compressorValues, const BaParameters::Values& values,
        LevelMeterPeaks* meters = nullptr) noexcept;
    int getLatencySamples() const noexcept { return transformer.getLatencySamples(); }

private:
    static float crossfade (float dry, float wet, float amount) noexcept;
    StereoCompressor compressor;
    // Transformer model choice: both stages report the Magnetic latency, so a
    // switch never changes delay compensation. On a switch both engines run for
    // one latency plus a short warm-up, flushing the idle engine's stale filter
    // and delay history, then crossfade.
    void processTransformer (juce::AudioBuffer<float>& wet, int channels, int samples) noexcept;
    TransformerSaturation transformer;
    VtbOutputTransformer vtb;
    juce::AudioBuffer<float> alternate;
    juce::SmoothedValue<float> modelBlend;
    int selectedModel = BaParameters::xfrmModelInitial;
    int modelWarmRemaining = 0, modelWarmSamples = 0;
    float modelTarget = 0.0f;
    LowFrequencyMono lfm;
    juce::AudioBuffer<float> dry;
    MagneticAudio::StereoDelay dryDelay;
    int chunkCapacity = 1;
    juce::SmoothedValue<float> inputGain, outputGain, mix, bypass;
    std::array<juce::SmoothedValue<float>, 4> monitor;
};
