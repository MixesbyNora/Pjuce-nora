#include <PluginProcessor.h>
#include <dsp/SoftKneeGainComputer.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
juce::RangedAudioParameter* findParameter (PluginProcessor& processor, const juce::String& id)
{
    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (ranged->paramID == id)
                return ranged;
    }
    return nullptr;
}

void setParameter (PluginProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = findParameter (processor, id);
    REQUIRE (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

juce::AudioBuffer<float> renderConstant (PluginProcessor& processor, int samples, float left, float right)
{
    juce::AudioBuffer<float> buffer (2, samples);
    juce::FloatVectorOperations::fill (buffer.getWritePointer (0), left, samples);
    juce::FloatVectorOperations::fill (buffer.getWritePointer (1), right, samples);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    return buffer;
}

int samplesFor (double sampleRate, double milliseconds)
{
    return juce::roundToInt (sampleRate * milliseconds * 0.001);
}
}

TEST_CASE ("Compressor exposes the five stable parameters and persists state", "[compressor][state]")
{
    PluginProcessor processor;
    REQUIRE (processor.getParameters().size() == 5);
    CHECK (findParameter (processor, "gain") == nullptr);

    struct Expected
    {
        const char* id;
        float minimum;
        float maximum;
        float initial;
    };
    for (const auto& expected : std::array {
             Expected { "input", -12.0f, 12.0f, 0.0f },
             Expected { "threshold", -40.0f, 0.0f, -18.0f },
             Expected { "attack", 0.2f, 30.0f, 5.0f },
             Expected { "release", 50.0f, 2000.0f, 200.0f },
             Expected { "makeup", 0.0f, 24.0f, 0.0f } })
    {
        auto* parameter = findParameter (processor, expected.id);
        REQUIRE (dynamic_cast<juce::AudioParameterFloat*> (parameter) != nullptr);
        CHECK (parameter->getNormalisableRange().start == Catch::Approx (expected.minimum));
        CHECK (parameter->getNormalisableRange().end == Catch::Approx (expected.maximum));
        CHECK (parameter->convertFrom0to1 (parameter->getValue()) == Catch::Approx (expected.initial).margin (0.001f));
    }

    setParameter (processor, "input", 3.0f);
    setParameter (processor, "threshold", -24.0f);
    setParameter (processor, "attack", 10.0f);
    setParameter (processor, "release", 500.0f);
    setParameter (processor, "makeup", 6.0f);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    PluginProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    for (auto* parameter : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);
        auto* restoredParameter = findParameter (restored, ranged->paramID);
        REQUIRE (restoredParameter != nullptr);
        CHECK (restoredParameter->getValue() == Catch::Approx (parameter->getValue()));
    }
}

TEST_CASE ("Signal below the knee passes unchanged with unity input and makeup", "[compressor]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    auto buffer = renderConstant (processor, 256, 0.01f, -0.03f);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        CHECK (buffer.getSample (0, sample) == Catch::Approx (0.01f).margin (0.00000001f));
        CHECK (buffer.getSample (1, sample) == Catch::Approx (-0.03f).margin (0.00000001f));
    }
    CHECK (processor.getGainReductionDb() == 0.0f);
}

TEST_CASE ("Above-threshold signal reaches the fixed four-to-one gain reduction", "[compressor]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    const auto level = juce::Decibels::decibelsToGain (-6.0f);
    const auto buffer = renderConstant (processor, samplesFor (48000.0, 100.0), level, level);
    // -6 dB in, -18 dB threshold: 12 dB over / 4 = 3 dB over at the output.
    const auto expectedOutput = juce::Decibels::decibelsToGain (-15.0f);
    CHECK (buffer.getSample (0, buffer.getNumSamples() - 1) == Catch::Approx (expectedOutput).margin (0.00001f));
    CHECK (processor.getGainReductionDb() == Catch::Approx (9.0f).margin (0.001f));
}

TEST_CASE ("Stereo link follows either louder channel and preserves channel balance", "[compressor][stereo]")
{
    for (bool rightIsLouder : { false, true })
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 256);
        const auto left = rightIsLouder ? -0.025f : 0.5f;
        const auto right = rightIsLouder ? 0.5f : -0.025f;
        const auto buffer = renderConstant (processor, 2048, left, right);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto leftGain = buffer.getSample (0, sample) / left;
            const auto rightGain = buffer.getSample (1, sample) / right;
            CHECK (leftGain == Catch::Approx (rightGain).margin (0.000001f));
        }
        CHECK (processor.getGainReductionDb() > 8.9f);
    }
}

TEST_CASE ("Attack and release use dB time constants independent of sample rate", "[compressor][ballistics]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CAPTURE (sampleRate);
        PluginProcessor processor;
        setParameter (processor, "attack", 10.0f);
        setParameter (processor, "release", 200.0f);
        processor.prepareToPlay (sampleRate, 256);
        const auto high = juce::Decibels::decibelsToGain (-6.0f);

        renderConstant (processor, samplesFor (sampleRate, 5.0), high, high);
        const auto earlyAttack = processor.getGainReductionDb();
        CHECK (earlyAttack > 3.4f);
        CHECK (earlyAttack < 3.7f);
        renderConstant (processor, samplesFor (sampleRate, 5.0), high, high);
        const auto attackAtTau = processor.getGainReductionDb();
        CHECK (attackAtTau > earlyAttack);
        CHECK (attackAtTau == Catch::Approx (9.0 * (1.0 - std::exp (-1.0))).margin (0.02));

        renderConstant (processor, samplesFor (sampleRate, 100.0), high, high);
        const auto settled = processor.getGainReductionDb();
        CHECK (settled == Catch::Approx (9.0f).margin (0.001f));
        renderConstant (processor, samplesFor (sampleRate, 100.0), 0.0f, 0.0f);
        const auto earlyRelease = processor.getGainReductionDb();
        CHECK (earlyRelease < settled);
        CHECK (earlyRelease == Catch::Approx (settled * std::exp (-0.5)).margin (0.02));
        renderConstant (processor, samplesFor (sampleRate, 100.0), 0.0f, 0.0f);
        CHECK (processor.getGainReductionDb() < earlyRelease);
        CHECK (processor.getGainReductionDb() == Catch::Approx (settled * std::exp (-1.0)).margin (0.02));
        renderConstant (processor, samplesFor (sampleRate, 1800.0), 0.0f, 0.0f);
        CHECK (processor.getGainReductionDb() < 0.001f);
    }
}

TEST_CASE ("Automating attack and release selects prepared time coefficients", "[compressor][ballistics]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    setParameter (processor, "attack", 30.0f);
    setParameter (processor, "release", 50.0f);
    const auto high = juce::Decibels::decibelsToGain (-6.0f);
    renderConstant (processor, samplesFor (48000.0, 30.0), high, high);
    CHECK (processor.getGainReductionDb() == Catch::Approx (9.0 * (1.0 - std::exp (-1.0))).margin (0.01));
    const auto beforeRelease = processor.getGainReductionDb();
    renderConstant (processor, samplesFor (48000.0, 50.0), 0.0f, 0.0f);
    CHECK (processor.getGainReductionDb() == Catch::Approx (beforeRelease * std::exp (-1.0)).margin (0.01));
}

TEST_CASE ("Soft knee joins unity and four-to-one curves continuously", "[compressor][curve]")
{
    SoftKneeGainComputer curve;
    curve.setThresholdDb (-18.0f);
    CHECK (curve.getGainReductionDb (-30.0f) == 0.0f);
    CHECK (curve.getGainReductionDb (-21.0f) == 0.0f);
    CHECK (curve.getGainReductionDb (-18.0f) == Catch::Approx (0.5625f));
    CHECK (curve.getGainReductionDb (-15.0f) == Catch::Approx (2.25f));
    CHECK (curve.getGainReductionDb (-6.0f) == Catch::Approx (9.0f));
    CHECK (curve.getGainReductionDb (-21.0f + 0.001f) < 0.00001f);
    CHECK (curve.getGainReductionDb (-15.0f - 0.001f) == Catch::Approx (2.25f).margin (0.001f));
}

TEST_CASE ("Input drives detection and makeup follows gain reduction", "[compressor][gain]")
{
    PluginProcessor processor;
    setParameter (processor, "input", 6.0f);
    setParameter (processor, "makeup", 3.0f);
    processor.prepareToPlay (48000.0, 256);
    const auto level = juce::Decibels::decibelsToGain (-12.0f);
    const auto buffer = renderConstant (processor, 4800, level, level);
    // Input reaches -6 dB, compression takes off 9 dB, makeup adds 3 dB.
    CHECK (processor.getGainReductionDb() == Catch::Approx (9.0f).margin (0.001f));
    CHECK (buffer.getSample (0, 4799) == Catch::Approx (level).margin (0.00001f));
}

TEST_CASE ("Input and makeup automation ramp in dB over twenty milliseconds", "[compressor][gain]")
{
    for (const auto* id : { "input", "makeup" })
    {
        PluginProcessor processor;
        processor.prepareToPlay (48000.0, 256);
        setParameter (processor, id, 6.0f);
        const auto buffer = renderConstant (processor, 960, 0.001f, -0.001f);
        CHECK (buffer.getSample (0, 0) < 0.00101f);
        CHECK (buffer.getSample (0, 479) == Catch::Approx (0.001f * juce::Decibels::decibelsToGain (3.0f)).margin (0.000001f));
        CHECK (buffer.getSample (0, 959) == Catch::Approx (0.001f * juce::Decibels::decibelsToGain (6.0f)).margin (0.000001f));
        CHECK (processor.getGainReductionDb() == 0.0f);
    }
}
