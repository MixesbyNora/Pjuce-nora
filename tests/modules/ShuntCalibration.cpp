#include "ShuntAnalysis.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>

// Step 2: calibrate the shunt prototype to published VTB1148 data. Only
// parameter values change here; curve, circuit and solver are untouched.
// Published points and their sources are in vtb1148_reference.csv.
namespace
{
using namespace ModuleResearch;
using namespace ShuntResearch;
// Owner decision: 0 dBFS = +18 dBu, read as AES17 (a full-scale sine peak).
constexpr double fullScaleDbu = 18;
// Integer-period analysis needs exact periods; every target frequency divides 96 kHz.
constexpr double analysisRate = 96000;
double peakForDbu (double dbu) { return std::pow (10.0, (dbu - fullScaleDbu) / 20); }

// Thesis test circuit, referred to the unity primary (Woods 2019, Table 3, p.60):
// measured windings 8 ohm per primary (series 16) and 21 ohm per secondary
// (series 42, /1.7^2 = 14.5); 100 kOhm analyzer on the secondary
// (/1.7^2 = 34602 ohm); driver output impedance not stated, modeled as ~0.
constexpr double turns = 1.7;
constexpr double thesisLossOhms = 16;
constexpr double thesisLoadOhms = 100000 / (turns * turns) + 42 / (turns * turns);
// Datasheet application (Carnhill guide p.2, series/series): "200 ohm / 600 ohm" are
// the impedances seen at each side (600 / 1.7^2 = 208), not a source resistor; the
// stated +4 dB gain is the 1.7 ratio less winding loss from a low-impedance driver.
// So: ~0 ohm driver, datasheet windings (12 ohm primary, 40/1.7^2 secondary), 600 ohm load.
constexpr double datasheetSourceOhms = 0.01, datasheetLoadOhms = 600 / (turns * turns) + 40 / (turns * turns), datasheetLossOhms = 12;

enum class Metric { thdn, harmonic };
struct Target
{
    const char* id;
    Metric metric;
    double hz, primaryDbu, value; // THD+N in percent, or harmonic level in dBc.
    int order = 0;
    double noisePercentAtMinus38 = 0.20; // 1 kHz THD+N floor at -38 dBu generator.
    double gainDb = 0;
    bool upperBound = false; // Contaminated point: only exceeding it counts as error.
};
// Plot readings are estimates; see the reference CSV for figure and page.
const std::vector<Target> targets {
    { "a20-m20", Metric::thdn, 20, -20, 0.37 }, { "a20-m15", Metric::thdn, 20, -15, 0.27 },
    { "a20-m10", Metric::thdn, 20, -10, 0.20 }, { "a20-m5", Metric::thdn, 20, -5, 0.145 },
    { "a20-0", Metric::thdn, 20, 0, 0.11 }, { "a20-p4", Metric::thdn, 20, 4, 0.088 },
    { "g16-m14", Metric::thdn, 20, -14, 0.255, 0, 0.025, 16 }, { "g16-m4", Metric::thdn, 20, -4, 0.14, 0, 0.025, 16 },
    { "g16-p1", Metric::thdn, 20, 1, 0.105, 0, 0.025, 16 }, { "g16-p6", Metric::thdn, 20, 6, 0.08, 0, 0.025, 16 },
    { "f-p4-30", Metric::thdn, 30, 4, 0.068 }, { "f-p4-40", Metric::thdn, 40, 4, 0.055 },
    { "f-p4-60", Metric::thdn, 60, 4, 0.041 }, { "f-p4-80", Metric::thdn, 80, 4, 0.035 },
    { "f-p4-100", Metric::thdn, 100, 4, 0.029 }, { "f-p4-200", Metric::thdn, 200, 4, 0.015 },
    { "f-p4-500", Metric::thdn, 500, 4, 0.011 },
    { "f-m10-30", Metric::thdn, 30, -10, 0.156 }, { "f-m10-40", Metric::thdn, 40, -10, 0.128 },
    { "f-m10-60", Metric::thdn, 60, -10, 0.102 }, { "f-m10-80", Metric::thdn, 80, -10, 0.083 },
    { "f-m10-100", Metric::thdn, 100, -10, 0.071 }, { "f-m10-200", Metric::thdn, 200, -10, 0.052 },
    { "f-m10-300", Metric::thdn, 300, -10, 0.037 }, { "f-m10-500", Metric::thdn, 500, -10, 0.019 },
    { "f32-h2", Metric::harmonic, 20, -38, -67, 2 }, { "f32-h3", Metric::harmonic, 20, -38, -43, 3 },
    { "f32-h5", Metric::harmonic, 20, -38, -66, 5 },
    { "f50-h2", Metric::harmonic, 1000, 4, -100, 2 }, { "f50-h3", Metric::harmonic, 1000, 4, -83, 3 },
};
// Step 3 adds the edge of the published clean range, so saturation onset is
// constrained: the last valid 20 Hz point before driver failure (Fig.54), and
// 50 Hz at +16 dBu as an upper bound only (mains-contaminated).
std::vector<Target> step3Targets()
{
    auto result = targets;
    result.push_back ({ "g16-p7.5", Metric::thdn, 20, 7.5, 0.075, 0, 0.025, 16 });
    result.push_back ({ "g16-50-p16", Metric::thdn, 50, 16, 0.027, 0, 0.025, 16, true });
    return result;
}
// Noise/hum floor of the published THD+N, generator-referred: it falls 1:1 with level.
double noisePercent (const Target& t)
{
    return t.noisePercentAtMinus38 * std::pow (10.0, -(t.primaryDbu - t.gainDb + 38) / 20);
}

struct Sine
{
    std::vector<double> harmonicDbc; // Index = order; [1] is 0.
    double thd = 0, gainDb = 0, phaseDegrees = 0;
    int maximumIterations = 0;
    std::uint64_t failures = 0, limited = 0;
};
// Steady-state integer-period DFT of one tone through the bare module, no FIR.
// Cosine phase starts the flux integral at zero mean: a sine from rest would
// leave a DC flux offset that only decays with the circuit's long L/R.
// Called from worker threads, so no Catch2 assertions in here.
Sine measureSine (Variant variant, TransformerParameters p, double hz, double dbu)
{
    TransformerModule module;
    if (! module.prepare ({ analysisRate, 4096, 1 })) return { {}, 0, 0, 0, 0, 1, 0 };
    module.configure (variant, Topology::shunt, p);
    const auto period = static_cast<int> (std::lround (analysisRate / hz));
    const auto periods = std::max (4, static_cast<int> (std::lround (0.2 * hz)));
    const auto warm = static_cast<int> (std::ceil (0.5 * hz)) * period;
    const auto amplitude = peakForDbu (dbu);
    std::vector<double> output (static_cast<std::size_t> (period * periods));
    for (int n = 0; n < warm + period * periods; ++n)
    {
        const auto y = module.processSample (amplitude * std::cos (2 * pi * (n % period) / period), 0);
        if (n >= warm) output[static_cast<std::size_t> (n - warm)] = y;
    }
    std::vector<double> cosine (static_cast<std::size_t> (period)), sine (static_cast<std::size_t> (period));
    for (int k = 0; k < period; ++k) { cosine[static_cast<std::size_t> (k)] = std::cos (2 * pi * k / period); sine[static_cast<std::size_t> (k)] = std::sin (2 * pi * k / period); }
    const auto bin = [&] (int order)
    {
        std::complex<double> sum;
        for (std::size_t n = 0; n < output.size(); ++n)
        {
            const auto index = static_cast<std::size_t> ((static_cast<long long> (order) * static_cast<long long> (n)) % period);
            sum += output[n] * std::complex<double> (cosine[index], -sine[index]);
        }
        return 2.0 * sum / static_cast<double> (output.size());
    };
    Sine result;
    const auto fundamental = bin (1);
    const auto maximumOrder = std::max (5, static_cast<int> (20000 / hz));
    result.harmonicDbc.assign (static_cast<std::size_t> (maximumOrder + 1), -300.0);
    double power = 0;
    for (int order = 2; order <= maximumOrder; ++order)
    {
        const auto h = std::abs (bin (order)) / std::abs (fundamental);
        result.harmonicDbc[static_cast<std::size_t> (order)] = db (h);
        power += h * h;
    }
    result.thd = std::sqrt (power);
    // The stimulus is cos(), whose DFT bin is the real amplitude.
    result.gainDb = db (std::abs (fundamental) / amplitude);
    result.phaseDegrees = std::arg (fundamental) * 180 / pi;
    const auto& d = module.circuits[0].diagnostics;
    result.maximumIterations = d.maximumIterations; result.failures = d.failures; result.limited = module.safety.limited;
    return result;
}

struct Fit
{
    Variant variant;
    TransformerParameters parameters;
    double cost = 0;
    std::string label = {}; // Defaults to the variant letter.
};
std::string labelOf (const Fit& fit) { return fit.label.empty() ? std::string (name (fit.variant)) : fit.label; }
TransformerParameters thesisCircuit()
{
    TransformerParameters p;
    p.sourceOhms = 0.01; // Lower bound of the module; the driver is a low-impedance amplifier.
    p.loss = thesisLossOhms / TransformerCircuit::impedanceBase;
    p.loadOhms = thesisLoadOhms;
    return p;
}
// Parameter vector: drive calibration (dB), log saturation, log knee, memory (B only).
TransformerParameters fromVector (const std::vector<double>& x)
{
    auto p = thesisCircuit();
    p.driveDb = x[0]; p.saturation = std::exp (x[1]); p.knee = std::exp (x[2]);
    if (x.size() > 3) p.memory = std::clamp (x[3], 0.0, 1.0);
    return p;
}
double modelValue (const Target& t, const Sine& s)
{
    if (t.metric == Metric::harmonic) return s.harmonicDbc[static_cast<std::size_t> (t.order)];
    // Model is noiseless; add the published floor so both sides are THD+N.
    return std::sqrt (std::pow (100 * s.thd, 2) + std::pow (noisePercent (t), 2));
}
double errorDb (const Target& t, double model)
{
    const auto error = t.metric == Metric::harmonic ? model - t.value : db (model / t.value);
    return t.upperBound ? std::max (error, 0.0) : error;
}
std::vector<Sine> measureTargets (Variant v, const TransformerParameters& p, const std::vector<Target>& list = targets)
{
    std::vector<std::future<Sine>> jobs;
    for (const auto& t : list) jobs.push_back (std::async (std::launch::async, [=] { return measureSine (v, p, t.hz, t.primaryDbu); }));
    std::vector<Sine> result;
    for (auto& job : jobs) result.push_back (job.get());
    return result;
}
// Equal-weight RMS error in dB over all targets: misfits are not weighted away.
double rmsError (Variant v, const TransformerParameters& p, const std::vector<Target>& list)
{
    const auto values = measureTargets (v, p, list);
    double sum = 0;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (values[i].failures > 0) return 1.0e6;
        sum += std::pow (errorDb (list[i], modelValue (list[i], values[i])), 2);
    }
    return std::sqrt (sum / static_cast<double> (list.size()));
}
double cost (Variant v, const std::vector<double>& x)
{
    const auto p = fromVector (x);
    // Out-of-range values would be silently clamped by configure(); penalize instead.
    const auto& d = transformerDescriptors;
    if (p.saturation < d[0].minimum || p.saturation > d[0].maximum || p.knee < d[1].minimum || p.knee > d[1].maximum
        || p.driveDb < d[4].minimum || p.driveDb > d[4].maximum || (x.size() > 3 && (x[3] < 0 || x[3] > 1)))
        return 1.0e6;
    return rmsError (v, p, targets);
}
std::vector<double> nelderMead (const std::function<double (const std::vector<double>&)>& f, std::vector<double> start, const std::vector<double>& step, int evaluations)
{
    const auto n = start.size();
    std::vector<std::vector<double>> simplex { start };
    for (std::size_t i = 0; i < n; ++i) { auto x = start; x[i] += step[i]; simplex.push_back (x); }
    std::vector<double> values;
    for (const auto& x : simplex) values.push_back (f (x));
    for (int used = static_cast<int> (n + 1); used < evaluations;)
    {
        std::vector<std::size_t> order (n + 1);
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&] (auto a, auto b) { return values[a] < values[b]; });
        const auto best = order.front(), worst = order.back(), second = order[n - 1];
        std::vector<double> centroid (n, 0);
        for (auto i : order) if (i != worst) for (std::size_t k = 0; k < n; ++k) centroid[k] += simplex[i][k] / static_cast<double> (n);
        const auto along = [&] (double t) { std::vector<double> x (n); for (std::size_t k = 0; k < n; ++k) x[k] = centroid[k] + t * (simplex[worst][k] - centroid[k]); return x; };
        auto reflected = along (-1); auto fr = f (reflected); ++used;
        if (fr < values[best])
        {
            auto expanded = along (-2); auto fe = f (expanded); ++used;
            if (fe < fr) { simplex[worst] = expanded; values[worst] = fe; } else { simplex[worst] = reflected; values[worst] = fr; }
        }
        else if (fr < values[second]) { simplex[worst] = reflected; values[worst] = fr; }
        else
        {
            auto contracted = along (fr < values[worst] ? -0.5 : 0.5); auto fc = f (contracted); ++used;
            if (fc < std::min (fr, values[worst])) { simplex[worst] = contracted; values[worst] = fc; }
            else
                for (auto i : order)
                    if (i != best)
                    {
                        for (std::size_t k = 0; k < n; ++k) simplex[i][k] = simplex[best][k] + 0.5 * (simplex[i][k] - simplex[best][k]);
                        values[i] = f (simplex[i]); ++used;
                    }
        }
    }
    return simplex[static_cast<std::size_t> (std::min_element (values.begin(), values.end()) - values.begin())];
}
std::ofstream csv (const juce::File& root, const char* file, const char* header)
{
    std::ofstream result (root.getChildFile (file).getFullPathName().toStdString());
    result << std::setprecision (10) << header << '\n'; return result;
}
Fit fitVariant (Variant v, std::ofstream& search)
{
    const auto evaluate = [&] (const std::vector<double>& x)
    {
        const auto value = cost (v, x);
        const auto p = fromVector (x);
        search << name (v) << ',' << p.driveDb << ',' << p.saturation << ',' << p.knee << ',' << p.memory << ',' << value << '\n';
        return value;
    };
    // Coarse grid over the descriptor ranges, then a local simplex from the best few.
    std::vector<std::pair<double, std::vector<double>>> grid;
    for (double drive : { -24.0, -12.0, 0.0, 12.0, 24.0 })
        for (double saturation : { 0.25, 1.0, 4.0 })
            for (double knee : { 0.05, 0.2, 1.0 })
                for (double memory : v == Variant::smooth ? std::vector<double> { 0.0 } : std::vector<double> { 0.1, 0.35, 0.7, 1.0 })
                {
                    std::vector<double> x { drive, std::log (saturation), std::log (knee) };
                    if (v == Variant::play3) x.push_back (memory);
                    grid.emplace_back (evaluate (x), x);
                }
    std::sort (grid.begin(), grid.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    Fit best { v, {}, 1.0e30 };
    for (std::size_t start = 0; start < 3; ++start)
    {
        std::vector<double> step { 4.0, 0.5, 0.5 };
        if (v == Variant::play3) step.push_back (0.15);
        const auto x = nelderMead (evaluate, grid[start].second, step, 160);
        const auto value = cost (v, x);
        if (value < best.cost) best = { v, fromVector (x), value };
        std::cout << "vtb1148 fit " << name (v) << " start " << start << " cost " << value << std::endl;
    }
    search.flush();
    return best;
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
struct Rendered
{
    std::array<std::vector<double>, 2> audio;
    int maximumIterations = 0;
    std::uint64_t failures = 0, limited = 0;
};
// 8x through the existing research oversampler; FIR delay removed offline.
Rendered render8x (Variant v, const TransformerParameters& p, const std::function<double (int, double)>& input, double scale, int frames)
{
    constexpr int factor = 8, warm = 48000;
    Processor processor (rate, factor, 2, v, Topology::shunt, p);
    const auto latency = processor.latency();
    const auto count = ((warm + frames + 1024 + capacity - 1) / capacity) * capacity;
    std::array<std::vector<double>, 2> raw { std::vector<double> (static_cast<std::size_t> (count)), std::vector<double> (static_cast<std::size_t> (count)) };
    juce::AudioBuffer<double> block (2, capacity);
    for (int start = 0; start < count; start += capacity)
    {
        for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < capacity; ++n) block.setSample (ch, n, scale * input (ch, start + n - warm));
        processor.process (block);
        for (int ch = 0; ch < 2; ++ch) std::copy_n (block.getReadPointer (ch), capacity, raw[static_cast<std::size_t> (ch)].data() + start);
    }
    Rendered result;
    for (int ch = 0; ch < 2; ++ch) result.audio[static_cast<std::size_t> (ch)] = MagneticValidation::alignTrace (raw[static_cast<std::size_t> (ch)], warm, latency, frames).samples;
    const auto d = processor.diagnostics();
    result.maximumIterations = d.maximumIterations; result.failures = d.failures; result.limited = processor.transformer.safety.limited;
    return result;
}
std::array<double, 10> octaveBands (const std::vector<double>& x)
{
    std::size_t size = 1;
    while (size < x.size()) size <<= 1;
    std::vector<double> padded (size, 0);
    for (std::size_t n = 0; n < x.size(); ++n) padded[n] = x[n] * (0.5 - 0.5 * std::cos (2 * pi * static_cast<double> (n) / static_cast<double> (x.size())));
    const auto bins = fft (padded);
    constexpr std::array edges { 20.0, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0, 24000.0 };
    std::array<double, 10> result {};
    for (std::size_t k = 1; k < size / 2; ++k)
    {
        const auto hz = static_cast<double> (k) * rate / static_cast<double> (size);
        for (std::size_t b = 0; b < result.size(); ++b) if (hz >= edges[b] && hz < edges[b + 1]) result[b] += std::norm (bins[k]);
    }
    for (auto& value : result) value = powerDb (value);
    return result;
}
double rms (const std::array<std::vector<double>, 2>& audio)
{
    double sum = 0; std::size_t count = 0;
    for (const auto& channel : audio) for (auto x : channel) { sum += x * x; ++count; }
    return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (count, 1)));
}
// Residual after a least-squares gain match to the (already delay-aligned) input.
double residualRatioDb (const std::array<std::vector<double>, 2>& out, const std::array<std::vector<double>, 2>& in)
{
    double cross = 0, inputPower = 0, outputPower = 0, residual = 0;
    for (std::size_t ch = 0; ch < 2; ++ch) for (std::size_t n = 0; n < in[ch].size(); ++n) { cross += out[ch][n] * in[ch][n]; inputPower += in[ch][n] * in[ch][n]; outputPower += out[ch][n] * out[ch][n]; }
    const auto gain = cross / std::max (inputPower, 1.0e-30);
    for (std::size_t ch = 0; ch < 2; ++ch) for (std::size_t n = 0; n < in[ch].size(); ++n) residual += std::pow (out[ch][n] - gain * in[ch][n], 2);
    return powerDb (residual / std::max (outputPower, 1.0e-30));
}

void writeFitRows (std::ofstream& out, const std::vector<Fit>& fits, const std::vector<Target>& list)
{
    for (const auto& fit : fits)
    {
        const auto values = measureTargets (fit.variant, fit.parameters, list);
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            const auto& t = list[i]; const auto model = modelValue (t, values[i]);
            out << labelOf (fit) << ',' << t.id << ',' << (t.metric == Metric::thdn ? (t.upperBound ? "thd+n_percent_upper_bound" : "thd+n_percent") : "harmonic_dbc") << ',' << t.hz << ',' << t.primaryDbu << ',' << t.value << ',' << model << ',' << 100 * values[i].thd << ','
                << errorDb (t, model) << ',' << (t.metric == Metric::thdn ? noisePercent (t) : 0.0) << ',' << values[i].maximumIterations << ',' << values[i].failures << '\n';
            CHECK (values[i].failures == 0);
        }
    }
}
// Level sweep and application comparison at the fitted values, beyond the
// published range too (marked), so the owner can see where the model goes.
void writeLevelSweep (const juce::File& root, const char* file, const std::vector<Fit>& fits)
{
    auto levelSweep = csv (root, file, "variant,circuit,frequency_hz,primary_dbu,dbfs,published_range,thd_percent,h2_dbc,h3_dbc,h5_dbc,gain_db,phase_degrees,maximum_iterations,solver_failures,limited_inputs");
    for (const auto& fit : fits)
        for (int circuit = 0; circuit < 2; ++circuit)
        {
            auto p = fit.parameters;
            if (circuit == 1) { p.sourceOhms = datasheetSourceOhms; p.loss = datasheetLossOhms / TransformerCircuit::impedanceBase; p.loadOhms = datasheetLoadOhms; }
            for (double hz : { 20.0, 50.0, 100.0, 1000.0 })
                for (double dbu = -38; dbu <= 24.01; dbu += 2)
                {
                    const auto s = measureSine (fit.variant, p, hz, dbu);
                    levelSweep << labelOf (fit) << ',' << (circuit == 0 ? "thesis" : "datasheet-600-load") << ',' << hz << ',' << dbu << ',' << dbu - fullScaleDbu << ',' << (dbu <= (hz < 50 ? 7.5 : 16) ? 1 : 0) << ',' << 100 * s.thd << ','
                          << s.harmonicDbc[2] << ',' << s.harmonicDbc[3] << ',' << s.harmonicDbc[5] << ',' << s.gainDb << ',' << s.phaseDegrees << ',' << s.maximumIterations << ',' << s.failures << ',' << s.limited << '\n';
                    CHECK (s.failures == 0);
                }
            levelSweep.flush();
        }
    REQUIRE (levelSweep.good());
}
// Small-signal LF response at the thesis circuit, versus 1 kHz.
void writeLfResponse (const juce::File& root, const char* file, const std::vector<Fit>& fits)
{
    auto lf = csv (root, file, "variant,frequency_hz,primary_dbu,gain_rel_1k_db,phase_degrees,published");
    for (const auto& fit : fits)
    {
        const auto reference = measureSine (fit.variant, fit.parameters, 1000, -20);
        for (double hz : { 20.0, 30.0, 50.0, 100.0 })
        {
            const auto s = measureSine (fit.variant, fit.parameters, hz, -20);
            lf << labelOf (fit) << ',' << hz << ",-20," << s.gainDb - reference.gainDb << ',' << s.phaseDegrees - reference.phaseDegrees << ',' << (hz < 25 ? "loss <0.5 dB; phase about -5 to -10 degrees (Fig.28-29)" : "") << '\n';
        }
    }
    REQUIRE (lf.good());
}
// Renders: existing synthetic material at nominal level (-18 dBFS RMS = 0 dBu).
void writeRenders (const juce::File& root, const char* folder, const char* file, const std::vector<Fit>& fits)
{
    const auto audioRoot = root.getChildFile (folder); REQUIRE (audioRoot.createDirectory().wasOk());
    auto renders = csv (root, file, "file,material,model,extra_drive_db,applied_drive_db,matched_gain,band_20_63_db,band_63_125_db,band_125_250_db,band_250_500_db,band_500_1k_db,band_1k_2k_db,band_2k_4k_db,band_4k_8k_db,band_8k_16k_db,band_16k_24k_db,residual_to_output_db,maximum_iterations,solver_failures,limited_inputs");
    for (int materialIndex = 0; materialIndex < 4; ++materialIndex)
    {
        const auto label = materialIndex == 0 ? "bass" : materialIndex == 1 ? "drums" : materialIndex == 2 ? "vocal-like" : "log-sweep";
        const auto frames = static_cast<int> (materialIndex == 3 ? sweepSeconds * rate : rate);
        const std::function<double (int, double)> input = [=] (int ch, double n) { return materialIndex == 3 ? sweep (n) : material (materialIndex, ch, n); };
        std::array<std::vector<double>, 2> dry;
        for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < frames; ++n) dry[static_cast<std::size_t> (ch)].push_back (input (ch, n));
        const auto scale = std::pow (10.0, -18.0 / 20) / rms (dry);
        for (auto& channel : dry) for (auto& x : channel) x *= scale;
        struct Clip { std::string stem, model; double extra = 0, applied = 0; Rendered r; };
        std::vector<Clip> files { { std::string (label) + "_input_nominal", "input", 0, 0, { dry, 0, 0, 0 } } };
        for (const auto& fit : fits)
            for (double extra : { 0.0, 12.0, 18.0 })
            {
                auto p = fit.parameters; p.driveDb += extra;
                // configure() clamps drive to the descriptor range; report what is applied.
                p.driveDb = std::min (p.driveDb, transformerDescriptors[4].maximum);
                auto r = render8x (fit.variant, p, input, scale, frames);
                CHECK (r.failures == 0);
                const auto stem = std::string (label) + "_" + labelOf (fit) + "_shunt_8x_vtb1148_drive+" + std::to_string (static_cast<int> (extra)) + "dB";
                files.push_back ({ stem, labelOf (fit), extra, p.driveDb, std::move (r) });
            }
        const auto inputBands = octaveBands (dry[0]);
        const auto inputRms = rms (dry);
        double target = 0.1;
        for (const auto& f : files) { double peak = 0; for (const auto& c : f.r.audio) for (auto x : c) peak = std::max (peak, std::abs (x)); target = std::min (target, 0.9 * rms (f.r.audio) / std::max (peak, 1.0e-30)); }
        for (const auto& f : files)
        {
            const auto gain = target / std::max (rms (f.r.audio), 1.0e-30);
            writeWav (audioRoot.getChildFile (juce::String (f.stem) + ".wav"), f.r.audio, gain);
            const auto bands = octaveBands (f.r.audio[0]);
            const auto levelOffset = 20 * std::log10 (inputRms / std::max (rms (f.r.audio), 1.0e-30));
            renders << folder << '/' << f.stem << ".wav," << label << ',' << f.model << ',' << f.extra << ',' << f.applied << ',' << gain;
            for (std::size_t b = 0; b < bands.size(); ++b) renders << ',' << bands[b] + levelOffset - inputBands[b];
            renders << ',' << (f.model == "input" ? -300.0 : residualRatioDb (f.r.audio, dry)) << ',' << f.r.maximumIterations << ',' << f.r.failures << ',' << f.r.limited << '\n';
        }
        renders.flush();
        std::cout << "vtb1148 renders " << label << std::endl;
    }
    REQUIRE (renders.good());
}
const char* fitHeader = "variant,target,metric,frequency_hz,primary_dbu,published,model,model_thd_only_percent,error_db,noise_percent,maximum_iterations,solver_failures";

void calibrationReport (const juce::File& root)
{
    auto search = csv (root, "step2_search.csv", "variant,drive_calibration_db,saturation,knee,memory,rms_error_db");
    std::vector<Fit> fits;
    for (auto v : { Variant::smooth, Variant::play3 }) fits.push_back (fitVariant (v, search));
    REQUIRE (search.good());

    auto parameters = csv (root, "step2_parameters.csv", "variant,drive_calibration_db,saturation,knee,memory,loss,source_ohms,load_ohms,rms_error_db,at_descriptor_bound");
    for (const auto& fit : fits)
    {
        const auto& p = fit.parameters; const auto& d = transformerDescriptors;
        const auto atBound = std::abs (p.driveDb - d[4].minimum) < 0.05 || std::abs (p.driveDb - d[4].maximum) < 0.05
            || p.saturation < d[0].minimum * 1.01 || p.saturation > d[0].maximum * 0.99 || p.knee < d[1].minimum * 1.01 || p.knee > d[1].maximum * 0.99;
        parameters << name (fit.variant) << ',' << p.driveDb << ',' << p.saturation << ',' << p.knee << ',' << (fit.variant == Variant::play3 ? p.memory : 0.0) << ',' << p.loss << ',' << p.sourceOhms << ',' << p.loadOhms << ',' << fit.cost << ',' << atBound << '\n';
    }
    auto fitOut = csv (root, "step2_fit.csv", fitHeader);
    writeFitRows (fitOut, fits, targets);
    REQUIRE (parameters.good()); REQUIRE (fitOut.good());
    writeLevelSweep (root, "step2_level_sweep.csv", fits);
    writeLfResponse (root, "step2_lf_response.csv", fits);
    writeRenders (root, "step2_audio", "step2_renders.csv", fits);
}

// Step 3, variant C. Saturation is fixed at 1 during the shape fit: (saturation,
// knee, width, drive) scaled together leave the curve unchanged, so one of them
// is redundant. Onset is then moved along (saturation, knee) x s, which keeps
// the small-signal slope and hysteresis and only changes the flux capacity.
TransformerParameters fromStep3 (const std::vector<double>& x, double asymmetry = 0, double onsetScale = 1)
{
    auto p = thesisCircuit();
    p.driveDb = x[0]; p.saturation = onsetScale; p.knee = std::exp (x[1]) * onsetScale;
    p.width = std::exp (x[2]); p.memory = std::clamp (x[3], 0.0, 1.0); p.asymmetry = asymmetry;
    return p;
}
bool inDescriptorRange (const TransformerParameters& p)
{
    const auto& d = transformerDescriptors;
    const std::array values { p.saturation, p.knee, p.memory, p.loss, p.driveDb, p.width, p.asymmetry };
    for (std::size_t i = 0; i < values.size(); ++i) if (values[i] < d[i].minimum || values[i] > d[i].maximum) return false;
    return true;
}
// Level (dBu) at which THD first reaches 1%, by linear interpolation in dB; NaN if never by +36 dBu.
double onePercentLevel (const TransformerParameters& p, double hz)
{
    double previousLevel = -10, previous = 100 * measureSine (Variant::play3Outer, p, hz, previousLevel).thd;
    for (double dbu = -9; dbu <= 36.01; dbu += 1)
    {
        const auto value = 100 * measureSine (Variant::play3Outer, p, hz, dbu).thd;
        if (value >= 1 && previous < 1) return previousLevel + (dbu - previousLevel) * (0 - db (previous / 100) + db (0.01)) / (db (value / 100) - db (previous / 100));
        previous = value; previousLevel = dbu;
    }
    return std::numeric_limits<double>::quiet_NaN();
}
void step3Report (const juce::File& root)
{
    const auto all = step3Targets();
    std::vector<Target> symmetric;
    for (const auto& t : all) if (! (t.metric == Metric::harmonic && t.order == 2)) symmetric.push_back (t);

    // Stage 1: symmetric C shape fit (even-harmonic targets excluded; a symmetric curve cannot make them).
    auto search = csv (root, "step3_search.csv", "stage,drive_calibration_db,saturation,knee,width,memory,asymmetry,rms_error_db");
    const auto evaluate = [&] (const std::vector<double>& x)
    {
        const auto p = fromStep3 (x);
        const auto value = inDescriptorRange (p) ? rmsError (Variant::play3Outer, p, symmetric) : 1.0e6;
        search << "shape," << p.driveDb << ',' << p.saturation << ',' << p.knee << ',' << p.width << ',' << p.memory << ',' << p.asymmetry << ',' << value << '\n';
        return value;
    };
    std::vector<std::pair<double, std::vector<double>>> grid;
    for (double drive : { -36.0, -24.0, -12.0, 0.0, 12.0, 24.0 })
        for (double knee : { 0.1, 0.5, 1.5 })
            for (double width : { 0.01, 0.1, 1.0 })
                for (double memory : { 0.3, 0.7, 1.0 })
                {
                    std::vector<double> x { drive, std::log (knee), std::log (width), memory };
                    grid.emplace_back (evaluate (x), x);
                }
    std::sort (grid.begin(), grid.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<double> shape; double shapeCost = 1.0e30;
    for (std::size_t start = 0; start < 3; ++start)
    {
        const auto x = nelderMead (evaluate, grid[start].second, { 4.0, 0.5, 0.7, 0.15 }, 220);
        const auto value = evaluate (x);
        if (value < shapeCost) { shapeCost = value; shape = x; }
        std::cout << "vtb1148 step3 shape start " << start << " cost " << value << std::endl;
    }
    search.flush();

    // Onset scan: cost over the published range and the 1% THD level versus s.
    auto onset = csv (root, "step3_onset_scan.csv", "onset_scale,saturation,knee,in_descriptor_range,rms_error_db,thd1_level_20hz_dbu,thd1_level_50hz_dbu");
    std::vector<std::pair<double, double>> scan;
    for (double s = 0.1; s <= 16.01; s *= std::pow (2.0, 0.25))
    {
        const auto p = fromStep3 (shape, 0, s);
        const auto inRange = inDescriptorRange (p);
        const auto value = inRange ? rmsError (Variant::play3Outer, p, symmetric) : 1.0e6;
        onset << s << ',' << p.saturation << ',' << p.knee << ',' << inRange << ',' << value << ',' << onePercentLevel (p, 20) << ',' << onePercentLevel (p, 50) << '\n';
        if (inRange) scan.emplace_back (s, value);
        onset.flush();
    }
    REQUIRE (! scan.empty());
    // Earliest onset the published data allows: smallest s within 0.25 dB of the best scanned error.
    const auto best = std::min_element (scan.begin(), scan.end(), [] (const auto& a, const auto& b) { return a.second < b.second; })->second;
    double chosen = scan.back().first;
    for (const auto& [s, value] : scan) if (value <= best + 0.25) { chosen = s; break; }

    // Stage 2: asymmetry for even harmonics, all other values held.
    auto asym = csv (root, "step3_asymmetry_scan.csv", "asymmetry,rms_error_db_all_targets,h2_20hz_m38_dbc,h2_1khz_p4_dbc");
    double bestAsymmetry = 0, bestAsymmetryCost = rmsError (Variant::play3Outer, fromStep3 (shape, 0, chosen), all);
    for (double asymmetry = 0.001; asymmetry <= 0.9; asymmetry *= std::pow (10.0, 0.1))
    {
        const auto p = fromStep3 (shape, asymmetry, chosen);
        if (! inDescriptorRange (p)) continue;
        const auto value = rmsError (Variant::play3Outer, p, all);
        asym << p.asymmetry << ',' << value << ',' << measureSine (Variant::play3Outer, p, 20, -38).harmonicDbc[2] << ',' << measureSine (Variant::play3Outer, p, 1000, 4).harmonicDbc[2] << '\n';
        if (value < bestAsymmetryCost) { bestAsymmetryCost = value; bestAsymmetry = p.asymmetry; }
    }
    REQUIRE (asym.good());

    // Final values and the Step 2 B fit for comparison.
    TransformerParameters b = thesisCircuit();
    b.driveDb = 9.8368043; b.saturation = 1.446579206; b.knee = 0.06138119471; b.memory = 0.9888762544; // step2_parameters.csv
    const std::vector<Fit> fits {
        { Variant::play3, b, rmsError (Variant::play3, b, all), "B" },
        { Variant::play3Outer, fromStep3 (shape, 0, chosen), rmsError (Variant::play3Outer, fromStep3 (shape, 0, chosen), all), "C-sym" },
        { Variant::play3Outer, fromStep3 (shape, bestAsymmetry, chosen), bestAsymmetryCost, "C" },
    };
    auto parameters = csv (root, "step3_parameters.csv", "variant,drive_calibration_db,saturation,knee,width,memory,asymmetry,loss,source_ohms,load_ohms,rms_error_db_all_targets,onset_scale,thd1_level_20hz_dbu,thd1_level_50hz_dbu");
    for (const auto& fit : fits)
    {
        const auto& p = fit.parameters;
        const auto c = fit.variant == Variant::play3Outer;
        parameters << labelOf (fit) << ',' << p.driveDb << ',' << p.saturation << ',' << p.knee << ',' << (c ? p.width : 1.0) << ',' << p.memory << ',' << (c ? p.asymmetry : 0.0) << ',' << p.loss << ',' << p.sourceOhms << ',' << p.loadOhms << ',' << fit.cost << ','
                   << (c ? chosen : 0.0) << ',' << (c ? onePercentLevel (p, 20) : 0.0) << ',' << (c ? onePercentLevel (p, 50) : 0.0) << '\n';
    }
    REQUIRE (parameters.good());
    auto fitOut = csv (root, "step3_fit.csv", fitHeader);
    writeFitRows (fitOut, fits, all);
    REQUIRE (fitOut.good());
    writeLevelSweep (root, "step3_level_sweep.csv", fits);
    writeLfResponse (root, "step3_lf_response.csv", fits);
    writeRenders (root, "step3_audio", "step3_renders.csv", { fits[0], fits[2] });
}
}

TEST_CASE ("Integer-period harmonic measurement recovers a known transfer", "[shunt-calibration]")
{
    // A near-linear module at moderate level: unity small-signal gain, no failures,
    // and harmonics far below the fundamental.
    auto p = thesisCircuit(); p.saturation = 0.1; p.knee = 2.0;
    const auto s = measureSine (Variant::smooth, p, 1000, -20);
    CHECK (std::abs (s.gainDb) < 0.1);
    CHECK (s.thd < 1.0e-4);
    CHECK (s.failures == 0);
    CHECK (std::abs (peakForDbu (18) - 1.0) < 1.0e-15);
}

TEST_CASE ("Variant C reduces to A without memory and makes even harmonics only with asymmetry", "[shunt-calibration]")
{
    auto p = thesisCircuit(); p.memory = 0; p.knee = 0.2; p.driveDb = 12;
    const auto a = measureSine (Variant::smooth, p, 20, 4), c = measureSine (Variant::play3Outer, p, 20, 4);
    CHECK (std::abs (a.thd - c.thd) <= 1.0e-9 * a.thd + 1.0e-15);
    CHECK (std::abs (a.gainDb - c.gainDb) < 1.0e-9);
    p.memory = 0.7; p.width = 0.05;
    const auto symmetric = measureSine (Variant::play3Outer, p, 20, -10);
    p.asymmetry = 0.2;
    const auto biased = measureSine (Variant::play3Outer, p, 20, -10);
    // Residual H2 without bias comes from the start-up history (about -124 dBc here).
    CHECK (symmetric.harmonicDbc[2] < -110);
    CHECK (biased.harmonicDbc[2] > symmetric.harmonicDbc[2] + 20);
    CHECK (symmetric.failures == 0); CHECK (biased.failures == 0);
}

TEST_CASE ("Export VTB1148 shunt calibration", "[shunt-report]")
{
    const auto* taskValue = std::getenv ("LUMIERE_MODULE_TASK");
    const std::string task = taskValue ? taskValue : "";
    if (task != "shunt-step2" && task != "shunt-step3") { SUCCEED ("Explicit shunt calibration opt-in"); return; }
    const auto* directory = std::getenv ("LUMIERE_MODULE_REPORT_DIR"); REQUIRE (directory != nullptr);
    const juce::File root (juce::String::fromUTF8 (directory)); REQUIRE (root.createDirectory().wasOk());
    if (task == "shunt-step2") calibrationReport (root);
    else step3Report (root);
}
