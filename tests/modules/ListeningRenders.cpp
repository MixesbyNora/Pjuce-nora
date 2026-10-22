#include "ResearchHarness.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iomanip>

namespace
{
using namespace ModuleResearch;
constexpr double pi = juce::MathConstants<double>::pi;
double stimulus (int material, int channel, double sample)
{
    constexpr double rate = 48000;
    const auto time = sample / rate;
    if (time < 0 || time >= 1) return 0;
    const auto fade = std::min ({ 1.0, time / 0.01, (1 - time) / 0.025 });
    const auto beat = std::fmod (time, 0.25);
    if (material == 0)
    {
        constexpr std::array notes { 55.0, 65.4064, 82.4069, 73.4162 };
        const auto hz = notes[static_cast<std::size_t> (static_cast<int> (time * 4) % 4)] * (channel == 0 ? 1.0 : 1.002);
        return fade * 0.8 * (1 - std::exp (-beat / 0.003)) * std::exp (-beat / 0.2)
            * (std::sin (2 * pi * hz * beat) + 0.2 * std::sin (4 * pi * hz * beat));
    }
    if (material == 1)
    {
        auto state = static_cast<std::uint32_t> (std::max (0.0, sample)) * 1664525U + 1013904223U + static_cast<std::uint32_t> (channel) * 1234567U;
        state ^= state >> 16; state *= 2246822519U;
        const auto noise = static_cast<double> (state) / 4294967295.0 * 2 - 1;
        return fade * (0.75 * std::exp (-beat * 30) * std::sin (2 * pi * (55 * beat + 1.5 * (1 - std::exp (-beat * 50))))
            + 0.2 * noise * std::exp (-beat * 70));
    }
    const auto hz = 120 * (1 + 0.015 * std::sin (2 * pi * 5 * time));
    double value = 0;
    for (int harmonic = 1; harmonic <= 40; ++harmonic)
    {
        const auto f = harmonic * 120.0;
        const auto weight = std::exp (-std::pow ((f - 700) / 250, 2)) + 0.7 * std::exp (-std::pow ((f - 1200) / 300, 2)) + 0.3 * std::exp (-std::pow ((f - 2600) / 500, 2));
        value += weight / harmonic * std::sin (2 * pi * harmonic * hz * time + channel * 0.015);
    }
    return fade * 1.5 * (0.6 + 0.4 * std::sin (2 * pi * 3 * time)) * value;
}
struct FileRender
{
    std::string name;
    juce::AudioBuffer<float> audio { 2, 48000 };
    double rms = 0, peak = 0, gain = 1;
};
FileRender renderFile (int material, Variant variant, Topology topology, Coupling coupling, double resistance)
{
    const auto factor = coupling == Coupling::reference ? 64 : 8;
    Processor processor (48000, factor, 2, variant, topology, {}, coupling, resistance);
    constexpr int warm = 4096, frames = 48000;
    constexpr int count = ((warm + frames + 1024 + capacity - 1) / capacity) * capacity;
    std::array<std::vector<double>, 2> raw;
    for (auto& channel : raw) channel.resize (count);
    juce::AudioBuffer<double> block (2, capacity);
    for (int start = 0; start < count; start += capacity)
    {
        for (int c = 0; c < 2; ++c) for (int n = 0; n < capacity; ++n) block.setSample (c, n, stimulus (material, c, start + n - warm));
        processor.process (block);
        for (int c = 0; c < 2; ++c) std::copy_n (block.getReadPointer (c), capacity, raw[static_cast<std::size_t> (c)].data() + start);
    }
    FileRender result;
    result.name = std::string (material == 0 ? "bass" : material == 1 ? "drums" : "vocal-like") + "_" + name (variant) + "_" + name (topology) + "_mode-" + name (coupling) + "_source-" + std::to_string (static_cast<int> (resistance));
    for (int c = 0; c < 2; ++c)
    {
        const auto aligned = MagneticValidation::alignTrace (raw[static_cast<std::size_t> (c)], warm, processor.latency(), frames);
        for (int n = 0; n < frames; ++n)
        {
            const auto x = aligned.samples[static_cast<std::size_t> (n)];
            result.audio.setSample (c, n, static_cast<float> (x)); result.rms += x * x; result.peak = std::max (result.peak, std::abs (x));
        }
    }
    result.rms = std::sqrt (result.rms / (2 * frames));
    CHECK (processor.diagnostics().failures == 0); return result;
}
void write (const juce::File& path, const juce::AudioBuffer<float>& audio)
{
    auto stream = path.createOutputStream(); REQUIRE (stream != nullptr);
    REQUIRE (stream->setPosition (0)); REQUIRE (stream->truncate().wasOk());
    std::unique_ptr<juce::OutputStream> output = std::move (stream);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (output, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    REQUIRE (writer != nullptr); REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
}
}

TEST_CASE ("Export level matched experimental module listening pairs", "[module-renders]")
{
    const auto* directory = std::getenv ("LUMIERE_MODULE_REPORT_DIR");
    if (! directory) { SUCCEED ("Explicit opt-in render export"); return; }
    const juce::File root (juce::String::fromUTF8 (directory));
    const auto audioRoot = root.getChildFile ("audio"); REQUIRE (audioRoot.createDirectory().wasOk());
    std::ofstream report (root.getChildFile ("renders.csv").getFullPathName().toStdString());
    report << "kind,file,raw_rms,raw_peak,applied_gain,matched_rms,matched_peak,rate_hz,channels,frames\n" << std::setprecision (17);
    for (int material = 0; material < 3; ++material)
    {
        std::vector<FileRender> renders;
        for (auto variant : { Variant::smooth, Variant::play3 })
            for (auto topology : { Topology::shunt, Topology::series })
            {
                renders.push_back (renderFile (material, variant, topology, Coupling::transformer, 1000));
                for (double resistance : { 10.0, 1000.0, 100000.0 })
                    for (auto coupling : { Coupling::buffered, Coupling::divider, Coupling::loadAware, Coupling::reference })
                        renders.push_back (renderFile (material, variant, topology, coupling, resistance));
            }
        auto target = 0.1;
        for (const auto& render : renders) target = std::min (target, 0.9 * render.rms / std::max (render.peak, 1.0e-30));
        for (auto& render : renders)
        {
            render.gain = target / std::max (render.rms, 1.0e-30); render.audio.applyGain (static_cast<float> (render.gain));
            write (audioRoot.getChildFile (juce::String (render.name) + ".wav"), render.audio);
            report << "measurement,audio/" << render.name << ".wav," << render.rms << ',' << render.peak << ',' << render.gain << ',' << render.rms * render.gain << ',' << render.peak * render.gain << ",48000,2,48000\n";
        }
        report.flush();
    }
    REQUIRE (report.good());
}
