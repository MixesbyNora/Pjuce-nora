#include "ShuntAnalysis.h"
#include <dsp/VtbTransformerCore.h>
#include <PluginProcessor.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>

// Owner-requested experiment (2026-10-23): lower-latency oversampling that stays
// linear phase. Option 1 = shorter half-band FIRs (passband to 20 kHz, aliasing
// folded above 20 kHz, lower stopband); option 4 = fewer stages (2x, 1x). Every
// design keeps an integer round-trip latency so dry, Mix and Delta stay sample-exact.
// Opt-in task: LUMIERE_MODULE_TASK=oversampling-options.
namespace
{
using namespace ShuntResearch;
struct Stage
{
    double upWidth, upDb, downWidth, downDb;
};
struct Design
{
    std::string name;
    std::vector<Stage> stages; // Empty = 1x.
};
// JUCE half-band FIR: passband edge = (0.5 - width) * stage rate / 2. Width
// 1/12 at the first 2x stage of 48 kHz puts the passband edge at 20 kHz and the
// stopband edge at 28 kHz, so anything that folds back lands above 20 kHz.
// The second stage only has to pass 0-28 kHz, so its transition can be wide.
constexpr double flatTo20k = 0.5 - 20000.0 / 48000.0;
std::vector<Design> designs()
{
    return {
        { "current-4x-120dB", { { 0.04, -120, 0.04, -120 }, { 0.08, -120, 0.07731219348552776, -120 } } },
        { "relaxed-4x-100dB", { { flatTo20k, -100, flatTo20k, -100 }, { 0.2, -100, 0.2, -100 } } },
        { "relaxed-4x-90dB", { { flatTo20k, -90, flatTo20k, -90 }, { 0.2, -90, 0.2, -90 } } },
        { "relaxed-4x-80dB", { { flatTo20k, -80, flatTo20k, -80 }, { 0.2, -80, 0.2, -80 } } },
        { "relaxed-2x-100dB", { { flatTo20k, -100, flatTo20k, -100 } } },
        { "relaxed-2x-90dB", { { flatTo20k, -90, flatTo20k, -90 } } },
        { "1x", {} },
    };
}
constexpr int block = 256;
std::unique_ptr<juce::dsp::Oversampling<double>> build (const std::vector<Stage>& stages, int channels = 2)
{
    auto filters = std::make_unique<juce::dsp::Oversampling<double>> (static_cast<std::size_t> (channels));
    filters->clearOversamplingStages();
    if (stages.empty()) filters->addDummyOversamplingStage();
    for (const auto& s : stages)
        filters->addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
            static_cast<float> (s.upWidth), static_cast<float> (s.upDb), static_cast<float> (s.downWidth), static_cast<float> (s.downDb));
    filters->setUsingIntegerLatency (false);
    filters->initProcessing (block);
    return filters;
}
bool integer (double latency) { return std::abs (latency - std::round (latency)) < 1.0e-9; }
// Narrow each down filter (never below its spec) until the round trip is a whole
// base-rate sample, keeping the shortest such combination.
std::vector<Stage> integerLatency (std::vector<Stage> stages)
{
    if (stages.empty() || integer (build (stages)->getLatencyInSamples())) return stages;
    auto best = stages; double bestLatency = 1.0e9;
    const auto search = [&] (auto&& self, std::size_t index, std::vector<Stage> current) -> void
    {
        if (index == current.size())
        {
            const auto latency = build (current)->getLatencyInSamples();
            if (integer (latency) && latency < bestLatency) { bestLatency = latency; best = current; }
            return;
        }
        for (int step = 0; step <= 40; ++step)
        {
            auto next = current;
            next[index].downWidth = current[index].downWidth - 0.0005 * step;
            if (next[index].downWidth < 0.005) break;
            self (self, index + 1, next);
        }
    };
    search (search, 0, stages);
    REQUIRE (bestLatency < 1.0e8);
    return best;
}
int factorOf (const std::vector<Stage>& stages) { return 1 << stages.size(); }

// Base-rate impulse response of the up/down pair alone (identity inside).
std::vector<double> impulseResponse (const std::vector<Stage>& stages, int length = 8192)
{
    auto filters = build (stages, 1);
    std::vector<double> out (static_cast<std::size_t> (length));
    juce::AudioBuffer<double> buffer (1, block);
    for (int start = 0; start < length; start += block)
    {
        buffer.clear();
        if (start == 0) buffer.setSample (0, 0, 1.0);
        juce::dsp::AudioBlock<double> base (buffer);
        filters->processSamplesUp (base);
        filters->processSamplesDown (base);
        for (int n = 0; n < block && start + n < length; ++n) out[static_cast<std::size_t> (start + n)] = buffer.getSample (0, n);
    }
    return out;
}
std::ofstream csv (const juce::File& root, const char* file, const char* header)
{
    std::ofstream result (root.getChildFile (file).getFullPathName().toStdString());
    result << std::setprecision (10) << header << '\n'; return result;
}

// Both transformers (input iron, output iron at the given extra drive) inside
// one oversampled region; the compressor is omitted because it does not
// interact with the filter choice beyond the transformers' own spectra.
struct Chain
{
    Chain (const std::vector<Stage>& stagesToUse, double extraDriveDb)
        : stages (stagesToUse), filters (build (stagesToUse)), factor (factorOf (stagesToUse))
    {
        input.prepare (rate * factor, ModuleSpike::vtb9046LineInputC(), 0.0);
        output.prepare (rate * factor, ModuleSpike::vtb1148OutputC(), extraDriveDb);
        scratch.setSize (2, block);
    }
    void process (juce::AudioBuffer<double>& audio)
    {
        for (int start = 0; start < audio.getNumSamples(); start += block)
        {
            const auto count = std::min (block, audio.getNumSamples() - start);
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < count; ++n) scratch.setSample (ch, n, audio.getSample (ch, start + n));
            auto base = juce::dsp::AudioBlock<double> (scratch).getSubBlock (0, static_cast<std::size_t> (count));
            auto high = filters->processSamplesUp (base);
            std::array<double, 2> frame {};
            for (std::size_t n = 0; n < high.getNumSamples(); ++n)
            {
                for (int ch = 0; ch < 2; ++ch) frame[static_cast<std::size_t> (ch)] = high.getSample (ch, static_cast<int> (n));
                input.processFrame (frame, 2);
                output.processFrame (frame, 2);
                for (int ch = 0; ch < 2; ++ch) high.setSample (ch, static_cast<int> (n), frame[static_cast<std::size_t> (ch)]);
            }
            filters->processSamplesDown (base);
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < count; ++n) audio.setSample (ch, start + n, scratch.getSample (ch, n));
        }
    }
    double latency() const { return filters->getLatencyInSamples(); }
    std::vector<Stage> stages;
    std::unique_ptr<juce::dsp::Oversampling<double>> filters;
    int factor;
    VtbTransformerCore input, output;
    juce::AudioBuffer<double> scratch;
};
// Render a stereo signal through a design; output aligned by its (integer) latency.
std::array<std::vector<double>, 2> renderChain (const std::vector<Stage>& stages, double extraDriveDb,
    const std::function<double (int, double)>& signal, int frames, int warm = 48000)
{
    Chain chain (stages, extraDriveDb);
    const auto latency = chain.latency();
    // Room for the latency plus the fractional-alignment kernel (the 64x reference is not integer).
    juce::AudioBuffer<double> audio (2, warm + frames + static_cast<int> (std::ceil (latency)) + 512);
    for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample (ch, n, signal (ch, n - warm));
    chain.process (audio);
    std::array<std::vector<double>, 2> result;
    for (int ch = 0; ch < 2; ++ch)
    {
        std::vector<double> raw (audio.getReadPointer (ch), audio.getReadPointer (ch) + audio.getNumSamples());
        result[static_cast<std::size_t> (ch)] = integer (latency)
            ? std::vector<double> (raw.begin() + warm + static_cast<long> (std::llround (latency)), raw.begin() + warm + static_cast<long> (std::llround (latency)) + frames)
            : MagneticValidation::alignTrace (raw, warm, latency, frames).samples;
    }
    return result;
}

void writeWav (const juce::File& path, const std::array<std::vector<double>, 2>& audio, double gain)
{
    juce::AudioBuffer<float> buffer (2, static_cast<int> (audio[0].size()));
    for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < buffer.getNumSamples(); ++n) buffer.setSample (ch, n, static_cast<float> (gain * audio[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]));
    auto stream = path.createOutputStream(); REQUIRE (stream != nullptr);
    REQUIRE (stream->setPosition (0)); REQUIRE (stream->truncate().wasOk());
    std::unique_ptr<juce::OutputStream> output = std::move (stream);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (output, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    REQUIRE (writer != nullptr); REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
}

void report (const juce::File& root)
{
    auto summary = csv (root, "oversampling_options.csv",
        "design,factor,latency_samples,latency_ms,up_down_widths,gain_10k_db,gain_15k_db,gain_18k_db,gain_20k_db,gain_21k_db,gain_22k_db,"
        "pre_ring_peak_db,pre_ring_samples_above_m100db,worst_sine_nonharmonic_dbc,worst_twotone_vs_reference_dbr,chain_cpu_percent_48k");
    // Reference for the two-tone difference: the current design at 64x.
    const std::vector<Stage> reference64 { { 0.04, -120, 0.04, -120 }, { 0.08, -120, 0.08, -120 }, { 0.08, -120, 0.08, -120 },
        { 0.08, -120, 0.08, -120 }, { 0.08, -120, 0.08, -120 }, { 0.08, -120, 0.08, -120 } };
    const auto twoToneBin = static_cast<int> (2 * std::round ((55.0 * sineSize / rate - 1) / 2) + 1);
    const auto probeBin = static_cast<int> (2 * std::round ((13001.0 * sineSize / rate - 1) / 2) + 1);
    const std::function<double (int, double)> twoTone = [&] (int, double n)
    { const auto t = n / sineSize; return 0.5 * std::sin (2 * pi * twoToneBin * t) + 0.1 * std::sin (2 * pi * probeBin * t); };
    for (auto design : designs())
    {
        design.stages = integerLatency (design.stages);
        const auto factor = factorOf (design.stages);
        const auto filters = build (design.stages);
        const auto latency = filters->getLatencyInSamples();
        REQUIRE (integer (latency));
        juce::String widths;
        for (const auto& s : design.stages) widths += juce::String (s.upWidth, 4) + "/" + juce::String (s.downWidth, 4) + " ";

        // Response and pre-ringing of the filters alone.
        const auto h = impulseResponse (design.stages);
        const auto peakIndex = static_cast<std::size_t> (std::max_element (h.begin(), h.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); }) - h.begin());
        double preRing = 0; int preSamples = 0;
        for (std::size_t n = 0; n + 2 < peakIndex; ++n)
        {
            preRing = std::max (preRing, std::abs (h[n]));
            if (std::abs (h[n]) > std::abs (h[peakIndex]) * 1.0e-5) ++preSamples;
        }
        const auto bins = fft (h);
        const auto gainAt = [&] (double hz) { return db (2.0 * std::abs (bins[static_cast<std::size_t> (std::lround (hz * static_cast<double> (h.size()) / rate))]) * static_cast<double> (h.size()) / 2.0); };

        // Aliasing: coherent sines through both transformers, +18 dB drive (hottest tested).
        double worstSine = -300;
        for (double requested : { 1000.0, 7001.0, 13001.0, 19001.0 })
        {
            const auto bin = static_cast<int> (2 * std::round ((requested * sineSize / rate - 1) / 2) + 1);
            const std::function<double (int, double)> tone = [&] (int, double n) { return 0.5 * std::sin (2 * pi * bin * n / sineSize); };
            const auto out = renderChain (design.stages, 18.0, tone, static_cast<int> (sineSize));
            const auto parts = split (out[0], out[0], bin);
            double fundamental = 0, nonHarmonic = 0;
            for (std::size_t b = 0; b < parts.size(); ++b) { fundamental += parts[b].fundamental; nonHarmonic += parts[b].nonHarmonic; }
            worstSine = std::max (worstSine, powerDb (nonHarmonic / std::max (fundamental, 1.0e-30)));
        }
        // Two-tone (55 Hz saturating the core + 13 kHz probe) against the 64x reference, in band.
        const auto candidate = renderChain (design.stages, 18.0, twoTone, static_cast<int> (sineSize));
        const auto referenceOut = renderChain (reference64, 18.0, twoTone, static_cast<int> (sineSize));
        const auto twoToneDifference = bandDifference (candidate[0], referenceOut[0]);

        // CPU of filters plus both transformers, stereo 48 kHz, 256-sample blocks.
        Chain chain (design.stages, 0.0);
        juce::AudioBuffer<double> audio (2, block);
        std::vector<double> durations;
        for (int b = -32; b < 1024; ++b)
        {
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < block; ++n) audio.setSample (ch, n, 0.5 * std::sin (2 * pi * 60.0 * (b * block + n) / rate));
            const auto t0 = std::chrono::steady_clock::now();
            chain.process (audio);
            const auto ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count();
            if (b >= 0) durations.push_back (ns);
        }
        std::sort (durations.begin(), durations.end());
        const auto cpu = durations[durations.size() / 2] * rate / (block * 1.0e7);

        summary << design.name << ',' << factor << ',' << latency << ',' << latency / rate * 1000.0 << ',' << widths << ','
                << gainAt (10000) << ',' << gainAt (15000) << ',' << gainAt (18000) << ',' << gainAt (20000) << ',' << gainAt (21000) << ',' << gainAt (22000) << ','
                << db (preRing / std::abs (h[peakIndex])) << ',' << preSamples << ',' << worstSine << ',' << twoToneDifference << ',' << cpu << '\n';
        summary.flush();
        std::cout << "oversampling " << design.name << " latency " << latency << " 20k " << gainAt (20000) << " alias " << worstSine << " cpu " << cpu << std::endl;

        // Listening renders: material at nominal level, both irons, output +0 and +12 dB.
        const auto audioRoot = root.getChildFile ("oversampling_audio"); REQUIRE (audioRoot.createDirectory().wasOk());
        for (int materialIndex = 0; materialIndex < 3; ++materialIndex)
        {
            const auto label = materialIndex == 0 ? "bass" : materialIndex == 1 ? "drums" : "vocal-like";
            std::array<std::vector<double>, 2> dry;
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < static_cast<int> (rate); ++n) dry[static_cast<std::size_t> (ch)].push_back (material (materialIndex, ch, n));
            double sum = 0; for (const auto& c : dry) for (auto x : c) sum += x * x;
            const auto scale = std::pow (10.0, -18.0 / 20) / std::sqrt (sum / (2 * rate));
            for (double extra : { 0.0, 12.0 })
            {
                const auto out = renderChain (design.stages, extra, [&] (int ch, double n) { return scale * material (materialIndex, ch, n); }, static_cast<int> (rate));
                writeWav (audioRoot.getChildFile (juce::String (label) + "_" + design.name + "_drive+" + juce::String (static_cast<int> (extra)) + "dB.wav"), out, 1.0);
            }
        }
    }
    REQUIRE (summary.good());
}
}

namespace
{
// Re-measurement of the adopted live design at each supported rate, using the
// exact live filters (MagneticAudio::prepareLiveFilters) and both transformers.
struct LiveChain
{
    LiveChain (double sampleRateToUse, int factorToUse, bool live, double extraDriveDb)
        : sampleRate (sampleRateToUse), factor (factorToUse)
    {
        if (live) MagneticAudio::prepareLiveFilters (filters, factor, sampleRate);
        else
        {
            // Steep high-rate reference: the original first stage, then 0.08 / -120 dB stages.
            filters.clearOversamplingStages();
            for (int stage = 1; stage < factor; stage *= 2)
            {
                const auto width = stage == 1 ? 0.04f : 0.08f;
                filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple, width, -120.0f, width, -120.0f);
            }
            filters.setUsingIntegerLatency (false);
            filters.initProcessing (block);
        }
        input.prepare (sampleRate * factor, ModuleSpike::vtb9046LineInputC(), 0.0);
        output.prepare (sampleRate * factor, ModuleSpike::vtb1148OutputC(), extraDriveDb);
        scratch.setSize (2, block);
    }
    std::vector<double> render (const std::function<double (double)>& signal, int frames, int warm)
    {
        const auto latency = filters.getLatencyInSamples();
        const auto total = warm + frames + static_cast<int> (std::ceil (latency)) + 512;
        std::vector<double> raw (static_cast<std::size_t> (total));
        for (int start = 0; start < total; start += block)
        {
            const auto count = std::min (block, total - start);
            for (int n = 0; n < count; ++n) { const auto x = signal (start + n - warm); scratch.setSample (0, n, x); scratch.setSample (1, n, x); }
            auto base = juce::dsp::AudioBlock<double> (scratch).getSubBlock (0, static_cast<std::size_t> (count));
            auto high = filters.processSamplesUp (base);
            std::array<double, 2> frame {};
            for (std::size_t n = 0; n < high.getNumSamples(); ++n)
            {
                for (int ch = 0; ch < 2; ++ch) frame[static_cast<std::size_t> (ch)] = high.getSample (ch, static_cast<int> (n));
                input.processFrame (frame, 2);
                output.processFrame (frame, 2);
                for (int ch = 0; ch < 2; ++ch) high.setSample (ch, static_cast<int> (n), frame[static_cast<std::size_t> (ch)]);
            }
            filters.processSamplesDown (base);
            for (int n = 0; n < count; ++n) raw[static_cast<std::size_t> (start + n)] = scratch.getSample (0, n);
        }
        if (integer (latency))
            return { raw.begin() + warm + static_cast<long> (std::llround (latency)), raw.begin() + warm + static_cast<long> (std::llround (latency)) + frames };
        return MagneticValidation::alignTrace (raw, warm, latency, frames).samples;
    }
    double sampleRate;
    int factor;
    juce::dsp::Oversampling<double> filters { 2 };
    VtbTransformerCore input, output;
    juce::AudioBuffer<double> scratch;
};
// Energy split of a coherent tone within 20 Hz-20 kHz at any rate.
std::pair<double, double> harmonicSplit (const std::vector<double>& x, int bin, double sampleRate)
{
    const auto bins = fft (x);
    double fundamental = 0, nonHarmonic = 0;
    for (std::size_t k = 1; k < x.size() / 2; ++k)
    {
        const auto hz = static_cast<double> (k) * sampleRate / static_cast<double> (x.size());
        if (hz < 20 || hz > 20000) continue;
        const auto e = 2 * std::norm (bins[k]);
        if (static_cast<int> (k) == bin) fundamental += e;
        else if (static_cast<int> (k) % bin != 0) nonHarmonic += e;
    }
    return { fundamental, nonHarmonic };
}
double bandError (const std::vector<double>& a, const std::vector<double>& b, double sampleRate)
{
    std::vector<double> error (a.size()), reference (b);
    for (std::size_t n = 0; n < a.size(); ++n) error[n] = a[n] - b[n];
    const auto e = fft (error), r = fft (reference);
    double ep = 0, rp = 0;
    for (std::size_t k = 1; k < a.size() / 2 && static_cast<double> (k) * sampleRate / static_cast<double> (a.size()) <= 20000; ++k)
    { ep += std::norm (e[k]); rp += std::norm (r[k]); }
    return powerDb (ep / std::max (rp, 1.0e-30));
}
void liveReport (const juce::File& root)
{
    auto out = csv (root, "live_design_by_rate.csv",
        "sample_rate_hz,factor,latency_samples,latency_ms,gain_10k_db,gain_15k_db,gain_18k_db,gain_20k_db,max_dev_20_20k_db,"
        "pre_ring_peak_db,pre_ring_samples_above_m100db,worst_sine_nonharmonic_dbc,twotone_vs_reference_dbr,plugin_cpu_percent_nominal");
    for (double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const auto factor = ModuleSpike::vtb1148OutputFactor (sampleRate);
        LiveChain probe (sampleRate, factor, true, 0.0);
        const auto latency = probe.filters.getLatencyInSamples();
        CHECK (integer (latency));
        // Filter-pair response and pre-ringing.
        juce::dsp::Oversampling<double> pair (1);
        MagneticAudio::prepareLiveFilters (pair, factor, sampleRate);
        std::vector<double> h (8192);
        juce::AudioBuffer<double> buffer (1, block);
        for (int start = 0; start < 8192; start += block)
        {
            buffer.clear(); if (start == 0) buffer.setSample (0, 0, 1.0);
            juce::dsp::AudioBlock<double> base (buffer);
            pair.processSamplesUp (base); pair.processSamplesDown (base);
            for (int n = 0; n < block; ++n) h[static_cast<std::size_t> (start + n)] = buffer.getSample (0, n);
        }
        const auto bins = fft (h);
        const auto gainAt = [&] (double hz) { return db (std::abs (bins[static_cast<std::size_t> (std::lround (hz * 8192.0 / sampleRate))]) * 8192.0); };
        double deviation = 0;
        for (double hz = 20; hz <= 20000; hz *= 1.01) deviation = std::max (deviation, std::abs (gainAt (hz)));
        const auto peak = static_cast<std::size_t> (std::max_element (h.begin(), h.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); }) - h.begin());
        double preRing = 0; int preSamples = 0;
        for (std::size_t n = 0; n + 2 < peak; ++n) { preRing = std::max (preRing, std::abs (h[n])); if (std::abs (h[n]) > std::abs (h[peak]) * 1.0e-5) ++preSamples; }
        // Aliasing at +18 dB output drive, coherent tones near 1k/7k/13k/19k.
        double worst = -300;
        for (double requested : { 1000.0, 7001.0, 13001.0, 19001.0 })
        {
            const auto bin = static_cast<int> (2 * std::round ((requested * sineSize / sampleRate - 1) / 2) + 1);
            LiveChain chain (sampleRate, factor, true, 18.0);
            const auto y = chain.render ([&] (double n) { return 0.5 * std::sin (2 * pi * bin * n / sineSize); }, static_cast<int> (sineSize), static_cast<int> (sampleRate));
            const auto [f, nh] = harmonicSplit (y, bin, sampleRate);
            worst = std::max (worst, powerDb (nh / std::max (f, 1.0e-30)));
        }
        // Two-tone against a steep high-rate reference (about 3 MHz internal).
        const auto lowBin = static_cast<int> (2 * std::round ((55.0 * sineSize / sampleRate - 1) / 2) + 1);
        const auto highBin = static_cast<int> (2 * std::round ((13001.0 * sineSize / sampleRate - 1) / 2) + 1);
        const std::function<double (double)> twoTone = [&] (double n) { const auto t = n / sineSize; return 0.5 * std::sin (2 * pi * lowBin * t) + 0.1 * std::sin (2 * pi * highBin * t); };
        LiveChain candidate (sampleRate, factor, true, 18.0);
        LiveChain reference (sampleRate, static_cast<int> (3072000.0 / sampleRate) >= 64 ? 64 : 16, false, 18.0);
        const auto difference = bandError (candidate.render (twoTone, static_cast<int> (sineSize), static_cast<int> (sampleRate)),
            reference.render (twoTone, static_cast<int> (sineSize), static_cast<int> (sampleRate)), sampleRate);
        // Whole plugin at nominal Amount, threshold -24 dB.
        PluginProcessor processor;
        for (auto* p : processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                if (r->paramID == "xfrmAmount") r->setValueNotifyingHost (r->convertTo0to1 (400.0f / 7.0f));
                if (r->paramID == "threshold") r->setValueNotifyingHost (r->convertTo0to1 (-24.0f));
            }
        processor.prepareToPlay (sampleRate, block);
        juce::AudioBuffer<float> audio (2, block);
        juce::MidiBuffer midi;
        std::vector<double> durations;
        for (int b = -32; b < 1024; ++b)
        {
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < block; ++n) audio.setSample (ch, n, static_cast<float> (0.5 * std::sin (2 * pi * 60.0 * (b * block + n) / sampleRate)));
            const auto t0 = std::chrono::steady_clock::now();
            processor.processBlock (audio, midi);
            const auto ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count();
            if (b >= 0) durations.push_back (ns);
        }
        std::sort (durations.begin(), durations.end());
        const auto cpu = durations[durations.size() / 2] * sampleRate / (block * 1.0e7);
        CHECK (processor.getLatencySamples() == static_cast<int> (std::llround (latency)));
        out << sampleRate << ',' << factor << ',' << latency << ',' << latency / sampleRate * 1000.0 << ',' << gainAt (10000) << ',' << gainAt (15000) << ','
            << gainAt (18000) << ',' << gainAt (20000) << ',' << deviation << ',' << (preSamples > 0 ? db (preRing / std::abs (h[peak])) : -300.0) << ','
            << preSamples << ',' << worst << ',' << difference << ',' << cpu << '\n';
        out.flush();
        std::cout << "live " << sampleRate << " latency " << latency << " dev " << deviation << " alias " << worst << " twotone " << difference << " cpu " << cpu << std::endl;
    }
    REQUIRE (out.good());
}
}

// Live design (owner decision 2026-10-23): relaxed 4x/2x at -100 dB, flat to
// 20 kHz at every rate, folding only above 20 kHz. Prints the integer-latency
// widths that MagneticOversampling.h records per rate.
TEST_CASE ("Derive live relaxed oversampling widths per sample rate", "[oversampling-design]")
{
    const auto* task = std::getenv ("LUMIERE_MODULE_TASK");
    if (task == nullptr || std::string (task) != "oversampling-design") { SUCCEED ("Explicit opt-in"); return; }
    // 44.1 kHz also with a 19.5 / 19 kHz passband, for comparison only.
    for (const auto& [base, edge] : { std::pair { 44100.0, 20000.0 }, std::pair { 44100.0, 19500.0 }, std::pair { 44100.0, 19000.0 },
             std::pair { 48000.0, 20000.0 }, std::pair { 88200.0, 20000.0 }, std::pair { 96000.0, 20000.0 } })
    {
        const auto first = 0.5 - edge / base;
        std::vector<Stage> stages { { first, -100, first, -100 } };
        if (base < 80000.0)
        {
            // Second stage passes 0 to the first stage's stopband edge.
            const auto passEdge = (0.5 + first) * base / 2.0;
            const auto second = std::min (0.2, 0.5 - passEdge / base);
            stages.push_back ({ second, -100, second, -100 });
        }
        const auto tuned = integerLatency (stages);
        std::cout << std::setprecision (17) << "design " << base << " edge " << edge;
        for (const auto& st : tuned) std::cout << " | up " << st.upWidth << " down " << st.downWidth;
        std::cout << " | latency " << build (tuned)->getLatencyInSamples() << std::endl;
    }
}

TEST_CASE ("Relaxed oversampling designs keep integer latency and a flat audio band", "[oversampling-options]")
{
    for (auto design : designs())
    {
        CAPTURE (design.name);
        design.stages = integerLatency (design.stages);
        CHECK (integer (build (design.stages)->getLatencyInSamples()));
        const auto h = impulseResponse (design.stages);
        const auto bins = fft (h);
        const auto gain1k = std::abs (bins[static_cast<std::size_t> (std::lround (1000.0 * static_cast<double> (h.size()) / rate))]);
        const auto gain20k = std::abs (bins[static_cast<std::size_t> (std::lround (20000.0 * static_cast<double> (h.size()) / rate))]);
        CHECK (std::abs (db (gain20k / gain1k)) < 0.1);
    }
}

TEST_CASE ("Export oversampling options experiment", "[oversampling-report]")
{
    const auto* task = std::getenv ("LUMIERE_MODULE_TASK");
    const std::string name = task ? task : "";
    if (name != "oversampling-options" && name != "oversampling-live") { SUCCEED ("Explicit opt-in"); return; }
    const auto* directory = std::getenv ("LUMIERE_MODULE_REPORT_DIR"); REQUIRE (directory != nullptr);
    const juce::File root (juce::String::fromUTF8 (directory)); REQUIRE (root.createDirectory().wasOk());
    if (name == "oversampling-options") report (root);
    else liveReport (root);
}
