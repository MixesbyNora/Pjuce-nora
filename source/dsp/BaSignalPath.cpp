#include "BaSignalPath.h"

void BaSignalPath::prepare (double sampleRate, int maximumBlockSize,
    const CompressorParameters::Values& compressorValues, const BaParameters::Values& values)
{
    chunkCapacity = juce::jmax (1, maximumBlockSize);
    dry.setSize (2, chunkCapacity);
    factor = ModuleSpike::vtb1148OutputFactor (sampleRate);
    MagneticAudio::prepareLiveFilters (filters, factor, sampleRate);
    latency = juce::roundToInt (filters.getLatencyInSamples());
    jassert (latency <= MagneticAudio::maximumLatency && std::abs (filters.getLatencyInSamples() - latency) < 1.0e-10);
    const auto internalRate = sampleRate * factor;
    scratch.setSize (2, MagneticAudio::capacity);
    compressorBlock.setSize (2, MagneticAudio::capacity * factor);
    // The compressor runs at the internal rate; its time constants and sidechain
    // filters follow the rate it is prepared with.
    compressor.prepare (internalRate, compressorValues);
    inputTransformer.prepare (internalRate, ModuleSpike::vtb9046LineInputC(), 0.0,
        values.inputTransformerVoicing, values.inputVoicingLevelMatch);
    outputTransformer.prepare (internalRate, ModuleSpike::vtb1148OutputC(),
        VtbOutputTransformer::driveDbFor (values.transformerAmountPercent), values.transformerVoicing,
        values.voicingLevelMatch || values.tubeVoicingLevelMatch, outputStage (values));
    outputActive.reset (sampleRate, TransformerSettings::bypassRampSeconds);
    outputActive.setCurrentAndTargetValue (outputEngaged (values) ? 1.0f : 0.0f);
    outputWasProcessing = false;
    inputActive.reset (sampleRate, TransformerSettings::bypassRampSeconds);
    inputActive.setCurrentAndTargetValue (values.inputTransformerOut ? 0.0f : 1.0f);
    inputWasProcessing = false;
    dryDelay.prepare (latency);
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

float BaSignalPath::processAnalog (juce::AudioBuffer<float>& wet, int channels) noexcept
{
    auto peakDb = 0.0f;
    for (int start = 0; start < wet.getNumSamples(); start += MagneticAudio::capacity)
    {
        const auto count = juce::jmin (MagneticAudio::capacity, wet.getNumSamples() - start);
        auto engaged = false;
        for (int n = 0; n < count; ++n)
        {
            activity[static_cast<std::size_t> (n)] = outputActive.getNextValue();
            engaged |= activity[static_cast<std::size_t> (n)] > 0.0f;
            inputActivity[static_cast<std::size_t> (n)] = inputActive.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
                scratch.setSample (channel, n, wet.getSample (channel, start + n));
        }
        auto base = juce::dsp::AudioBlock<double> (scratch).getSubsetChannelBlock (0, static_cast<std::size_t> (channels)).getSubBlock (0, static_cast<std::size_t> (count));
        auto high = filters.processSamplesUp (base);
        const auto highCount = static_cast<int> (high.getNumSamples());
        std::array<double, 2> frame {};
        for (int n = 0; n < highCount; ++n)
        {
            for (int channel = 0; channel < channels; ++channel)
                frame[static_cast<std::size_t> (channel)] = high.getSample (channel, n);
            const auto inputAmount = inputActivity[static_cast<std::size_t> (n / factor)];
            if (inputAmount <= 0.0f)
            {
                // Input XFRM Out: the iron stops sample-exactly and restarts clean.
                inputTransformer.skip (1);
                if (inputWasProcessing) inputTransformer.reset();
                inputWasProcessing = false;
            }
            else
            {
                inputWasProcessing = true;
                const auto original = frame;
                inputTransformer.processFrame (frame, channels);
                if (inputAmount < 1.0f)
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        const auto ch = static_cast<std::size_t> (channel);
                        frame[ch] = original[ch] + inputAmount * (frame[ch] - original[ch]);
                    }
            }
            for (int channel = 0; channel < channels; ++channel)
                compressorBlock.setSample (channel, n, static_cast<float> (frame[static_cast<std::size_t> (channel)]));
        }
        // Non-owning view of preallocated storage (no heap allocation).
        juce::AudioBuffer<float> view (compressorBlock.getArrayOfWritePointers(), channels, 0, highCount);
        peakDb = juce::jmax (peakDb, compressor.process (view, channels));
        for (int n = 0; n < highCount; ++n)
        {
            const auto amount = activity[static_cast<std::size_t> (n / factor)];
            for (int channel = 0; channel < channels; ++channel)
                frame[static_cast<std::size_t> (channel)] = compressorBlock.getSample (channel, n);
            if (! engaged || amount <= 0.0f)
            {
                // Output stage bypassed (Amount 0, or tube and transformer both out): its work stops sample-exactly.
                outputTransformer.skip (1);
                if (outputWasProcessing) outputTransformer.reset();
                outputWasProcessing = false;
            }
            else
            {
                outputWasProcessing = true;
                const auto original = frame;
                outputTransformer.processFrame (frame, channels);
                if (amount < 1.0f)
                    for (int channel = 0; channel < channels; ++channel)
                    {
                        const auto ch = static_cast<std::size_t> (channel);
                        frame[ch] = original[ch] + amount * (frame[ch] - original[ch]);
                    }
            }
            for (int channel = 0; channel < channels; ++channel)
                high.setSample (channel, n, frame[static_cast<std::size_t> (channel)]);
        }
        filters.processSamplesDown (base);
        for (int channel = 0; channel < channels; ++channel)
            for (int n = 0; n < count; ++n)
                wet.setSample (channel, start + n, static_cast<float> (scratch.getSample (channel, n)));
    }
    return peakDb;
}

VtbTransformerCore::Tube BaSignalPath::outputStage (const BaParameters::Values& values) noexcept
{
    return { values.tubeVoicing, ! values.outputTubeOut, ! values.transformerOut };
}
bool BaSignalPath::outputEngaged (const BaParameters::Values& values) noexcept
{
    // Amount drives the whole output stage; it is bypassed when both parts are out.
    return values.transformerAmountPercent > 0.0f && (! values.transformerOut || ! values.outputTubeOut);
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
    outputTransformer.setExtraDriveDb (VtbOutputTransformer::driveDbFor (values.transformerAmountPercent));
    outputTransformer.setVoicing (values.transformerVoicing, values.voicingLevelMatch || values.tubeVoicingLevelMatch);
    outputTransformer.setTube (outputStage (values));
    outputActive.setTargetValue (outputEngaged (values) ? 1.0f : 0.0f);
    inputTransformer.setVoicing (values.inputTransformerVoicing, values.inputVoicingLevelMatch);
    inputActive.setTargetValue (values.inputTransformerOut ? 0.0f : 1.0f);
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
        peakDb = juce::jmax (peakDb, processAnalog (wet, channels));
        for (int sample = 0; sample < samples; ++sample)
        {
            // LF Mono, then Digital OUT, before Mix: the dry path is never scaled.
            auto left = pointers[0][sample];
            auto right = channels > 1 ? pointers[1][sample] : left;
            if (channels > 1)
                lfm.process (left, right);
            const auto gain = outputGain.getNextValue();
            left *= gain;
            right *= gain;
            const auto dryValues = dryDelay.process (dry.getSample (0, sample), channels > 1 ? dry.getSample (1, sample) : dry.getSample (0, sample));
            const auto dryLeft = dryValues[0];
            const auto dryRight = dryValues[1];
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
                // Compare the same mixed, bypass-crossfaded signal as LR
                // against the untouched input, before any monitor routing.
                const auto deltaLeft = left - dryLeft;
                const auto deltaRight = right - dryRight;
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
