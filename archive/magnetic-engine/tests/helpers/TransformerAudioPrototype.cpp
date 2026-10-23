#include "TransformerAudioPrototype.h"

namespace TransformerAudioPrototype
{
Processor::Processor (double rate, int factor, int channels, Circuit::Parameters parameters, bool linear, bool exact)
    : filters (static_cast<std::size_t> (channels)), multiplier (factor)
{
    jassert (channels >= 1 && channels <= 2 && factor >= 1 && factor <= 512 && (factor & (factor - 1)) == 0);
    filters.clearOversamplingStages();
    if (factor == 1)
        filters.addDummyOversamplingStage();
    for (int stage = 1; stage < factor; stage *= 2)
    {
        const auto width = stage == 1 ? MagneticOversampling::firstTransition : MagneticOversampling::laterTransition;
        filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
            width, MagneticOversampling::stopbandDb, width, MagneticOversampling::stopbandDb);
    }
    filters.setUsingIntegerLatency (false);
    filters.initProcessing (capacity);
    for (auto& circuit : circuits)
        circuit.prepare (rate * factor, parameters, linear, exact);
    juce::AudioBuffer<double> block (channels, capacity);
    auto sum = 0.0, moment = 0.0;
    for (int start = 0; start < 1024; start += capacity)
    {
        block.clear();
        juce::dsp::AudioBlock<double> base (block);
        auto high = filters.processSamplesUp (base);
        high.clear();
        if (start == 0) high.setSample (0, 0, static_cast<double> (factor));
        filters.processSamplesDown (base);
        for (int i = 0; i < capacity; ++i)
        {
            sum += block.getSample (0, i);
            moment += (start + i) * block.getSample (0, i);
        }
    }
    decimatorLatency = moment / sum;
    reset();
}
void Processor::reset() noexcept
{
    filters.reset();
    for (auto& circuit : circuits) circuit.reset();
}
void Processor::process (juce::AudioBuffer<double>& audio) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    juce::dsp::AudioBlock<double> entire (audio);
    for (std::size_t offset = 0; offset < entire.getNumSamples(); offset += capacity)
    {
        auto base = entire.getSubBlock (offset, std::min (static_cast<std::size_t> (capacity), entire.getNumSamples() - offset));
        auto high = filters.processSamplesUp (base);
        for (std::size_t channel = 0; channel < high.getNumChannels(); ++channel)
        {
            auto* samples = high.getChannelPointer (channel);
            for (std::size_t i = 0; i < high.getNumSamples(); ++i)
                samples[i] = circuits[channel].processSample (samples[i]);
        }
        filters.processSamplesDown (base);
    }
}
void Processor::processAnalytic (juce::AudioBuffer<double>& audio, const Waveform& input, double start)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();
    juce::dsp::AudioBlock<double> base (audio);
    auto high = filters.processSamplesUp (base);
    for (std::size_t channel = 0; channel < high.getNumChannels(); ++channel)
    {
        auto* samples = high.getChannelPointer (channel);
        for (std::size_t i = 0; i < high.getNumSamples(); ++i)
            samples[i] = circuits[channel].processSample (input (start + static_cast<double> (i) / multiplier));
    }
    filters.processSamplesDown (base);
}
Render render (double rate, const Waveform& input, int factor, bool analytic, bool linear, Circuit::Parameters parameters, int length)
{
    Processor processor (rate, factor, 1, parameters, linear, analytic);
    const auto warm = static_cast<int> (std::ceil (rate / capacity)) * capacity;
    const auto count = ((warm + length + 512 + capacity - 1) / capacity) * capacity;
    std::vector<double> raw (static_cast<std::size_t> (count));
    juce::AudioBuffer<double> block (1, capacity);
    for (int start = 0; start < count; start += capacity)
    {
        if (analytic)
            processor.processAnalytic (block, input, start - warm);
        else
        {
            for (int i = 0; i < capacity; ++i) block.setSample (0, i, input (start + i - warm));
            processor.process (block);
        }
        std::copy_n (block.getReadPointer (0), capacity, raw.data() + start);
    }
    return { MagneticValidation::alignTrace (raw, warm, analytic ? processor.referenceLatency() : processor.latency(), length),
        processor.getCircuit().getDiagnostics() };
}
}
