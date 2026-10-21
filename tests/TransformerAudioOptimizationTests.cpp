#include "helpers/TransformerAudioPrototype.h"
#include "helpers/TransformerAudioReferenceHarness.h"
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>

namespace
{
using Candidate = MagneticPrototype::TransformerAudioCircuit;
using Reference = MagneticPrototype::TransformerAudioReference;
using namespace MagneticValidation;
constexpr double pi = juce::MathConstants<double>::pi;
bool equivalent (double a, double b) { return std::abs (a - b) <= 2.0e-9 * std::max ({ 1.0, std::abs (a), std::abs (b) }); }
Reference::Parameters referenceParameters (Candidate::Parameters p) { return { p.cornerHz, p.driveDb, p.magnetizationWeight, p.core }; }

double profileInput (int workload, int channel, int n, double rate)
{
    const auto time = n / rate;
    if (workload == 0)
        return 0.7 * std::sin (2 * pi * (channel == 0 ? 60.0 : 73.0) * time);
    if (workload == 1)
        return 0.4 * std::sin (2 * pi * 60 * time) + 0.2 * std::sin (2 * pi * 7000 * time)
            + 0.1 * std::sin (2 * pi * (channel == 0 ? 19000.0 : 20000.0) * time);
    const auto phase = std::fmod (time, 0.05);
    return phase < 0.025 ? std::exp (-phase * 150) * (std::sin (2 * pi * 80 * time)
        + 0.25 * std::sin (2 * pi * 12000 * time)) : 0.0;
}
template <typename Processor>
double timedRun (Processor& processor, juce::AudioBuffer<double>& block, int workload, double rate, double& checksum)
{
    processor.reset();
    const auto start = std::chrono::steady_clock::now();
    for (int offset = 0; offset < 16384; offset += TransformerAudioPrototype::capacity)
    {
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            for (int n = 0; n < block.getNumSamples(); ++n)
                block.setSample (channel, n, profileInput (workload, channel, offset + n, rate));
        processor.process (block);
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            checksum += block.getSample (channel, block.getNumSamples() - 1);
    }
    return std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count() / 16384.0;
}
}

TEST_CASE ("Optimized coupled solver retains v1 output and magnetic history at parameter extremes", "[transformer-audio][transformer-cpu]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double memory : { 0.0, 0.35, 1.0 })
            for (double width : { 0.05, 1.0, 4.0 })
                for (double drive : { -24.0, 0.0, 12.0 })
                {
                    const Candidate::Parameters parameters { 30.0, drive, 16.0, { memory, width } };
                    Candidate candidate;
                    Reference reference;
                    candidate.prepare (rate * 16, parameters);
                    reference.prepare (rate * 16, referenceParameters (parameters));
                    bool matches = true;
                    std::uint32_t random = 12345;
                    for (int n = 0; n < 8192; ++n)
                    {
                        random = random * 1664525U + 1013904223U;
                        const auto noise = static_cast<double> (random) / 4294967295.0 * 32 - 16;
                        const auto x = n < 512 ? 16.0 : n < 1024 ? -16.0 : n < 2048 ? noise
                            : n < 6144 ? 0.7 * std::sin (n * 0.017) : 0.0;
                        matches &= equivalent (candidate.processSample (x), reference.processSample (x))
                            && equivalent (candidate.getField(), reference.getField())
                            && equivalent (candidate.getMagnetization(), reference.getMagnetization())
                           ;
                    }
                    CAPTURE (rate, memory, width, drive);
                    CHECK (matches);
                    // The frozen solver can exhaust its old cap on these paths.
                    // The current solver must converge at the original tolerance.
                    CHECK (candidate.getDiagnostics().solverFailures == 0);
                    CHECK (candidate.getDiagnostics().maximumEquationResidual <= MagneticPrototype::AudioSettings::rootTolerance);
                    CHECK (candidate.getDiagnostics().maximumIterations <= MagneticPrototype::AudioSettings::maximumIterations);
                    candidate.reset();
                    reference.reset();
                    bool resetMatches = true;
                    for (double x : { -0.0, 0.0, 0.1, -0.1, 1.0e-20, -1.0e-20, std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1.0e300, -1.0e300, 0.0 })
                        resetMatches &= equivalent (candidate.processSample (x), reference.processSample (x));
                    CHECK (resetMatches);
                }
}

TEST_CASE ("Optimized complete transformer chain agrees with the frozen v1 across signal and rate grid", "[transformer-audio][transformer-cpu]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto signal : signals)
            for (double peak : { 0.05, 1.0, 4.0 })
            {
                TransformerAudioPrototype::Processor candidate (rate, 16, 2);
                TransformerAudioReferenceHarness::Processor reference (rate, 16, 2);
                juce::AudioBuffer<double> a (2, 256), b (2, 256);
                const Stimulus stimulus (signal, rate, peak);
                bool matches = true;
                // Capture the entire existing window, including transient onset
                // and decay; a different channel trajectory checks isolation.
                for (int start = 0; start < windowSize; start += 256)
                {
                    for (int n = 0; n < 256; ++n)
                        for (int channel = 0; channel < 2; ++channel)
                        {
                            const auto value = stimulus.at (start + n + channel * 137.0);
                            a.setSample (channel, n, value);
                            b.setSample (channel, n, value);
                        }
                    candidate.process (a);
                    reference.process (b);
                    for (int channel = 0; channel < 2; ++channel)
                        for (int n = 0; n < 256; ++n)
                            matches &= equivalent (a.getSample (channel, n), b.getSample (channel, n));
                }
                CAPTURE (rate, name (signal), peak);
                CHECK (matches);
                for (std::size_t channel = 0; channel < 2; ++channel)
                {
                    CHECK (candidate.getCircuit (channel).getDiagnostics().solverFailures == 0);
                    CHECK (equivalent (candidate.getCircuit (channel).getField(), reference.getCircuit (channel).getField()));
                    CHECK (equivalent (candidate.getCircuit (channel).getMagnetization(), reference.getCircuit (channel).getMagnetization()));
                }
            }
}

TEST_CASE ("Cached magnetic trials preserve branch slopes to double precision and reset on reprepare", "[transformer-audio][transformer-cpu]")
{
    Candidate candidate;
    Reference reference;
    for (double memory : { 0.0, 0.35, 1.0 })
        for (double width : { 0.05, 1.0, 4.0 })
        {
            const Candidate::Parameters parameters { 5.0, 0.0, 4.0, { memory, width } };
            candidate.prepare (48000.0 * 16, parameters);
            reference.prepare (48000.0 * 16, referenceParameters (parameters));
            bool matches = true;
            for (int n = 0; n < 1024; ++n)
            {
                const auto x = 0.7 * std::sin (n * 0.017);
                matches &= equivalent (candidate.processSample (x), reference.processSample (x));
                const auto h = candidate.getField();
                const auto states = candidate.getCore().getPlayStates();
                for (double trial : { h, std::nextafter (h, -100.0), std::nextafter (h, 100.0),
                         -100.0, -32.0, -0.0, 0.0, 32.0, 100.0, std::numeric_limits<double>::quiet_NaN() })
                {
                    const auto a = candidate.evaluateTrial (trial);
                    const auto b = reference.evaluateTrial (trial);
                    matches &= equivalent (a.magnetization, b.magnetization) && std::isfinite (a.derivative) && a.derivative >= 0.0;
                }
                matches &= states == candidate.getCore().getPlayStates();
            }
            CHECK (matches);
        }
    for (bool linear : { false, true })
        for (double weight : { 0.0, 4.0 })
        {
            Candidate::Parameters p;
            p.magnetizationWeight = weight;
            candidate.prepare (192000.0 * 16, p, linear);
            reference.prepare (192000.0 * 16, referenceParameters (p), linear);
            bool matches = true;
            for (int n = 0; n < 1024; ++n)
            {
                const auto x = n < 512 ? 0.2 : 0.0;
                matches &= equivalent (candidate.processSample (x), reference.processSample (x));
            }
            CHECK (matches);
        }
}

TEST_CASE ("Export paired Release transformer CPU measurements", "[transformer-cpu-report]")
{
    const auto* directory = std::getenv ("LUMIERE_TRANSFORMER_CPU_REPORT_DIR");
    if (directory == nullptr)
    {
        SUCCEED ("Set LUMIERE_TRANSFORMER_CPU_REPORT_DIR for isolated paired timing");
        return;
    }
    const juce::File root (juce::String::fromUTF8 (directory));
    REQUIRE (root.createDirectory().wasOk());
    std::ofstream output (root.getChildFile ("paired_timing.csv").getFullPathName().toStdString());
    output << std::setprecision (17)
           << "workload,sample_rate_hz,channels,reference_ns_per_frame,candidate_ns_per_frame,speedup,reference_realtime_percent,candidate_realtime_percent,checksums_finite,solver_failures,maximum_iterations\n";
    constexpr std::array names { "low_tone", "multitone", "transient" };
    for (int workload = 0; workload < 3; ++workload)
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int channels : { 1, 2 })
            {
                TransformerAudioPrototype::Processor candidate (rate, MagneticPrototype::AudioSettings::factorForRate (rate), channels);
                TransformerAudioReferenceHarness::Processor reference (rate, 16, channels);
                juce::AudioBuffer<double> block (channels, TransformerAudioPrototype::capacity);
                std::array<double, 7> oldTimes {}, newTimes {};
                double oldChecksum = 0, newChecksum = 0;
                // Warm caches, then alternate execution order to reduce drift.
                timedRun (reference, block, workload, rate, oldChecksum);
                timedRun (candidate, block, workload, rate, newChecksum);
                for (std::size_t trial = 0; trial < oldTimes.size(); ++trial)
                {
                    if (trial % 2 == 0)
                    {
                        oldTimes[trial] = timedRun (reference, block, workload, rate, oldChecksum);
                        newTimes[trial] = timedRun (candidate, block, workload, rate, newChecksum);
                    }
                    else
                    {
                        newTimes[trial] = timedRun (candidate, block, workload, rate, newChecksum);
                        oldTimes[trial] = timedRun (reference, block, workload, rate, oldChecksum);
                    }
                }
                std::sort (oldTimes.begin(), oldTimes.end());
                std::sort (newTimes.begin(), newTimes.end());
                const auto before = oldTimes[3], after = newTimes[3];
                std::uint64_t failures = 0;
                int iterations = 0;
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto& diagnostics = candidate.getCircuit (static_cast<std::size_t> (channel)).getDiagnostics();
                    failures += diagnostics.solverFailures;
                    failures += reference.getCircuit (static_cast<std::size_t> (channel)).getDiagnostics().solverFailures;
                    iterations = std::max (iterations, diagnostics.maximumIterations);
                }
                const auto matches = std::isfinite (oldChecksum) && std::isfinite (newChecksum);
                output << names[static_cast<std::size_t> (workload)] << ',' << rate << ',' << channels << ',' << before << ',' << after << ','
                       << before / after << ',' << before * rate / 1.0e7 << ',' << after * rate / 1.0e7 << ',' << matches << ',' << failures << ',' << iterations << '\n';
                output.flush();
                CAPTURE (workload, rate, channels, oldChecksum, newChecksum);
                CHECK (matches);
                CHECK (failures == 0);
            }
    REQUIRE (output.good());
}
