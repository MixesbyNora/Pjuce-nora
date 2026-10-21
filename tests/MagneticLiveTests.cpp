#include <PluginProcessor.h>
#include <dsp/TransformerSaturation.h>
#include "helpers/TransformerAudioPrototype.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>

namespace
{
using namespace MagneticValidation;
void set (PluginProcessor& processor, const char* id, float value)
{
    for (auto* p : processor.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p); parameter != nullptr && parameter->paramID == id)
        {
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            return;
        }
    FAIL ("Missing APVTS parameter " << id);
}
TransformerAudioPrototype::Render liveRender (double rate, const Stimulus& stimulus)
{
    TransformerSaturation stage;
    stage.prepare (rate, 100.0f, false);
    const auto warm = static_cast<int> (std::ceil (rate / 256)) * 256;
    const auto count = ((warm + windowSize + stage.getLatencySamples() + 255) / 256) * 256;
    std::vector<double> raw (static_cast<std::size_t> (count));
    juce::AudioBuffer<float> block (1, 256);
    for (int start = 0; start < count; start += 256)
    {
        for (int n = 0; n < 256; ++n) block.setSample (0, n, static_cast<float> (stimulus.at (start + n - warm)));
        stage.process (block, 1);
        for (int n = 0; n < 256; ++n) raw[static_cast<std::size_t> (start + n)] = block.getSample (0, n);
    }
    Trace trace;
    trace.latencySamples = stage.getLatencySamples();
    const auto first = raw.begin() + warm + stage.getLatencySamples();
    trace.samples.assign (first, first + windowSize);
    return { std::move (trace), stage.getCircuit().getDiagnostics() };
}
TransformerAudioPrototype::Render stageReference (double rate, const Stimulus& stimulus, int factor)
{
    juce::dsp::Oversampling<double> filters (1);
    filters.clearOversamplingStages();
    for (int stage = 1; stage < factor; stage *= 2)
    {
        const auto width = stage == 1 ? MagneticOversampling::firstTransition : MagneticOversampling::laterTransition;
        filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
            width, MagneticOversampling::stopbandDb, width, MagneticOversampling::stopbandDb);
    }
    filters.setUsingIntegerLatency (false);
    filters.initProcessing (256);
    juce::AudioBuffer<double> block (1, 256);
    double sum = 0.0, moment = 0.0;
    for (int start = 0; start < 1024; start += 256)
    {
        block.clear();
        juce::dsp::AudioBlock<double> base (block);
        auto high = filters.processSamplesUp (base);
        high.clear();
        if (start == 0) high.setSample (0, 0, factor);
        filters.processSamplesDown (base);
        for (int n = 0; n < 256; ++n)
        { sum += block.getSample (0, n); moment += (start + n) * block.getSample (0, n); }
    }
    const auto latency = moment / sum;
    filters.reset();
    const auto highRate = rate * factor;
    MagneticPrototype::TransformerAudioCircuit core;
    MagneticPrototype::TransformerAudioCircuit::Parameters parameters;
    parameters.driveDb = 12.0;
    core.prepare (highRate, parameters, false, true); // Scalar constitutive reference.
    const auto raw = juce::dsp::IIR::ArrayCoefficients<double>::makeLowShelf (highRate,
        TransformerSettings::shelfFrequencyHz, TransformerSettings::shelfQ,
        juce::Decibels::decibelsToGain (TransformerSettings::shelfGainDb));
    const DoubleBiquad::Coefficients pre { raw[0] / raw[3], raw[1] / raw[3], raw[2] / raw[3], raw[4] / raw[3], raw[5] / raw[3] };
    const DoubleBiquad::Coefficients post { 1.0 / pre.b0, pre.a1 / pre.b0, pre.a2 / pre.b0, pre.b1 / pre.b0, pre.b2 / pre.b0 };
    DoubleBiquad::State preState, postState;
    const auto warm = static_cast<int> (std::ceil (rate / 256)) * 256;
    const auto count = ((warm + windowSize + 512 + 255) / 256) * 256;
    std::vector<double> output (static_cast<std::size_t> (count));
    for (int start = 0; start < count; start += 256)
    {
        block.clear();
        juce::dsp::AudioBlock<double> base (block);
        auto high = filters.processSamplesUp (base);
        for (std::size_t n = 0; n < high.getNumSamples(); ++n)
        {
            const auto input = stimulus.at (start - warm + static_cast<double> (n) / factor);
            const auto emphasized = DoubleBiquad::processSample (input, pre, preState);
            const auto voltage = core.processSample (emphasized);
            high.setSample (0, static_cast<int> (n), DoubleBiquad::processSample (voltage, post, postState));
        }
        filters.processSamplesDown (base);
        for (int n = 0; n < 256; ++n) output[static_cast<std::size_t> (start + n)] = block.getSample (0, n);
    }
    return { alignTrace (output, warm, latency), core.getDiagnostics() };
}

}

TEST_CASE ("Magnetic constitutive table retains the internal tanh to double precision", "[magnetic-live][solver]")
{
    MagneticPrototype::ConstitutiveLookup lookup;
    double worst = 0.0;
    bool monotone = true;
    auto previous = -1.0;
    for (int n = 0; n <= 2000000; ++n)
    {
        const auto x = -32.0 + 64.0 * n / 2000000.0;
        const auto value = lookup.value (x);
        worst = std::max (worst, std::abs (value - std::tanh (x)));
        // Below an ulp of slope, independent polynomial rounding can differ
        // by one ulp. The scalar function has the same floating plateau.
        monotone &= value + 2.0e-15 >= previous;
        previous = value;
    }
    CHECK (worst < 2.0e-15);
    CHECK (monotone);
    CHECK (std::bit_cast<std::uint64_t> (lookup.value (-0.0)) == std::bit_cast<std::uint64_t> (-0.0));
}

TEST_CASE ("Magnetic plugin reports constant latency and exact delayed host bypass in mono and stereo", "[magnetic-live][latency]")
{
    constexpr std::array rates { 44100.0, 48000.0, 96000.0, 192000.0 };
    constexpr std::array latencies { 144, 144, 137, 124 };
    constexpr std::array factors { 16, 16, 8, 4 };
    for (std::size_t index = 0; index < rates.size(); ++index)
        for (int channels : { 1, 2 })
        {
            PluginProcessor processor;
            if (channels == 1)
            {
                auto layout = processor.getBusesLayout();
                layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
                layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
                REQUIRE (processor.setBusesLayout (layout));
            }
            set (processor, "threshold", 0.0f);
            set (processor, "bypass", 1.0f);
            set (processor, "xfrmOut", 1.0f);
            set (processor, "xfrmAmount", 100.0f);
            processor.prepareToPlay (rates[index], 64);
            REQUIRE (processor.getLatencySamples() == latencies[index]);
            CHECK (MagneticPrototype::AudioSettings::factorForRate (rates[index]) == factors[index]);
            juce::AudioBuffer<float> input (channels, 4096), actual (channels, 4096);
            for (int ch = 0; ch < channels; ++ch)
                for (int n = 0; n < 4096; ++n)
                    input.setSample (ch, n, (ch == 0 ? 1.0f : -1.0f) * static_cast<float> (n % 113 - 56) * 0.0001f);
            actual.makeCopyOf (input);
            juce::MidiBuffer midi;
            processor.processBlock (actual, midi);
            bool exact = true;
            for (int ch = 0; ch < channels; ++ch)
                for (int n = 0; n < 4096; ++n)
                {
                    const auto expected = n < latencies[index] ? 0.0f : input.getSample (ch, n - latencies[index]);
                    exact &= std::bit_cast<std::uint32_t> (expected) == std::bit_cast<std::uint32_t> (actual.getSample (ch, n));
                }
            CHECK (exact);
            set (processor, "xfrmOut", 0.0f);
            processor.processBlock (actual, midi);
            CHECK (processor.getLatencySamples() == latencies[index]);
            CHECK (processor.getTailLengthSeconds() >= 1.0);
        }
}

TEST_CASE ("Shipped magnetic stage agrees with independently driven converged audio references", "[magnetic-live][validation]")
{
    const auto* directory = std::getenv ("LUMIERE_MAGNETIC_LIVE_REPORT_DIR");
    const auto full = directory != nullptr;
    std::ofstream report;
    if (full)
    {
        const juce::File root (juce::String::fromUTF8 (directory));
        REQUIRE (root.createDirectory().wasOk());
        report.open (root.getChildFile ("live_validation.csv").getFullPathName().toStdString());
        report << std::setprecision (17) << "signal,rate_hz,peak,factor,latency,reference_residual_dbr,reference_max_error,reference_full_max_error,band_residual_dbr,band_peak_error_db,band_max_error,off_grid_dbr,tail_error_dbr,tail_drift_dbr,full_residual_dbr,solver_failures,solver_residual,maximum_iterations,pass\n";
    }
    for (auto signal : signals)
    {
        if (! full && signal != Signal::closeHigh) continue;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double peak : { 0.05, 1.0, 4.0 })
            {
                if (! full && peak < 1.0) continue;
                const Stimulus stimulus (signal, rate, peak);
                auto lower = stageReference (rate, stimulus, 32);
                auto reference = stageReference (rate, stimulus, 64);
                auto convergence = measure (lower.trace, reference.trace, stimulus);
                for (int factor : { 128, 256, 512 })
                {
                    if (convergence.bandResidualDb <= -100.0 && convergence.bandMaximumError <= 1.0e-4
                        && (! stimulus.silentTail() || convergence.tailErrorDb <= -100.0)) break;
                    lower = std::move (reference);
                    reference = stageReference (rate, stimulus, factor);
                    convergence = measure (lower.trace, reference.trace, stimulus);
                }
                CHECK (reference.diagnostics.solverFailures == 0);
                const auto live = liveRender (rate, stimulus);
                const auto metrics = measure (live.trace, reference.trace, stimulus);
                const auto pass = convergence.bandResidualDb <= -100.0 && convergence.bandMaximumError <= 1.0e-4
                    && (! stimulus.silentTail() || convergence.tailErrorDb <= -100.0)
                    && metrics.bandResidualDb <= -80.0 && std::abs (metrics.bandPeakErrorDb) <= 0.1
                    && (! stimulus.periodic() || metrics.offGridDb <= -80.0)
                    && (! stimulus.silentTail() || metrics.tailErrorDb <= -80.0)
                    && live.diagnostics.solverFailures == 0;
                CAPTURE (name (signal), rate, peak, convergence.bandResidualDb, metrics.bandResidualDb, metrics.bandPeakErrorDb);
                CHECK (pass);
                if (full)
                {
                    report << name (signal) << ',' << rate << ',' << peak << ',' << MagneticPrototype::AudioSettings::factorForRate (rate)
                           << ',' << live.trace.latencySamples << ',' << convergence.bandResidualDb << ',' << convergence.bandMaximumError << ',' << convergence.maximumError
                           << ',' << metrics.bandResidualDb << ',' << metrics.bandPeakErrorDb << ',' << metrics.bandMaximumError
                           << ',';
                    if (stimulus.periodic()) report << metrics.offGridDb;
                    report << ',';
                    if (stimulus.silentTail()) report << metrics.tailErrorDb;
                    report << ',';
                    if (stimulus.silentTail()) report << metrics.tailDriftDb;
                    report << ',' << metrics.fullResidualDb << ',' << live.diagnostics.solverFailures << ',' << live.diagnostics.maximumEquationResidual
                           << ',' << live.diagnostics.maximumIterations << ',' << pass << '\n';
                    report.flush();
                }
            }
    }
}

TEST_CASE ("Export full magnetic plugin Release CPU at each rate", "[magnetic-live-cpu]")
{
    const auto* directory = std::getenv ("LUMIERE_MAGNETIC_CPU_REPORT_DIR");
    if (directory == nullptr) { SUCCEED ("Optional isolated full-plugin CPU export"); return; }
    const juce::File root (juce::String::fromUTF8 (directory));
    REQUIRE (root.createDirectory().wasOk());
    std::ofstream output (root.getChildFile ("plugin_cpu.csv").getFullPathName().toStdString());
    output << std::setprecision (17) << "rate_hz,channels,factor,latency,ns_per_frame,realtime_percent,checksum\n";
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            PluginProcessor processor;
            if (channels == 1)
            {
                auto layout = processor.getBusesLayout();
                layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
                layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
                REQUIRE (processor.setBusesLayout (layout));
            }
            set (processor, "xfrmAmount", 100.0f);
            processor.prepareToPlay (rate, 256);
            juce::AudioBuffer<float> block (channels, 256);
            juce::MidiBuffer midi;
            std::array<double, 7> times {};
            double checksum = 0.0;
            for (auto& duration : times)
            {
                const auto begin = std::chrono::steady_clock::now();
                for (int start = 0; start < 16384; start += 256)
                {
                    for (int ch = 0; ch < channels; ++ch)
                        for (int n = 0; n < 256; ++n)
                            block.setSample (ch, n, static_cast<float> (0.7 * std::sin (2 * juce::MathConstants<double>::pi * (ch == 0 ? 60 : 73) * (start + n) / rate)));
                    processor.processBlock (block, midi);
                    checksum += block.getSample (0, 255);
                }
                duration = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - begin).count() / 16384.0;
            }
            std::sort (times.begin(), times.end());
            output << rate << ',' << channels << ',' << MagneticPrototype::AudioSettings::factorForRate (rate) << ',' << processor.getLatencySamples()
                   << ',' << times[3] << ',' << times[3] * rate / 1.0e7 << ',' << checksum << '\n';
            CHECK (std::isfinite (checksum));
        }
    REQUIRE (output.good());
}
