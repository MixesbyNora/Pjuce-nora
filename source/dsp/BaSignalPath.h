#pragma once

#include "StereoCompressor.h"
#include "LowFrequencyMono.h"
#include "MagneticOversampling.h"
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
    int getLatencySamples() const noexcept { return latency; }
    int getOversamplingFactor() const noexcept { return factor; }
    // Audio-thread/offline inspection only.
    const VtbTransformerCore& getInputTransformer() const noexcept { return inputTransformer; }
    const VtbTransformerCore& getOutputTransformer() const noexcept { return outputTransformer; }

private:
    static float crossfade (float dry, float wet, float amount) noexcept;
    static VtbTransformerCore::Tube outputStage (const BaParameters::Values&) noexcept;
    static bool outputEngaged (const BaParameters::Values&) noexcept;
    // Digital IN, then one shared oversampled region (4/2/1): input transformer
    // (VTB9046, Input XFRM) -> Analog Input + compressor -> output stage: 12BH7
    // follower (Output Tube, Tube Amount) and VTB1148 (Output XFRM, Xfrm Amount),
    // solved jointly. Then LF Mono and Digital OUT at the base rate.
    float processAnalog (juce::AudioBuffer<float>& wet, int channels) noexcept;
    StereoCompressor compressor;
    VtbTransformerCore inputTransformer, outputTransformer;
    juce::dsp::Oversampling<double> filters { 2 };
    juce::AudioBuffer<double> scratch;
    juce::AudioBuffer<float> compressorBlock;
    juce::SmoothedValue<float> outputActive, inputActive;
    std::array<float, MagneticAudio::capacity> activity {}, inputActivity {};
    bool outputWasProcessing = false, inputWasProcessing = false;
    int latency = 0, factor = 4;
    LowFrequencyMono lfm;
    juce::AudioBuffer<float> dry;
    MagneticAudio::StereoDelay dryDelay;
    int chunkCapacity = 1;
    juce::SmoothedValue<float> inputGain, outputGain, mix, bypass;
    std::array<juce::SmoothedValue<float>, 4> monitor;
};
