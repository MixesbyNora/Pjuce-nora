#include "BaSignalPath.h"

void BaSignalPath::prepare (double sampleRate, int maximumBlockSize,
    const CompressorParameters::Values& compressorValues, const BaParameters::Values& values)
{
    chunkCapacity = juce::jmax (1, maximumBlockSize);
    dry.setSize (2, chunkCapacity);
    compressor.prepare (sampleRate, compressorValues);
    lfm.prepare (sampleRate, values.lfmHz);
    inputGain.reset (sampleRate, BaParameters::gainRampSeconds);
    outputGain.reset (sampleRate, BaParameters::gainRampSeconds);
    mix.reset (sampleRate, BaParameters::gainRampSeconds);
    bypass.reset (sampleRate, BaParameters::switchRampSeconds);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (values.inGainDb));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (values.outGainDb));
    mix.setCurrentAndTargetValue (values.mixPercent * 0.01f);
    bypass.setCurrentAndTargetValue (values.bypassed ? 1.0f : 0.0f);
    for (std::size_t i = 0; i < monitor.size(); ++i)
    {
        monitor[i].reset (sampleRate, BaParameters::switchRampSeconds);
        monitor[i].setCurrentAndTargetValue (static_cast<int> (i) == values.monitor ? 1.0f : 0.0f);
    }
}

float BaSignalPath::crossfade (float dryValue, float wet, float amount) noexcept
{
    if (amount <= 0.0f)
        return dryValue;
    if (amount >= 1.0f)
        return wet;
    return dryValue * (1.0f - amount) + wet * amount;
}

float BaSignalPath::process (juce::AudioBuffer<float>& buffer, int channels,
    const CompressorParameters::Values& compressorValues, const BaParameters::Values& values,
    LevelMeterPeaks* meters) noexcept
{
    compressor.setParameters (compressorValues);
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (values.inGainDb));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (values.outGainDb));
    mix.setTargetValue (values.mixPercent * 0.01f);
    bypass.setTargetValue (values.bypassed ? 1.0f : 0.0f);
    lfm.setCutoff (values.lfmHz);
    for (std::size_t i = 0; i < monitor.size(); ++i)
        monitor[i].setTargetValue (static_cast<int> (i) == values.monitor ? 1.0f : 0.0f);

    auto peakDb = 0.0f;
    std::array<float, 2> inputPeaks {}, outputPeaks {};
    for (int start = 0; start < buffer.getNumSamples(); start += chunkCapacity)
    {
        const auto samples = juce::jmin (chunkCapacity, buffer.getNumSamples() - start);
        std::array<float*, 2> pointers {};
        for (int channel = 0; channel < channels; ++channel)
        {
            dry.copyFrom (channel, 0, buffer, channel, start, samples);
            pointers[static_cast<std::size_t> (channel)] = buffer.getWritePointer (channel, start);
        }
        // JUCE uses its inline channel-pointer storage for this non-owning mono/
        // stereo view (no heap allocation). Dry storage was allocated in prepare.
        juce::AudioBuffer<float> wet (pointers.data(), channels, samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto gain = inputGain.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
                pointers[static_cast<std::size_t> (channel)][sample] *= gain;
        }
        if (meters != nullptr)
            for (int channel = 0; channel < channels; ++channel)
                inputPeaks[static_cast<std::size_t> (channel)] = juce::jmax (inputPeaks[static_cast<std::size_t> (channel)],
                    PeakAccumulator::findPeak (pointers[static_cast<std::size_t> (channel)], samples));
        peakDb = juce::jmax (peakDb, compressor.process (wet, channels));
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto gain = outputGain.getNextValue();
            auto left = pointers[0][sample] * gain;
            auto right = channels > 1 ? pointers[1][sample] * gain : left;
            if (channels > 1)
                lfm.process (left, right);
            const auto wetLeft = left;
            const auto wetRight = right;
            const auto dryLeft = dry.getSample (0, sample);
            const auto dryRight = channels > 1 ? dry.getSample (1, sample) : dryLeft;
            const auto wetAmount = mix.getNextValue();
            const auto bypassAmount = bypass.getNextValue();
            left = crossfade (crossfade (dryLeft, left, wetAmount), dryLeft, bypassAmount);
            right = crossfade (crossfade (dryRight, right, wetAmount), dryRight, bypassAmount);
            std::array<float, 4> weights {};
            for (std::size_t i = 0; i < weights.size(); ++i)
                weights[i] = monitor[i].getNextValue();
            if (weights[0] < 1.0f)
            {
                const auto mid = (left + right) * 0.5f;
                const auto side = channels > 1 ? (left - right) * 0.5f : 0.0f;
                const auto deltaAmount = 1.0f - bypassAmount;
                const auto deltaLeft = (wetLeft - dryLeft) * deltaAmount;
                const auto deltaRight = (wetRight - dryRight) * deltaAmount;
                left = weights[0] * left + weights[1] * mid + weights[2] * side + weights[3] * deltaLeft;
                right = weights[0] * right + weights[1] * mid + weights[2] * side + weights[3] * deltaRight;
            }
            pointers[0][sample] = left;
            if (channels > 1)
                pointers[1][sample] = right;
        }
        if (meters != nullptr)
            for (int channel = 0; channel < channels; ++channel)
                outputPeaks[static_cast<std::size_t> (channel)] = juce::jmax (outputPeaks[static_cast<std::size_t> (channel)],
                    PeakAccumulator::findPeak (pointers[static_cast<std::size_t> (channel)], samples));
    }
    if (meters != nullptr)
    {
        PeakAccumulator::publish (meters->inL, inputPeaks[0]);
        PeakAccumulator::publish (meters->inR, inputPeaks[channels > 1 ? 1u : 0u]);
        PeakAccumulator::publish (meters->outL, outputPeaks[0]);
        PeakAccumulator::publish (meters->outR, outputPeaks[channels > 1 ? 1u : 0u]);
    }
    return values.bypassed ? 0.0f : peakDb;
}
