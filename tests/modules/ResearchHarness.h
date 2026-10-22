#pragma once
#include "OfflineCoupledReference.h"
#include "../helpers/MagneticValidation.h"
#include <dsp/MagneticOversampling.h>
#include <functional>

namespace ModuleResearch
{
using namespace ModuleSpike;
using Waveform = std::function<double (double)>;
enum class Coupling { transformer, buffered, divider, loadAware, reference };
inline const char* name (Variant v) { return v == Variant::smooth ? "A" : "B"; }
inline const char* name (Topology t) { return t == Topology::shunt ? "shunt" : "series"; }
inline const char* name (Coupling c)
{
    switch (c)
    {
        case Coupling::transformer: return "transformer";
        case Coupling::buffered: return "1";
        case Coupling::divider: return "2";
        case Coupling::loadAware: return "2b";
        case Coupling::reference: return "3-offline";
    }
    return "unknown";
}
inline constexpr int capacity = 256;
// Periodic spectra must be conditioned in elapsed time, not a fixed number of
// base-rate frames. One second is checked against two seconds in Step 1; this
// is a measurement policy, not a model/tail guarantee for arbitrary settings.
inline int periodicWarmFrames (double sampleRate) noexcept
{
    return static_cast<int> (std::ceil (sampleRate / capacity)) * capacity;
}
// Offline/JUCE filter owner. Allocates only in constructor, not processing.
// Kernels remain allocation-free; oversampling is outside the module contract.
class Processor
{
public:
    Processor (double rate, int factor, int channelCount, Variant v, Topology t,
        TransformerParameters p = {}, Coupling c = Coupling::transformer, double sourceOhms = 1000)
        : filters (static_cast<std::size_t> (channelCount)), channels (channelCount), multiplier (factor), sampleRate (rate),
          variant (v), topology (t), parameters (p), coupling (c), resistance (sourceOhms)
    {
        filters.clearOversamplingStages();
        if (factor == 1) filters.addDummyOversamplingStage();
        std::size_t index = 0;
        for (int stage = 1; stage < factor; stage *= 2, ++index)
        {
            const auto up = index < MagneticAudio::upWidths.size() ? MagneticAudio::upWidths[index] : 0.08;
            const auto down = index < MagneticAudio::downWidths.size() ? MagneticAudio::downWidths[index] : 0.08;
            filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
                static_cast<float> (up), -120.0f, static_cast<float> (down), -120.0f);
        }
        filters.setUsingIntegerLatency (false); filters.initProcessing (capacity);
        // Measure only the analytic-reference decimator's centroid.
        juce::AudioBuffer<double> block (channels, capacity);
        auto sum = 0.0, moment = 0.0;
        for (int start = 0; start < 1024; start += capacity)
        {
            block.clear(); juce::dsp::AudioBlock<double> base (block);
            auto high = filters.processSamplesUp (base); high.clear();
            if (start == 0) high.setSample (0, 0, static_cast<double> (factor));
            filters.processSamplesDown (base);
            for (int n = 0; n < capacity; ++n) { sum += block.getSample (0, n); moment += (start + n) * block.getSample (0, n); }
        }
        decimatorLatency = moment / sum;
        transformer.prepare ({ rate * factor, 4096, channels }); source.prepare ({ rate * factor, 4096, channels });
        configure (v, t, p, c, sourceOhms); reset();
    }
    void configure (Variant v, Topology t, TransformerParameters p, Coupling c, double sourceOhms) noexcept
    {
        variant = v; topology = t; parameters = p; coupling = c; resistance = sourceOhms;
        if (c != Coupling::transformer) p.sourceOhms = 100; // Winding only; external source not counted twice.
        transformer.configure (v, t, p);
        loading = ModuleSpike::divider (Impedance::scalar (sourceOhms), transformer.info().input);
        source.configure (sourceOhms, c == Coupling::loadAware ? 1 - loading : 0);
        for (auto& reference : references) reference.prepare (sampleRate * multiplier, v, t, p, sourceOhms);
    }
    void reset() noexcept { filters.reset(); transformer.reset(); source.reset(); safety.reset(); for (auto& r : references) r.reset(); delay = {}; position = 0; }
    void clearBlockDiagnostics() noexcept
    {
        for (auto& c : transformer.circuits) c.diagnostics.maximumIterations = 0;
        for (auto& c : references) c.diagnostics.maximumIterations = 0;
    }
    SolverDiagnostics diagnostics() const noexcept
    {
        SolverDiagnostics result;
        for (int c = 0; c < channels; ++c)
        {
            const auto& d = coupling == Coupling::reference ? references[static_cast<std::size_t> (c)].diagnostics : transformer.circuits[static_cast<std::size_t> (c)].diagnostics;
            result.maximumIterations = std::max (result.maximumIterations, d.maximumIterations);
            result.failures += d.failures; result.maximumResidual = std::max (result.maximumResidual, d.maximumResidual);
        }
        return result;
    }
    double latency() const noexcept { return filters.getLatencyInSamples(); }
    double referenceLatency() const noexcept { return decimatorLatency; }
    void process (juce::AudioBuffer<double>& audio) noexcept
    {
        juce::ScopedNoDenormals noDenormals;
        juce::dsp::AudioBlock<double> entire (audio);
        for (std::size_t offset = 0; offset < entire.getNumSamples(); offset += capacity)
        {
            auto base = entire.getSubBlock (offset, std::min (static_cast<std::size_t> (capacity), entire.getNumSamples() - offset));
            if (off)
            {
                const auto length = static_cast<std::size_t> (std::llround (latency()));
                for (std::size_t n = 0; n < base.getNumSamples(); ++n)
                {
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        const auto c = static_cast<std::size_t> (ch); const auto x = base.getSample (ch, static_cast<int> (n));
                        if (length > 0) { base.setSample (ch, static_cast<int> (n), delay[c][position]); delay[c][position] = x; }
                    }
                    if (length > 0) position = (position + 1) % length;
                }
                continue;
            }
            auto high = filters.processSamplesUp (base);
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* data = high.getChannelPointer (static_cast<std::size_t> (ch));
                for (std::size_t n = 0; n < high.getNumSamples(); ++n) data[n] = sample (data[n], ch);
            }
            filters.processSamplesDown (base);
        }
    }
    void processAnalytic (juce::AudioBuffer<double>& audio, const Waveform& input, double start)
    {
        audio.clear(); juce::dsp::AudioBlock<double> base (audio); auto high = filters.processSamplesUp (base);
        for (int ch = 0; ch < channels; ++ch)
        {
            auto* data = high.getChannelPointer (static_cast<std::size_t> (ch));
            for (std::size_t n = 0; n < high.getNumSamples(); ++n) data[n] = sample (input (start + static_cast<double> (n) / multiplier), ch);
        }
        filters.processSamplesDown (base);
    }
    double sample (double x, int ch) noexcept
    {
        if (identity) return x;
        if (coupling == Coupling::transformer) x = transformer.processSample (x, ch);
        else
        {
            x = safety.process (x);
            if (coupling == Coupling::reference) x = references[static_cast<std::size_t> (ch)].process (x);
            else
            {
                x = source.processSample (x);
                if (coupling == Coupling::divider || coupling == Coupling::loadAware) x *= loading;
                x = transformer.processSample (x, ch);
            }
        }
        return secondStage ? std::tanh (2.0 * x) / 2.0 : x;
    }
    TransformerModule transformer;
    InputSafety safety;
    bool off = false, secondStage = false, identity = false;
private:
    juce::dsp::Oversampling<double> filters;
    std::array<OfflineCoupledReference, 2> references;
    SourceFixture source;
    int channels, multiplier;
    double sampleRate;
    Variant variant;
    Topology topology;
    TransformerParameters parameters;
    Coupling coupling;
    double resistance, loading = 1, decimatorLatency = 0;
    std::array<std::array<double, 512>, 2> delay {};
    std::size_t position = 0;
};
struct Render
{
    MagneticValidation::Trace trace;
    SolverDiagnostics diagnostics;
    std::uint64_t limited = 0, invalid = 0;
    bool stereoExact = true;
};
inline Render render (double rate, int factor, Variant v, Topology t, TransformerParameters p,
    const Waveform& input, bool analytic = false, Coupling coupling = Coupling::transformer,
    double sourceOhms = 1000, bool limit = true, int channels = 1, int warmFrames = 16384)
{
    Processor processor (rate, factor, channels, v, t, p, coupling, sourceOhms);
    processor.transformer.safety.enabled = limit; processor.safety.enabled = limit;
    const auto warm = ((warmFrames + capacity - 1) / capacity) * capacity;
    const auto count = ((warm + MagneticValidation::windowSize + 1024 + capacity - 1) / capacity) * capacity;
    std::vector<double> raw (static_cast<std::size_t> (count));
    juce::AudioBuffer<double> block (channels, capacity);
    bool stereoExact = true;
    for (int start = 0; start < count; start += capacity)
    {
        if (analytic) processor.processAnalytic (block, input, start - warm);
        else
        {
            for (int n = 0; n < capacity; ++n)
                for (int ch = 0; ch < channels; ++ch) block.setSample (ch, n, input (start + n - warm));
            processor.process (block);
        }
        std::copy_n (block.getReadPointer (0), capacity, raw.data() + start);
        if (channels > 1) stereoExact &= std::memcmp (block.getReadPointer (0), block.getReadPointer (1), capacity * sizeof (double)) == 0;
    }
    return { MagneticValidation::alignTrace (raw, warm, analytic ? processor.referenceLatency() : processor.latency()),
        processor.diagnostics(), processor.transformer.safety.limited + processor.safety.limited,
        processor.transformer.safety.invalid + processor.safety.invalid, stereoExact };
}
}
