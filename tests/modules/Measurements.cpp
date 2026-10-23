#include "ResearchHarness.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace
{
using namespace ModuleResearch;
using namespace MagneticValidation;
constexpr double pi = juce::MathConstants<double>::pi;
double db (double a) { return 20 * std::log10 (std::max (std::abs (a), 1.0e-15)); }
std::ofstream csv (const juce::File& root, const char* path, const char* header)
{
    std::ofstream out (root.getChildFile (path).getFullPathName().toStdString());
    out << std::setprecision (17) << header << '\n'; return out;
}
double amplitude (const Trace& trace, double frequency, double rate)
{
    std::complex<double> value {};
    for (std::size_t n = 0; n < trace.samples.size(); ++n)
        value += trace.samples[n] * std::polar (1.0, -2 * pi * frequency * static_cast<double> (n) / rate);
    return 2 * std::abs (value) / static_cast<double> (trace.samples.size());
}
double difference (const Trace& a, const Trace& b)
{
    double error = 0, power = 0;
    for (std::size_t n = 0; n < a.samples.size(); ++n) { const auto d = a.samples[n] - b.samples[n]; error += d * d; power += b.samples[n] * b.samples[n]; }
    return db (std::sqrt (error / std::max (power, 1.0e-30)));
}
struct Timing { double median = 0, p999 = 0, worst = 0, checksum = 0; int iterations = 0; };
template <typename Process, typename Diagnostic>
Timing timing (double rate, int channels, double peak, Process&& process, Diagnostic&& diagnostic)
{
    constexpr int blocks = 1024;
    juce::AudioBuffer<double> audio (channels, capacity), input (channels, 4096);
    for (int c = 0; c < channels; ++c)
        for (int n = 0; n < input.getNumSamples(); ++n)
            input.setSample (c, n, peak * (0.95 * std::sin (2 * pi * (c == 0 ? 60 : 73) * n / rate) + 0.05 * std::sin (2 * pi * 2000 * n / rate)));
    std::array<double, blocks> durations {};
    Timing result;
    for (int block = -32; block < blocks; ++block)
    {
        for (int c = 0; c < channels; ++c) audio.copyFrom (c, 0, input, c, ((block + 32) * capacity) % 4096, capacity);
        const auto start = std::chrono::steady_clock::now();
        process (audio);
        const auto elapsed = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count();
        if (block >= 0) durations[static_cast<std::size_t> (block)] = elapsed;
        result.checksum += audio.getSample (0, 127);
        result.iterations = std::max (result.iterations, diagnostic());
    }
    std::sort (durations.begin(), durations.end());
    result.median = durations[blocks / 2]; result.p999 = durations[static_cast<std::size_t> (std::ceil (0.999 * blocks) - 1)]; result.worst = durations.back();
    return result;
}
void cpuReport (const juce::File& root)
{
    auto out = csv (root, "cpu.csv", "kind,model,topology,coupling,rate_hz,channels,workload,factor,latency_samples,median_block_ns,p999_block_ns,worst_block_ns,median_realtime_percent,maximum_iterations,solver_failures,checksum");
    auto row = [&] (const char* model, const char* topology, const char* coupling, double rate, int channels, int workload, int factor, double latency, Timing t, std::uint64_t failures)
    {
        out << "measurement," << model << ',' << topology << ',' << coupling << ',' << rate << ',' << channels << ',' << workload << ',' << factor << ',' << latency << ','
            << t.median << ',' << t.p999 << ',' << t.worst << ',' << t.median * rate / (capacity * 1.0e7) << ',' << t.iterations << ',' << failures << ',' << t.checksum << '\n'; out.flush();
        CHECK (isFinite (t.checksum)); CHECK (failures == 0);
    };
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
            for (int workload : { 0, 1 })
            {
                const auto peak = workload == 0 ? 0.7 : 16.0;
                TransformerParameters parameters; parameters.driveDb = workload == 0 ? 0 : 12;
                for (auto v : { Variant::smooth, Variant::play3 })
                    for (auto topology : { Topology::shunt, Topology::series })
                    {
                        const auto factor = v == Variant::smooth ? 4 : 8;
                        Processor p (rate, factor, channels, v, topology, parameters);
                        const auto time = timing (rate, channels, peak, [&] (auto& audio) { p.clearBlockDiagnostics(); p.process (audio); }, [&] { return p.diagnostics().maximumIterations; });
                        row (name (v), name (topology), "transformer", rate, channels, workload, factor, p.latency(), time, p.diagnostics().failures);
                    }
                // The retired Magnetic ("shipped") stage was timed here; see archive/magnetic-engine.
                Processor off (rate, 8, channels, Variant::smooth, Topology::shunt); off.off = true;
                row ("Off", "none", "none", rate, channels, workload, 8, off.latency(), timing (rate, channels, peak, [&] (auto& a) { off.process (a); }, [] { return 0; }), 0);
                row ("empty", "none", "none", rate, channels, workload, 1, 0, timing (rate, channels, peak, [] (auto&) {}, [] { return 0; }), 0);
            }
    // The two-way column measures offline reference cost, never a live candidate.
    for (double rate : { 48000.0, 96000.0, 192000.0 })
        for (auto v : { Variant::smooth, Variant::play3 })
            for (auto topology : { Topology::shunt, Topology::series })
                for (auto coupling : { Coupling::buffered, Coupling::divider, Coupling::loadAware, Coupling::reference })
                {
                    const auto factor = coupling == Coupling::reference ? 64 : 8;
                    Processor p (rate, factor, 2, v, topology, {}, coupling);
                    const auto time = timing (rate, 2, 0.7, [&] (auto& audio) { p.clearBlockDiagnostics(); p.process (audio); }, [&] { return p.diagnostics().maximumIterations; });
                    row (name (v), name (topology), name (coupling), rate, 2, 0, factor, p.latency(), time, p.diagnostics().failures);
                }
    REQUIRE (out.good());
}
bool converged (const Metrics& m, const Stimulus& s)
{
    return m.bandResidualDb <= referenceGoalDb && m.bandMaximumError <= 1.0e-4 && (! s.silentTail() || m.tailErrorDb <= referenceGoalDb);
}
void validationReport (const juce::File& root)
{
    auto out = csv (root, "validation.csv", "kind,variant,topology,rate_hz,channels,signal,peak,factor,reference_factor,reference_converged,reference_band_dbr,reference_maximum_error,reference_tail_dbr,band_dbr,peak_error_db,off_grid_dbr,tail_dbr,solver_failures,maximum_iterations,maximum_residual,stereo_exact,pass");
    for (auto v : { Variant::smooth, Variant::play3 })
        for (auto topology : { Topology::shunt, Topology::series })
            for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (auto signal : signals)
                    for (double peak : { 0.05, 1.0, 4.0 })
                    {
                        const Stimulus s (signal, rate, peak);
                        const auto warm = s.periodic() ? periodicWarmFrames (rate) : signal == Signal::impulse ? windowSize : 1024;
                        const Waveform wave = [&] (double n) { return s.at (n); };
                        auto lower = render (rate, 32, v, topology, {}, wave, true, Coupling::transformer, 1000, true, 1, warm);
                        auto reference = render (rate, 64, v, topology, {}, wave, true, Coupling::transformer, 1000, true, 1, warm);
                        auto convergence = measure (lower.trace, reference.trace, s);
                        int refFactor = 64;
                        for (int factor : { 128, 256, 512 })
                        {
                            if (converged (convergence, s)) break;
                            lower = std::move (reference);
                            reference = render (rate, factor, v, topology, {}, wave, true, Coupling::transformer, 1000, true, 1, warm);
                            refFactor = factor; convergence = measure (lower.trace, reference.trace, s);
                        }
                        for (int channels : { 1, 2 })
                            for (int factor : { 1, 2, 4, 8 })
                            {
                                const auto candidate = render (rate, factor, v, topology, {}, wave, false, Coupling::transformer, 1000, true, channels, warm);
                                const auto m = measure (candidate.trace, reference.trace, s);
                                const auto pass = converged (convergence, s) && reference.diagnostics.failures == 0 && candidate.diagnostics.failures == 0
                                    && m.bandResidualDb <= residualScreenDb && std::abs (m.bandPeakErrorDb) <= 0.1
                                    && (! s.periodic() || m.offGridDb <= residualScreenDb) && (! s.silentTail() || m.tailErrorDb <= residualScreenDb);
                                out << "measurement," << name (v) << ',' << name (topology) << ',' << rate << ',' << channels << ',' << MagneticValidation::name (signal) << ',' << peak << ',' << factor << ','
                                    << refFactor << ',' << converged (convergence, s) << ',' << convergence.bandResidualDb << ',' << convergence.bandMaximumError << ',' << convergence.tailErrorDb << ','
                                    << m.bandResidualDb << ',' << m.bandPeakErrorDb << ',' << m.offGridDb << ',' << m.tailErrorDb << ',' << candidate.diagnostics.failures + reference.diagnostics.failures << ','
                                    << candidate.diagnostics.maximumIterations << ',' << candidate.diagnostics.maximumResidual << ',' << candidate.stereoExact << ',' << pass << '\n';
                                CHECK (candidate.diagnostics.failures == 0); CHECK (candidate.stereoExact);
                            }
                        out.flush();
                        std::cout << "validation " << name (v) << ' ' << name (topology) << ' ' << rate << ' ' << MagneticValidation::name (signal) << ' ' << peak << " ref " << refFactor << std::endl;
                    }
    REQUIRE (out.good());
}
void probeReport (const juce::File& root)
{
    auto out = csv (root, "hf_survival.csv", "kind,variant,topology,rate_hz,input_peak,limiter_enabled,probe_attenuation_db,hf_survival_ratio,limited_inputs,invalid_inputs,solver_failures");
    for (auto v : { Variant::smooth, Variant::play3 })
        for (auto topology : { Topology::shunt, Topology::series })
            for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
                for (double peak : { 1.0, 32.0 })
                    for (bool limit : { false, true })
                    {
                        const auto result = render (rate, 8, v, topology, {}, [=] (double n) { return peak * std::sin (2 * pi * 60 * n / rate) + 0.001 * std::sin (2 * pi * 2000 * n / rate); }, false, Coupling::transformer, 1000, limit);
                        const auto carrier = render (rate, 8, v, topology, {}, [=] (double n) { return 0.001 * std::sin (2 * pi * 2000 * n / rate); });
                        // Difference against bass-only removes bass harmonic/leakage energy from the probe estimate.
                        const auto bass = render (rate, 8, v, topology, {}, [=] (double n) { return peak * std::sin (2 * pi * 60 * n / rate); }, false, Coupling::transformer, 1000, limit);
                        auto isolated = result.trace;
                        for (std::size_t n = 0; n < isolated.samples.size(); ++n) isolated.samples[n] -= bass.trace.samples[n];
                        const auto a = amplitude (isolated, 2000, rate), b = amplitude (carrier.trace, 2000, rate);
                        out << "measurement," << name (v) << ',' << name (topology) << ',' << rate << ',' << peak << ',' << limit << ',' << db (a / 0.001) << ',' << a / std::max (b, 1.0e-20) << ','
                            << result.limited << ',' << result.invalid << ',' << result.diagnostics.failures << '\n';
                        CHECK (result.diagnostics.failures == 0);
                    }
    REQUIRE (out.good());
}
double stepCorner (double rate, Variant v, Topology t, TransformerParameters p, Coupling c, double resistance)
{
    Processor processor (rate, 64, 1, v, t, p, c, resistance);
    std::array<double, 3> samples {};
    for (auto& x : samples) x = processor.sample (0.0001, 0);
    const auto ratio = (samples[2] - samples[1]) / (samples[1] - samples[0]);
    return ratio > 0 && ratio < 1 ? -rate * 64 * std::log (ratio) / (2 * pi) : -1;
}
void couplingReport (const juce::File& root)
{
    auto out = csv (root, "coupling.csv", "kind,variant,topology,source_ohms,load_ohms,input_peak,mode,reference_factor,reference_difference_dbr,reference_valid,difference_dbr,lf_gain_db,h3_dbc,step_corner_hz,solver_failures,maximum_iterations");
    constexpr double rate = 48000;
    const auto bass = std::round (60 * windowSize / rate) * rate / windowSize;
    const auto high = std::round (2000 * windowSize / rate) * rate / windowSize;
    for (auto v : { Variant::smooth, Variant::play3 })
        for (auto t : { Topology::shunt, Topology::series })
            for (double resistance : { 10.0, 100.0, 1000.0, 10000.0, 100000.0 })
                for (double load : { 1000.0, 10000.0, 100000.0 })
                    for (double peak : { 0.05, 0.7, 4.0 })
                    {
                        TransformerParameters p; p.loadOhms = load;
                        const Waveform wave = [=] (double n) { return peak * std::sin (2 * pi * bass * n / rate) + 0.001 * std::sin (2 * pi * high * n / rate); };
                        auto lower = render (rate, 32, v, t, p, wave, true, Coupling::reference, resistance);
                        auto reference = render (rate, 64, v, t, p, wave, true, Coupling::reference, resistance);
                        auto error = difference (lower.trace, reference.trace);
                        int factor = 64;
                        for (int next : { 128, 256 })
                        {
                            if (error <= -100) break;
                            lower = std::move (reference); reference = render (rate, next, v, t, p, wave, true, Coupling::reference, resistance);
                            error = difference (lower.trace, reference.trace); factor = next;
                        }
                        const auto referenceValid = error <= -100 && reference.diagnostics.failures == 0;
                        for (auto c : { Coupling::buffered, Coupling::divider, Coupling::loadAware })
                        {
                            // Equal analytic rate isolates coupling from cheap-rate aliasing/filter differences.
                            const auto candidate = render (rate, factor, v, t, p, wave, true, c, resistance);
                            out << "measurement," << name (v) << ',' << name (t) << ',' << resistance << ',' << load << ',' << peak << ',' << name (c) << ',' << factor << ',' << error << ',' << referenceValid << ','
                                << difference (candidate.trace, reference.trace) << ',' << db (amplitude (candidate.trace, bass, rate) / peak) << ','
                                << db (amplitude (candidate.trace, 3 * bass, rate) / std::max (amplitude (candidate.trace, bass, rate), 1.0e-20)) << ','
                                << stepCorner (rate, v, t, p, c, resistance) << ',' << candidate.diagnostics.failures << ',' << candidate.diagnostics.maximumIterations << '\n';
                            CHECK (candidate.diagnostics.failures == 0);
                        }
                        out << "measurement," << name (v) << ',' << name (t) << ',' << resistance << ',' << load << ',' << peak << ",3-offline," << factor << ',' << error << ',' << referenceValid << ",0,"
                            << db (amplitude (reference.trace, bass, rate) / peak) << ',' << db (amplitude (reference.trace, 3 * bass, rate) / std::max (amplitude (reference.trace, bass, rate), 1.0e-20)) << ','
                            << stepCorner (rate, v, t, p, Coupling::reference, resistance) << ',' << reference.diagnostics.failures << ',' << reference.diagnostics.maximumIterations << '\n';
                        out.flush();
                        std::cout << "coupling " << name (v) << ' ' << name (t) << ' ' << resistance << ' ' << load << ' ' << peak << std::endl;
                    }
    REQUIRE (out.good());
}
}

TEST_CASE ("Export isolated module spike numeric measurements", "[module-report]")
{
    const auto* directory = std::getenv ("LUMIERE_MODULE_REPORT_DIR");
    if (! directory) { SUCCEED ("Explicit opt-in offline report"); return; }
    const juce::File root (juce::String::fromUTF8 (directory)); REQUIRE (root.createDirectory().wasOk());
    const auto* taskValue = std::getenv ("LUMIERE_MODULE_TASK");
    const std::string task = taskValue ? taskValue : "all";
    if (task == "all" || task == "cpu") cpuReport (root);
    if (task == "all" || task == "validation") validationReport (root);
    if (task == "all" || task == "probe") probeReport (root);
    if (task == "all" || task == "coupling") couplingReport (root);
}

TEST_CASE ("Reduced hysteretic solver handles cancellation without dropping samples", "[module-transformer]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int factor : { 8, 64 })
            for (auto topology : { Topology::shunt, Topology::series })
            {
                TransformerCircuit circuit;
                circuit.prepare (rate * factor, Variant::play3, topology, {});
                const Stimulus stimulus (Signal::lowHigh, rate, 4);
                for (int n = 0; n < 131072; ++n) circuit.process (stimulus.at (static_cast<double> (n) / factor - windowSize));
                CAPTURE (rate, factor, name (topology), circuit.diagnostics.maximumResidual);
                CHECK (circuit.diagnostics.failures == 0);
                CHECK (circuit.diagnostics.maximumResidual <= TransformerCircuit::tolerance);
            }
}
