#pragma once

#include "StereoCompressor.h"
#include "LowFrequencyMono.h"
#include "../PeakAccumulator.h"

class BaSignalPath
{
public:
    void prepare (double sampleRate, int maximumBlockSize,
        const CompressorParameters::Values& compressorValues, const BaParameters::Values& values);
    float process (juce::AudioBuffer<float>& buffer, int channels,
        const CompressorParameters::Values& compressorValues, const BaParameters::Values& values,
        LevelMeterPeaks* meters = nullptr) noexcept;

private:
    static float crossfade (float dry, float wet, float amount) noexcept;
    StereoCompressor compressor;
    LowFrequencyMono lfm;
    juce::AudioBuffer<float> dry;
    int chunkCapacity = 1;
    juce::SmoothedValue<float> inputGain, outputGain, mix, bypass;
    std::array<juce::SmoothedValue<float>, 4> monitor;
};
