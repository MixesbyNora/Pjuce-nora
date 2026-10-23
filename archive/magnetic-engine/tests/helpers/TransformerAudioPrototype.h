#pragma once

#include "MagneticValidation.h"
#include <experimental/TransformerAudioCircuit.h>
#include <functional>

namespace TransformerAudioPrototype
{
using Circuit = MagneticPrototype::TransformerAudioCircuit;
using Waveform = std::function<double (double)>; // Offline render callback only.
inline constexpr int capacity = 256;

class Processor
{
public:
    Processor (double rate, int factor = MagneticPrototype::AudioSettings::oversamplingFactor,
        int channels = 1, Circuit::Parameters = {}, bool linear = false, bool exact = false);
    void reset() noexcept;
    void process (juce::AudioBuffer<double>&) noexcept;
    void processAnalytic (juce::AudioBuffer<double>&, const Waveform&, double start);
    double latency() const noexcept { return filters.getLatencyInSamples(); }
    double referenceLatency() const noexcept { return decimatorLatency; }
    const Circuit& getCircuit (std::size_t channel = 0) const noexcept { return circuits[channel]; }

private:
    juce::dsp::Oversampling<double> filters;
    std::array<Circuit, 2> circuits;
    double decimatorLatency = 0.0;
    int multiplier;
};

struct Render
{
    MagneticValidation::Trace trace;
    Circuit::Diagnostics diagnostics;
};
Render render (double rate, const Waveform&, int factor, bool analytic = false, bool linear = false,
    Circuit::Parameters = {}, int length = MagneticValidation::windowSize);
}
