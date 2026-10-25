#include <PluginProcessor.h>
#include <dsp/SidechainFilter.h>
#include "data/UnfilteredAudioReference.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace
{
// Captured feedforward, FB/FF's default before the analog detector (docs/Detector.md).
juce::AudioBuffer<float> renderUnfilteredReference (float fbffPercent = 100.0f)
{
    StereoCompressor compressor;
    CompressorParameters::Values values { -3.0f, -22.0f, 0.2f, 130.0f, 6.0f };
    values.fbffPercent = fbffPercent;
    compressor.prepare (48000.0, values);
    juce::AudioBuffer<float> result (2, 8192);
    juce::AudioBuffer<float> block (2, 128);
    for (int start = 0; start < result.getNumSamples(); start += block.getNumSamples())
    {
        if (start == 2048)
            values = { 6.0f, -30.0f, 5.0f, 2000.0f, 12.0f };
        if (start == 4096)
            values = { 0.0f, -18.0f, 30.0f, 50.0f, 0.0f };
        values.fbffPercent = fbffPercent;
        compressor.setParameters (values);
        for (int i = 0; i < block.getNumSamples(); ++i)
        {
            const auto sample = start + i;
            const auto amplitude = (sample / 512) % 2 == 0 ? 1.0f : 0.015625f;
            block.setSample (0, i, static_cast<float> (sample % 97 - 48) / 64.0f * amplitude);
            block.setSample (1, i, static_cast<float> (sample % 71 - 35) / 128.0f * amplitude);
        }
        compressor.process (block, 2);
        for (int channel = 0; channel < 2; ++channel)
            result.copyFrom (channel, start, block, channel, 0, block.getNumSamples());
    }
    return result;
}

double responseDb (const SidechainFilter& filter, int mode, double frequency, double sampleRate = 48000.0)
{
    const auto z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * frequency / sampleRate);
    const auto z2 = z * z;
    std::complex<double> response { 1.0, 0.0 };
    for (const auto& c : filter.getCoefficients (mode))
        response *= (c.b0 + c.b1 * z + c.b2 * z2) / (1.0 + c.a1 * z + c.a2 * z2);
    return juce::Decibels::gainToDecibels (std::abs (response), -300.0);
}

bool isFiniteValue (double sample)
{
    // Bit tests remain meaningful with the project's fast-math flags enabled.
    return (std::bit_cast<std::uint64_t> (sample) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}

bool bufferIsFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if ((std::bit_cast<std::uint32_t> (buffer.getSample (channel, sample)) & 0x7f800000U) == 0x7f800000U)
                return false;
    return true;
}
}

TEST_CASE ("Off preserves the pre-filter compressor audio reference", "[sidechain][sc-reference]")
{
    const auto audio = renderUnfilteredReference();
    for (std::size_t i = 0; i < UnfilteredAudioReference::frames.size(); ++i)
    {
        const auto frame = UnfilteredAudioReference::frames[i];
        CHECK (audio.getSample (0, frame) == Catch::Approx (UnfilteredAudioReference::channel0[i]).margin (0.0000001f));
        CHECK (audio.getSample (1, frame) == Catch::Approx (UnfilteredAudioReference::channel1[i]).margin (0.0000001f));
    }
    // Optional full-resolution export for a bit-for-bit before/after comparison.
    if (const auto* path = std::getenv ("LUMIERE_SC_AUDIO_EXPORT"))
    {
        std::ofstream output (path, std::ios::binary);
        for (int channel = 0; channel < 2; ++channel)
            output.write (reinterpret_cast<const char*> (audio.getReadPointer (channel)),
                static_cast<std::streamsize> (audio.getNumSamples()) * static_cast<std::streamsize> (sizeof (float)));
        REQUIRE (output.good());
    }
}

TEST_CASE ("Full feedforward is bit-identical to the audio captured before FB FF", "[fbff][audio]")
{
    // Full stereo float PCM captured before this change, including parameter
    // automation, transients, and release tails. No second DSP implementation.
    // Recaptured 2026-10-24 for the 20 ms release floor and Release Mod.
    const auto path = juce::File (__FILE__).getSiblingFile ("data").getChildFile ("FeedforwardAudioReference.bin");
    std::ifstream input (path.getFullPathName().toStdString(), std::ios::binary);
    REQUIRE (input.good());
    const auto audio = renderUnfilteredReference (100.0f);
    std::array<float, 8192> reference {};
    for (int channel = 0; channel < 2; ++channel)
    {
        input.read (reinterpret_cast<char*> (reference.data()), sizeof (reference));
        REQUIRE (input.good());
        CHECK (std::memcmp (reference.data(), audio.getReadPointer (channel), sizeof (reference)) == 0);
    }
}

TEST_CASE ("Off detector bypass is exact and every filter leaves uncompressed audio unchanged", "[sidechain][audio]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int channels : { 1, 2 })
        {
            juce::AudioBuffer<float> original (channels, 4096);
            for (int channel = 0; channel < channels; ++channel)
                for (int sample = 0; sample < original.getNumSamples(); ++sample)
                    original.setSample (channel, sample, static_cast<float> ((sample * (channel + 1)) % 97 - 48) * 0.000001f);
            for (int mode = 0; mode < 7; ++mode)
            {
                CAPTURE (sampleRate, channels, mode);
                CompressorParameters::Values values;
                values.scFilterMode = mode;
                StereoCompressor compressor;
                compressor.prepare (sampleRate, values);
                juce::AudioBuffer<float> audio;
                audio.makeCopyOf (original);
                CHECK (compressor.process (audio, channels) == 0.0f);
                for (int channel = 0; channel < channels; ++channel)
                    CHECK (std::memcmp (audio.getReadPointer (channel), original.getReadPointer (channel),
                        static_cast<std::size_t> (audio.getNumSamples()) * sizeof (float)) == 0);
            }
        }
    }
    SidechainFilter filter;
    filter.prepare (48000.0);
    for (double value : { -100.0, -0.1, 0.0, 0.1, 100.0, 1.0e-30 })
        CHECK (std::bit_cast<std::uint64_t> (filter.processSample (0, value)) == std::bit_cast<std::uint64_t> (value));
}

TEST_CASE ("High-pass and band-pass prepared coefficients match the specified responses", "[sidechain][response]")
{
    SidechainFilter filter;
    filter.prepare (48000.0);
    for (const auto& [mode, cutoff] : std::array { std::pair { 1, 80.0 }, std::pair { 2, 120.0 } })
    {
        CHECK (responseDb (filter, mode, cutoff) == Catch::Approx (-3.0).margin (0.5));
        CHECK (responseDb (filter, mode, cutoff * 0.5) == Catch::Approx (-12.0).margin (1.5));
        CHECK (responseDb (filter, mode, 1000.0) == Catch::Approx (0.0).margin (0.3));
    }
    CHECK (responseDb (filter, 5, 730.0) == Catch::Approx (0.0).margin (0.3));
    CHECK (responseDb (filter, 5, 100.0) == Catch::Approx (-3.3).margin (0.7));
    CHECK (responseDb (filter, 5, 20.0) == Catch::Approx (-15.0).margin (2.0));
}

TEST_CASE ("Custom and tilt prepared coefficients match the specified responses", "[sidechain][response]")
{
    SidechainFilter filter;
    filter.prepare (48000.0);
    CHECK (responseDb (filter, 6, 20.0) == Catch::Approx (-5.5).margin (1.0));
    CHECK (responseDb (filter, 6, 712.0) == Catch::Approx (1.0).margin (1.0));
    for (int mode : { 3, 4 })
    {
        CHECK (responseDb (filter, mode, 20.0) < -4.5);
        CHECK (responseDb (filter, mode, 20000.0) > 4.5);
        CHECK (std::abs (responseDb (filter, mode, 730.0)) <= 1.0);
    }
}

TEST_CASE ("Mode changes reset both channels and repeated modes preserve filter history", "[sidechain][state]")
{
    SidechainFilter filter;
    filter.prepare (48000.0);
    filter.setMode (6);
    filter.processSample (0, 1.0);
    filter.setMode (6);
    CHECK (std::abs (filter.processSample (0, 0.0)) > 0.000001);
    CHECK (filter.processSample (1, 0.0) == 0.0);
    filter.processSample (1, 1.0);
    filter.setMode (1);
    CHECK (filter.processSample (0, 0.0) == 0.0);
    CHECK (filter.processSample (1, 0.0) == 0.0);
}

TEST_CASE ("SC Filter choice changes detection and never filters the output directly", "[sidechain][parameter]")
{
    const auto render = [] (int mode)
    {
        PluginProcessor processor;
        for (auto* parameter : processor.getParameters())
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
                if (choice->paramID == "scFilter")
                    choice->setValueNotifyingHost (choice->convertTo0to1 (static_cast<float> (mode)));
        processor.prepareToPlay (48000.0, 48000);
        juce::AudioBuffer<float> audio (2, 48000);
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto value = static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 20.0 * sample / 48000.0));
            audio.setSample (0, sample, value);
            audio.setSample (1, sample, value);
        }
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        return processor.getGainReductionDb();
    };
    CHECK (render (0) > 5.0f);
    CHECK (render (1) < 1.0f);
}

TEST_CASE ("All modes stay finite at supported sample rates through stream mode changes", "[sidechain][finite]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        SidechainFilter filter;
        filter.prepare (sampleRate);
        StereoCompressor compressor;
        CompressorParameters::Values values { 12.0f, -40.0f, 0.2f, 2000.0f, 24.0f };
        compressor.prepare (sampleRate, values);
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int mode = 0; mode < 7; ++mode)
            {
                for (float amplitude : { 100.0f, 0.0f, 1.0e-30f })
                {
                    CAPTURE (sampleRate, pass, mode, amplitude);
                    filter.setMode (mode);
                    values.scFilterMode = mode;
                    compressor.setParameters (values);
                    juce::AudioBuffer<float> audio (2, 4096);
                    bool detectorFinite = true;
                    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                    {
                        const auto left = sample % 7 < 3 ? amplitude : -amplitude;
                        const auto right = amplitude * 0.5f;
                        detectorFinite = detectorFinite && isFiniteValue (filter.processSample (0, left))
                            && isFiniteValue (filter.processSample (1, right));
                        audio.setSample (0, sample, left);
                        audio.setSample (1, sample, right);
                    }
                    CHECK (detectorFinite);
                    CHECK (isFiniteValue (compressor.process (audio, 2)));
                    CHECK (bufferIsFinite (audio));
                }
            }
        }
    }
}

TEST_CASE ("Print sidechain magnitude response table at 48 kHz", "[sidechain][response-table]")
{
    SidechainFilter filter;
    filter.prepare (48000.0);
    constexpr std::array frequencies { 20.0, 50.0, 100.0, 250.0, 500.0, 730.0, 1000.0, 2000.0, 5000.0, 10000.0, 15800.0, 20000.0 };
    std::cout << "\nMagnitude response in dB at 48 kHz\n| Mode | 20 | 50 | 100 | 250 | 500 | 730 | 1k | 2k | 5k | 10k | 15.8k | 20k |\n";
    std::cout << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (int mode = 0; mode < 7; ++mode)
    {
        std::cout << "| " << SidechainFilterSettings::modes[static_cast<std::size_t> (mode)].name << " |";
        for (double frequency : frequencies)
        {
            const auto db = responseDb (filter, mode, frequency);
            CHECK (isFiniteValue (db));
            std::cout << ' ' << std::fixed << std::setprecision (2) << (std::abs (db) < 0.005 ? 0.0 : db) << " |";
        }
        std::cout << '\n';
    }
}
