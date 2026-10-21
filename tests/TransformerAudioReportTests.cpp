#include "helpers/TransformerAudioPrototype.h"
#include <dsp/TransformerSaturation.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <numeric>

namespace
{
using namespace TransformerAudioPrototype;
using namespace MagneticValidation;
constexpr double pi = juce::MathConstants<double>::pi;
bool finite (double x) { return (std::bit_cast<std::uint64_t> (x) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL; }
double db (double amplitude) { return 20.0 * std::log10 (std::max (amplitude, 1.0e-15)); }
std::ofstream file (const juce::File& root, const char* name)
{
    std::ofstream result (root.getChildFile (name).getFullPathName().toStdString());
    result << std::setprecision (17);
    return result;
}
Trace liveMagnetic (double rate, const Waveform& input, int length = windowSize)
{
    TransformerSaturation transformer;
    transformer.prepare (rate, 100.0f, false, {}, false);
    const auto warm = static_cast<int> (std::ceil (rate / capacity)) * capacity;
    const auto delay = transformer.getLatencySamples();
    const auto count = ((length + warm + delay + capacity - 1) / capacity) * capacity;
    juce::AudioBuffer<float> block (1, capacity);
    Trace result;
    result.samples.resize (static_cast<std::size_t> (length));
    for (int start = 0; start < count; start += capacity)
    {
        for (int n = 0; n < capacity; ++n) block.setSample (0, n, static_cast<float> (input (start + n - warm)));
        transformer.process (block, 1);
        for (int n = 0; n < capacity; ++n)
            if (start + n >= warm + delay && start + n < warm + delay + length)
                result.samples[static_cast<std::size_t> (start + n - warm - delay)] = block.getSample (0, n);
    }
    return result;
}
void validRender (const Render& result)
{
    bool valid = true;
    for (auto x : result.trace.samples) valid &= finite (x) && std::abs (x) < 64.0;
    CHECK (valid);
    CHECK (result.diagnostics.solverFailures == 0);
    CHECK (result.diagnostics.maximumEquationResidual <= MagneticPrototype::AudioSettings::rootTolerance);
}
void writeWav (const juce::File& path, const juce::AudioBuffer<float>& audio)
{
    auto output = path.createOutputStream();
    REQUIRE (output != nullptr);
    REQUIRE (output->setPosition (0));
    REQUIRE (output->truncate().wasOk());
    std::unique_ptr<juce::OutputStream> stream = std::move (output);
    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (32)
        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = format.createWriterFor (stream, options);
    REQUIRE (writer != nullptr);
    REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
}
double rms (const juce::AudioBuffer<float>& audio)
{
    double power = 0.0;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int n = 0; n < audio.getNumSamples(); ++n) power += static_cast<double> (audio.getSample (ch, n)) * audio.getSample (ch, n);
    return std::sqrt (power / (audio.getNumChannels() * audio.getNumSamples()));
}
double fixture (int kind, int channel, double sample)
{
    const auto time = sample / 48000.0 - 0.1;
    if (time < 0.0 || time >= 3.8) return 0.0;
    const auto fade = std::min ({ 1.0, time / 0.01, (3.8 - time) / 0.05 });
    const auto position = std::fmod (time, 0.5);
    if (kind == 0)
    {
        constexpr std::array notes { 55.0, 65.4064, 82.4069, 73.4162 };
        const auto frequency = notes[static_cast<std::size_t> (static_cast<int> (time * 2) % 4)] * (channel == 0 ? 1.0 : 1.002);
        return fade * 0.5 * (1.0 - std::exp (-position / 0.005)) * std::exp (-position / 0.3)
            * (0.7 * std::sin (2 * pi * frequency * position) + 0.2 * std::sin (4 * pi * frequency * position) + 0.1 * std::sin (6 * pi * frequency * position));
    }
    if (kind == 1)
    {
        const auto index = static_cast<std::uint32_t> (std::max (0.0, sample));
        auto random = index * 1664525U + 1013904223U + static_cast<std::uint32_t> (channel) * 1234567U;
        random ^= random >> 16;
        random *= 2246822519U;
        const auto noise = static_cast<double> (random) / 4294967295.0 * 2 - 1;
        return fade * (0.5 * std::exp (-position * 24) * std::sin (2 * pi * (55 * position + 1.5 * (1 - std::exp (-position * 50))))
            + 0.12 * noise * std::exp (-position * 65));
    }
    return fade * (0.25 * std::sin (2 * pi * (channel == 0 ? 60.0 : 75.0) * time)
        + 0.08 * std::sin (2 * pi * 1000 * time) + 0.05 * std::sin (2 * pi * 10000 * time));
}
}

TEST_CASE ("Export voltage-driven transformer audio measurements and listening fixtures", "[transformer-audio-report]")
{
    const auto* directory = std::getenv ("LUMIERE_TRANSFORMER_AUDIO_REPORT_DIR");
    if (directory == nullptr)
    {
        SUCCEED ("Set LUMIERE_TRANSFORMER_AUDIO_REPORT_DIR for the offline export");
        return;
    }
    const juce::File root (juce::String::fromUTF8 (directory));
    REQUIRE (root.createDirectory().wasOk());
    auto summary = file (root, "comparison.csv");
    auto convergence = file (root, "convergence.csv");
    summary << "signal,sample_rate_hz,input_peak_linear,band_residual_dbr,off_grid_dbr,band_peak_error_db,band_max_error,tail_mean_error_dbr,tail_output_drift_dbr,full_residual_dbr,linear_calibration_dbr,reference_factor,reference_difference_dbr,reference_band_max_error,reference_full_agreement,converged,latency_samples,solver_failures,maximum_scaled_equation_residual,maximum_solver_iterations,pass\n";
    convergence << "signal,sample_rate_hz,input_peak_linear,factor,band_difference_dbr,band_max_error,tail_mean_error_dbr,full_max_error\n";
    const auto full = std::getenv ("LUMIERE_TRANSFORMER_AUDIO_FULL_GRID") != nullptr;
    for (auto signal : signals)
    {
        if (! full && signal != Signal::closeHigh && signal != Signal::impulse) continue;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double peak : { 0.05, 1.0, 4.0 })
            {
                const Stimulus stimulus (signal, rate, peak);
                const Waveform input = [=] (double n) { return stimulus.at (n); };
                auto previous = TransformerAudioPrototype::render (rate, input, 32, true);
                validRender (previous);
                Render reference;
                Metrics referenceCheck;
                int factor = 64;
                bool converged = false;
                for (int current : { 64, 128, 256, 512 })
                {
                    reference = TransformerAudioPrototype::render (rate, input, current, true);
                    validRender (reference);
                    factor = current;
                    referenceCheck = measure (previous.trace, reference.trace, stimulus);
                    convergence << name (signal) << ',' << rate << ',' << peak << ',' << factor << ',' << referenceCheck.bandResidualDb << ','
                        << referenceCheck.bandMaximumError << ',' << referenceCheck.tailErrorDb << ',' << referenceCheck.maximumError << '\n';
                    converged = referenceCheck.bandResidualDb <= -100.0 && referenceCheck.bandMaximumError <= 1.0e-4
                        && (! stimulus.silentTail() || referenceCheck.tailErrorDb <= -100.0);
                    if (converged || current == 512) break;
                    previous = std::move (reference);
                }
                const auto candidate = TransformerAudioPrototype::render (rate, input, MagneticPrototype::AudioSettings::factorForRate (rate));
                validRender (candidate);
                const auto m = measure (candidate.trace, reference.trace, stimulus);
                const auto linear = TransformerAudioPrototype::render (rate, input, 16, false, true);
                const auto linearReference = TransformerAudioPrototype::render (rate, input, factor, true, true);
                const auto calibration = measure (linear.trace, linearReference.trace, stimulus).bandResidualDb;
                const auto pass = converged && calibration <= -95.0 && m.bandResidualDb <= -80.0
                    && (! stimulus.periodic() || m.offGridDb <= -80.0) && std::abs (m.bandPeakErrorDb) <= 0.1
                    && (! stimulus.silentTail() || m.tailErrorDb <= -80.0) && candidate.diagnostics.solverFailures == 0;
                CAPTURE (name (signal), rate, peak);
                CHECK (pass);
                summary << name (signal) << ',' << rate << ',' << peak << ',' << m.bandResidualDb << ',';
                if (stimulus.periodic()) summary << m.offGridDb;
                summary << ',' << m.bandPeakErrorDb << ',' << m.bandMaximumError << ',';
                if (stimulus.silentTail()) summary << m.tailErrorDb;
                summary << ',';
                if (stimulus.silentTail()) summary << m.tailDriftDb;
                summary << ',' << m.fullResidualDb << ',' << calibration << ',' << factor << ',' << referenceCheck.bandResidualDb << ','
                    << referenceCheck.bandMaximumError << ',' << (referenceCheck.maximumError <= 1.0e-4) << ',' << converged << ','
                    << candidate.trace.latencySamples << ',' << candidate.diagnostics.solverFailures << ',' << candidate.diagnostics.maximumEquationResidual << ','
                    << candidate.diagnostics.maximumIterations << ',' << pass << '\n';
                summary.flush();
                convergence.flush();
                if (signal == Signal::impulse && rate == 48000.0 && peak == 1.0)
                {
                    auto trace = file (root, "impulse.csv");
                    trace << "time_ms,source,candidate,reference,error,linear\n";
                    for (int n = 0; n < windowSize; ++n)
                    {
                        const auto i = static_cast<std::size_t> (n);
                        trace << n / 48.0 << ',' << input (n) << ',' << candidate.trace.samples[i] << ',' << reference.trace.samples[i] << ','
                            << candidate.trace.samples[i] - reference.trace.samples[i] << ',' << linear.trace.samples[i] << '\n';
                    }
                    REQUIRE (trace.good());
                }
            }
    }
    REQUIRE (summary.good());
    REQUIRE (convergence.good());

    auto tones = file (root, "tones.csv");
    tones << "requested_hz,frequency_hz,input_dbfs,model,fundamental_gain_db,rms_gain_db,h2_dbc,h3_dbc,h5_dbc,h7_dbc\n";
    for (double frequency : { 20.0, 50.0, 60.0, 200.0, 1000.0, 5000.0, 8000.0 })
        for (double level : { -60.0, -24.0, -6.0, 6.0 })
        {
            const auto bin = 2 * static_cast<int> (std::round ((frequency * windowSize / 48000.0 - 1) / 2.0)) + 1;
            const auto amplitude = std::pow (10.0, level / 20.0);
            const Waveform input = [=] (double n) { return amplitude * std::sin (2 * pi * bin * n / windowSize); };
            for (int model = 0; model < 3; ++model)
            {
                const auto output = model == 2 ? liveMagnetic (48000.0, input)
                    : TransformerAudioPrototype::render (48000.0, input, 16, false, model == 0).trace;
                const auto bins = spectrum (output.samples);
                const auto fundamental = std::abs (bins[static_cast<std::size_t> (bin)]) * 2.0;
                const auto power = std::inner_product (output.samples.begin(), output.samples.end(), output.samples.begin(), 0.0) / windowSize;
                tones << frequency << ',' << bin * 48000.0 / windowSize << ',' << level << ',' << (model == 0 ? "linear" : model == 1 ? "magnetic16" : "live_magnetic") << ','
                    << db (fundamental / amplitude) << ',' << db (std::sqrt (power) / (amplitude / std::sqrt (2.0)));
                for (int harmonic : { 2, 3, 5, 7 })
                {
                    tones << ',';
                    if (harmonic * bin < windowSize / 2) tones << db (2 * std::abs (bins[static_cast<std::size_t> (harmonic * bin)]) / fundamental);
                }
                tones << '\n';
            }
            tones.flush();
        }
    REQUIRE (tones.good());

    auto listening = file (root, "listening.csv");
    listening << "fixture,model,raw_rms_dbfs,raw_peak_dbfs,matched_gain_db,matched_peak_dbfs\n";
    constexpr int length = 48000 * 6;
    for (int kind = 0; kind < 3; ++kind)
    {
        const juce::String prefix = kind == 0 ? "synthetic_bass" : kind == 1 ? "synthetic_percussion" : "synthetic_low_high";
        juce::AudioBuffer<float> source (2, length);
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < length; ++n) source.setSample (channel, n, static_cast<float> (fixture (kind, channel, n)));
        writeWav (root.getChildFile (prefix + "_source.wav"), source);
        for (int model = 0; model < 3; ++model)
        {
            juce::AudioBuffer<float> audio (2, length);
            for (int channel = 0; channel < 2; ++channel)
            {
                const Waveform input = [=] (double n) { return fixture (kind, channel, n); };
                const auto trace = model == 2 ? liveMagnetic (48000.0, input, length)
                    : TransformerAudioPrototype::render (48000.0, input, 16, false, model == 0, {}, length).trace;
                for (int n = 0; n < length; ++n) audio.setSample (channel, n, static_cast<float> (trace.samples[static_cast<std::size_t> (n)]));
            }
            const juce::String suffix = model == 0 ? "linear" : model == 1 ? "magnetic16" : "live_magnetic";
            const auto rawRms = rms (audio);
            const auto rawPeak = audio.getMagnitude (0, length);
            writeWav (root.getChildFile (prefix + "_" + suffix + "_raw.wav"), audio);
            const auto gain = rms (source) / std::max (rawRms, 1.0e-15);
            audio.applyGain (static_cast<float> (gain));
            listening << prefix << ',' << suffix << ',' << db (rawRms) << ',' << db (rawPeak) << ',' << db (gain) << ',' << db (audio.getMagnitude (0, length)) << '\n';
            writeWav (root.getChildFile (prefix + "_" + suffix + "_matched.wav"), audio);
        }
    }
    REQUIRE (listening.good());

    if (std::getenv ("LUMIERE_TRANSFORMER_AUDIO_SKIP_TIMING") != nullptr)
        return; // Accuracy/listening exports may overlap builds; timing may not.

    // Profile last, after all FFT, rendering and file output have finished.
    auto profile = file (root, "timing.csv");
    profile << "sample_rate_hz,channels,ns_per_base_frame,us_per_256_frames,realtime_percent,checksum,solver_failures\n";
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            Processor processor (rate, MagneticPrototype::AudioSettings::factorForRate (rate), channels);
            juce::AudioBuffer<double> block (channels, capacity);
            std::array<double, 5> durations {};
            auto checksum = 0.0;
            for (auto& duration : durations)
            {
                processor.reset();
                const auto start = std::chrono::steady_clock::now();
                for (int offset = 0; offset < 16384; offset += capacity)
                {
                    for (int ch = 0; ch < channels; ++ch)
                        for (int n = 0; n < capacity; ++n)
                            block.setSample (ch, n, 0.7 * std::sin (2 * pi * (ch == 0 ? 60.0 : 73.0) * (offset + n) / rate));
                    processor.process (block);
                    checksum += block.getSample (0, capacity - 1);
                }
                duration = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count() / 16384.0;
            }
            std::sort (durations.begin(), durations.end());
            profile << rate << ',' << channels << ',' << durations[2] << ',' << durations[2] * 256.0 / 1000.0 << ','
                << durations[2] * rate / 1.0e7 << ',' << checksum << ',' << processor.getCircuit().getDiagnostics().solverFailures << '\n';
            CHECK (finite (checksum));
            CHECK (processor.getCircuit().getDiagnostics().solverFailures == 0);
        }
    REQUIRE (profile.good());
}
