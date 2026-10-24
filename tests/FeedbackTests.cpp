#include <PluginProcessor.h>
#include "helpers/CompressorUnderTest.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <cstdint>
#include <limits>

namespace
{
juce::RangedAudioParameter* parameter (PluginProcessor& processor, const juce::String& id)
{
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID == id)
            return ranged;
    return nullptr;
}

bool finite (float value)
{
    // std::isfinite can be optimized away by the project's fast-math flags.
    return (std::bit_cast<std::uint32_t> (value) & 0x7f800000U) != 0x7f800000U;
}

float settledReduction (float inputDb, float thresholdDb, float percent)
{
    constexpr auto slope = 1.0f - 1.0f / SoftKneeGainComputer::ratio;
    const auto feedback = (100.0f - percent) * 0.01f;
    return slope * (inputDb - thresholdDb) / (1.0f + feedback * slope);
}

struct Trace
{
    float last = 0.0f;
    float minimum = std::numeric_limits<float>::max();
    float maximum = 0.0f;
    float tailMinimum = std::numeric_limits<float>::max();
    float tailMaximum = 0.0f;
    float maximumJump = 0.0f;
    bool allFinite = true;
};

Trace renderDc (StereoCompressor& compressor, juce::AudioBuffer<float>& frame,
    float amplitude, int samples, int tailSamples)
{
    Trace result;
    auto previous = compressor.getGainReductionDb();
    for (int i = 0; i < samples; ++i)
    {
        for (int channel = 0; channel < frame.getNumChannels(); ++channel)
            frame.setSample (channel, 0, amplitude * (channel == 0 ? 1.0f : 0.5f));
        compressor.process (frame, frame.getNumChannels());
        result.last = compressor.getGainReductionDb();
        result.allFinite &= finite (result.last);
        for (int channel = 0; channel < frame.getNumChannels(); ++channel)
            result.allFinite &= finite (frame.getSample (channel, 0));
        result.minimum = juce::jmin (result.minimum, result.last);
        result.maximum = juce::jmax (result.maximum, result.last);
        result.maximumJump = juce::jmax (result.maximumJump, std::abs (result.last - previous));
        previous = result.last;
        if (i >= samples - tailSamples)
        {
            result.tailMinimum = juce::jmin (result.tailMinimum, result.last);
            result.tailMaximum = juce::jmax (result.tailMaximum, result.last);
        }
    }
    return result;
}
}

TEST_CASE ("FB FF has stable range default text parsing and host ordering", "[fbff][parameter]")
{
    PluginProcessor processor;
    const std::array oldIds { "input", "threshold", "attack", "release", "makeup", "scFilter",
        "bypass", "inGain", "outGain", "ioLink", "lfm", "mix", "monitor" };
    for (std::size_t i = 0; i < oldIds.size(); ++i)
        CHECK (dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters()[static_cast<int> (i)])->paramID == oldIds[i]);
    auto* blend = parameter (processor, "fbff");
    REQUIRE (dynamic_cast<juce::AudioParameterFloat*> (blend) != nullptr);
    CHECK (blend->getNormalisableRange().start == 0.0f);
    CHECK (blend->getNormalisableRange().end == 100.0f);
    CHECK (blend->getNormalisableRange().interval == Catch::Approx (0.1f));
    CHECK (blend->convertFrom0to1 (blend->getDefaultValue()) == 100.0f);
    for (const auto& [value, text] : std::array {
             std::pair { 0.0f, "FB" }, std::pair { 25.0f, "25% FF" }, std::pair { 49.9f, "49.9% FF" },
             std::pair { 50.0f, "50/50" }, std::pair { 50.1f, "50.1% FF" }, std::pair { 70.0f, "70% FF" },
             std::pair { 99.9f, "99.9% FF" }, std::pair { 100.0f, "FF" } })
    {
        CHECK (blend->getText (blend->convertTo0to1 (value), 64) == text);
        CHECK (blend->convertFrom0to1 (blend->getValueForText (text)) == Catch::Approx (value).margin (0.0001f));
    }
    for (const auto& [text, value] : std::array {
             std::pair { "fb", 0.0f }, std::pair { " FF ", 100.0f }, std::pair { "50/50", 50.0f },
             std::pair { "70", 70.0f }, std::pair { "33.3% FF", 33.3f }, std::pair { "25.1", 25.1f } })
        CHECK (blend->convertFrom0to1 (blend->getValueForText (text)) == Catch::Approx (value).margin (0.0001f));
}

TEST_CASE ("FB FF settled reduction follows the single-detector formula monotonically", "[fbff][static]")
{
    CompressorParameters::Values values;
    values.thresholdDb = -30.0f;
    juce::AudioBuffer<float> frame (2, 1);
    auto previous = std::numeric_limits<float>::max();
    for (float percent : { 100.0f, 75.0f, 50.0f, 25.0f, 0.0f })
    {
        CAPTURE (percent);
        values.fbffPercent = percent;
        StereoCompressor compressor;
        compressor.prepare (48000.0, values);
        const auto trace = renderDc (compressor, frame, juce::Decibels::decibelsToGain (-10.0f), 96000, 2400);
        CHECK (trace.last == Catch::Approx (settledReduction (-10.0f, values.thresholdDb, percent)).margin (0.1f));
        CHECK (trace.last < previous);
        CHECK (trace.allFinite);
        previous = trace.last;
    }
}

TEST_CASE ("FB FF step response settles without sustained oscillation at every sample rate", "[fbff][stability]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto samples = juce::roundToInt (sampleRate * 0.2);
        const auto tail = juce::roundToInt (sampleRate * 0.05);
        for (float percent : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
        {
            CAPTURE (sampleRate, percent);
            CompressorParameters::Values values;
            values.thresholdDb = -30.0f;
            values.attackMs = 0.2f;
            values.releaseMs = 50.0f;
            values.fbffPercent = percent;
            StereoCompressor compressor;
            compressor.prepare (sampleRate, values);
            juce::AudioBuffer<float> frame (2, 1);
            CHECK (renderDc (compressor, frame, 0.0f, tail, tail).maximum == 0.0f);
            const auto high = renderDc (compressor, frame, juce::Decibels::decibelsToGain (-3.0f), samples, tail);
            CHECK (high.allFinite);
            CHECK (high.minimum >= 0.0f);
            // Feedback cannot exceed the open-loop gain computer's maximum
            // reduction for this step; no new clamp is added to the DSP.
            CHECK (high.maximum <= settledReduction (-3.0f, values.thresholdDb, 100.0f) + 0.0001f);
            const auto expected = settledReduction (-3.0f, values.thresholdDb, percent);
            CHECK (high.tailMinimum == Catch::Approx (expected).margin (0.05f));
            CHECK (high.tailMaximum == Catch::Approx (expected).margin (0.05f));
            CHECK (high.tailMaximum - high.tailMinimum < 0.05f);
            // Release is never faster than 20 ms (docs/Overdrive.md), so 20 dB needs
            // a little longer than the 200 ms attack phase to clear.
            const auto low = renderDc (compressor, frame, 0.0f, 2 * samples, tail);
            CHECK (low.allFinite);
            CHECK (low.minimum >= 0.0f);
            CHECK (low.maximum <= high.last);
            CHECK (low.last < 0.05f);
            CHECK (low.tailMaximum - low.tailMinimum < 0.05f);
        }
    }
}

TEST_CASE ("Abrupt FB FF automation ramps smoothly in both directions", "[fbff][smoothing]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CompressorParameters::Values values;
        values.thresholdDb = -30.0f;
        values.attackMs = 0.2f;
        values.releaseMs = 50.0f;
        StereoCompressor compressor;
        compressor.prepare (sampleRate, values);
        juce::AudioBuffer<float> frame (2, 1);
        const auto samples = juce::roundToInt (sampleRate * 0.2);
        const auto tail = juce::roundToInt (sampleRate * 0.05);
        const auto amplitude = juce::Decibels::decibelsToGain (-3.0f);
        renderDc (compressor, frame, amplitude, samples, tail);
        for (float percent : { 0.0f, 25.0f, 100.0f, 50.0f, 100.0f })
        {
            CAPTURE (sampleRate, percent);
            values.fbffPercent = percent;
            compressor.setParameters (values);
            const auto trace = renderDc (compressor, frame, amplitude, samples, tail);
            CHECK (trace.allFinite);
            CHECK (trace.maximumJump <= 0.1f);
            CHECK (trace.last == Catch::Approx (settledReduction (-3.0f, values.thresholdDb, percent)).margin (0.05f));
        }
    }
}

TEST_CASE ("FB FF automation reaches the detector without feeding back makeup", "[fbff][processor]")
{
    // DC drive: the compressor alone (the plugin's input transformer blocks DC).
    CompressorUnderTest processor;
    processor.set ("threshold", -30.0f);
    processor.set ("makeup", 24.0f);
    processor.set ("fbff", 0.0f);
    processor.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> audio (2, 256);
    juce::MidiBuffer midi;
    for (float percent : { 0.0f, 100.0f, 50.0f })
    {
        processor.set ("fbff", percent);
        for (int block = 0; block < 375; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < audio.getNumSamples(); ++i)
                    audio.setSample (channel, i, juce::Decibels::decibelsToGain (-10.0f));
            processor.processBlock (audio, midi);
        }
        CHECK (processor.getGainReductionDb() == Catch::Approx (settledReduction (-10.0f, -30.0f, percent)).margin (0.1f));
    }
}

TEST_CASE ("FB FF extremes stay finite with every detector filter and mono or stereo", "[fbff][finite]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
            for (float percent : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
            {
                CAPTURE (sampleRate, channels, percent);
                CompressorParameters::Values values { 12.0f, -40.0f, 0.2f, 50.0f, 24.0f };
                values.fbffPercent = percent;
                StereoCompressor compressor;
                compressor.prepare (sampleRate, values);
                juce::AudioBuffer<float> frame (channels, 1);
                for (int mode = 0; mode < 7; ++mode)
                {
                    values.scFilterMode = mode;
                    // Include both extremes of attack/release and gain staging.
                    values.attackMs = mode % 2 == 0 ? 0.2f : 30.0f;
                    values.releaseMs = mode % 2 == 0 ? 50.0f : 2000.0f;
                    values.inputDb = mode % 2 == 0 ? 12.0f : -12.0f;
                    values.makeupDb = mode % 2 == 0 ? 24.0f : 0.0f;
                    compressor.setParameters (values);
                    for (float amplitude : { 100.0f, 0.0f, 1.0e-30f, -100.0f })
                    {
                        CAPTURE (mode, amplitude);
                        const auto trace = renderDc (compressor, frame, amplitude, 2048, 256);
                        CHECK (trace.allFinite);
                        CHECK (trace.minimum >= 0.0f);
                    }
                }
            }
}
