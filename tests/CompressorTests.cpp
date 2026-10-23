#include <PluginProcessor.h>
#include <dsp/SoftKneeGainComputer.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <cstdint>

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

DbGainEnvelope primedEnvelope (double sampleRate, float initialReductionDb, float releaseMs)
{
    DbGainEnvelope envelope;
    envelope.prepare (sampleRate);
    envelope.setTimes (0.2f, releaseMs);
    for (int sample = 0; sample < samplesFor (sampleRate, 20.0); ++sample)
        envelope.process (initialReductionDb);
    REQUIRE (envelope.getGainReductionDb() == Catch::Approx (initialReductionDb).margin (0.000001f));
    return envelope;
}

bool bufferIsFinite (const juce::AudioBuffer<float>& buffer)
{
    // Check IEEE exponent bits: fast-math compiler flags can otherwise optimize
    // std::isfinite away, defeating the NaN/Inf regression test.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if ((std::bit_cast<std::uint32_t> (buffer.getSample (channel, sample)) & 0x7f800000U) == 0x7f800000U)
                return false;
    return true;
}
}

TEST_CASE ("Compressor and BA bar expose stable parameters and persist state", "[compressor][state]")
{
    PluginProcessor processor;
    REQUIRE (processor.getParameters().size() == 17);
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
             Expected { "makeup", 0.0f, 24.0f, 0.0f },
             Expected { "fbff", 0.0f, 100.0f, 100.0f } })
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
    setParameter (processor, "fbff", 25.0f);
    setParameter (processor, "xfrmOut", 1.0f);
    setParameter (processor, "xfrmAmount", 36.5f);
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (findParameter (processor, "scFilter"));
    REQUIRE (choice != nullptr);
    CHECK (choice->getName (32) == "SC Filter");
    CHECK (choice->getIndex() == 0);
    const juce::StringArray expectedChoices { "Off", "HP 80 Hz", "HP 120 Hz", "Tilt Wide", "Tilt Focused", "Band 730 Hz", "Custom" };
    CHECK (choice->choices == expectedChoices);
    setParameter (processor, "scFilter", 6.0f);
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
        CHECK (buffer.getSample (0, sample) == Catch::Approx (sample < processor.getLatencySamples() ? 0.0f : 0.01f).margin (0.00000001f));
        CHECK (buffer.getSample (1, sample) == Catch::Approx (sample < processor.getLatencySamples() ? 0.0f : -0.03f).margin (0.00000001f));
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

TEST_CASE ("Attack retains its dB time constant independent of sample rate", "[compressor][ballistics]")
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
        CHECK (processor.getGainReductionDb() == Catch::Approx (9.0f).margin (0.001f));
    }
}

TEST_CASE ("Attack automation still selects prepared time coefficients", "[compressor][ballistics]")
{
    PluginProcessor processor;
    processor.prepareToPlay (48000.0, 256);
    setParameter (processor, "attack", 30.0f);
    setParameter (processor, "release", 50.0f);
    const auto high = juce::Decibels::decibelsToGain (-6.0f);
    renderConstant (processor, samplesFor (48000.0, 30.0), high, high);
    CHECK (processor.getGainReductionDb() == Catch::Approx (9.0 * (1.0 - std::exp (-1.0))).margin (0.01));
    const auto beforeRelease = processor.getGainReductionDb();
    const auto releaseBuffer = renderConstant (processor, samplesFor (48000.0, 50.0), 0.01f, 0.01f);
    const auto finalReduction = -juce::Decibels::gainToDecibels (
        releaseBuffer.getSample (0, releaseBuffer.getNumSamples() - 1) / 0.01f);
    CHECK (finalReduction < beforeRelease * std::exp (-1.0));
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
        const auto buffer = renderConstant (processor, 960 + processor.getLatencySamples(), 0.001f, -0.001f);
        CHECK (buffer.getSample (0, 0) < 0.00101f);
        CHECK (buffer.getSample (0, 479 + processor.getLatencySamples()) == Catch::Approx (0.001f * juce::Decibels::decibelsToGain (3.0f)).margin (0.000001f));
        CHECK (buffer.getSample (0, 959 + processor.getLatencySamples()) == Catch::Approx (0.001f * juce::Decibels::decibelsToGain (6.0f)).margin (0.000001f));
        CHECK (processor.getGainReductionDb() == 0.0f);
    }
}

TEST_CASE ("Deep gain reduction releases faster than shallow reduction", "[compressor][release]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CAPTURE (sampleRate);
        auto deep = primedEnvelope (sampleRate, 18.0f, 200.0f);
        auto shallow = primedEnvelope (sampleRate, 0.1f, 200.0f);
        for (int sample = 0; sample < samplesFor (sampleRate, 1.0); ++sample)
        {
            deep.process (0.0f);
            shallow.process (0.0f);
        }
        const auto deepFraction = deep.getGainReductionDb() / 18.0f;
        const auto shallowFraction = shallow.getGainReductionDb() / 0.1f;
        CHECK (deepFraction < shallowFraction);
        CHECK (deepFraction < 0.5f);
        CHECK (shallowFraction > 0.99f);
    }
}

TEST_CASE ("Effective release stays between one sample and the Release setting", "[compressor][release]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (float releaseMs : { 50.0f, 200.0f, 2000.0f })
        {
            for (float initialDb : { 0.001f, 0.1f, 1.0f, 6.0f, 12.0f, 24.0f })
            {
                CAPTURE (sampleRate, releaseMs, initialDb);
                for (float targetDb : { 0.0f, initialDb * 0.25f })
                {
                    auto envelope = primedEnvelope (sampleRate, initialDb, releaseMs);
                    const auto slowestCoefficient = std::exp (-1000.0 / (sampleRate * releaseMs));
                    for (int sample = 0; sample < 128; ++sample)
                    {
                        const auto before = envelope.getGainReductionDb();
                        const auto after = envelope.process (targetDb);
                        const auto slowest = targetDb + (before - targetDb) * slowestCoefficient;
                        const auto fastest = targetDb + (before - targetDb) * std::exp (-1.0);
                        CHECK (after <= slowest + 0.000001);
                        CHECK (after >= fastest - 0.000001);
                    }
                }
            }
        }
    }
}

TEST_CASE ("Release modulation reaches the geometric midpoint and clamps at full depth", "[compressor][release]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        constexpr auto releaseMs = 200.0f;
        auto halfDepth = primedEnvelope (sampleRate, 6.0f, releaseMs);
        const auto midpointMs = std::sqrt ((1000.0 / sampleRate) * releaseMs);
        const auto midpointCoefficient = std::exp (-1000.0 / (sampleRate * midpointMs));
        CHECK (halfDepth.process (0.0f) == Catch::Approx (6.0 * midpointCoefficient).margin (0.000001));
        for (float depth : { 12.0f, 24.0f })
        {
            auto envelope = primedEnvelope (sampleRate, depth, releaseMs);
            CHECK (envelope.process (0.0f) == Catch::Approx (depth * std::exp (-1.0)).margin (0.000001));
        }
    }
}

TEST_CASE ("Very shallow reduction releases close to the user time constant", "[compressor][release]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (float releaseMs : { 50.0f, 200.0f, 2000.0f })
        {
            CAPTURE (sampleRate, releaseMs);
            auto envelope = primedEnvelope (sampleRate, 0.001f, releaseMs);
            for (int sample = 0; sample < samplesFor (sampleRate, releaseMs); ++sample)
                envelope.process (0.0f);
            const auto remainingFraction = envelope.getGainReductionDb() / 0.001f;
            CHECK (remainingFraction <= std::exp (-1.0) + 0.00001);
            CHECK (remainingFraction >= std::exp (-1.0) * 0.995);
        }
    }
}

TEST_CASE ("Attack matches the original exponential across its full parameter range", "[compressor][ballistics]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (float attackMs : { 0.2f, 5.0f, 30.0f })
        {
            CAPTURE (sampleRate, attackMs);
            DbGainEnvelope envelope;
            envelope.prepare (sampleRate);
            envelope.setTimes (attackMs, 50.0f);
            constexpr auto targetDb = 18.0f;
            const auto coefficient = std::exp (-1000.0 / (sampleRate * attackMs));
            const auto samples = samplesFor (sampleRate, 2.0 * attackMs);
            for (int sample = 0; sample < samples; ++sample)
            {
                // Changing Release during attack must not alter the attack trajectory.
                envelope.setTimes (attackMs, sample % 2 == 0 ? 50.0f : 2000.0f);
                const auto expected = targetDb * (1.0 - std::pow (coefficient, sample + 1));
                CHECK (envelope.process (targetDb) == Catch::Approx (expected).margin (0.00001));
            }
        }
    }
}

TEST_CASE ("Output stays finite at all parameter endpoint combinations", "[compressor][release][finite]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        // Exhaust all 2^5 combinations of parameter extremes, then automate
        // to the opposite extremes with compression already active.
        for (unsigned int combination = 0; combination < 32; ++combination)
        {
            CAPTURE (sampleRate, combination);
            PluginProcessor processor;
            unsigned int bit = 1;
            for (const auto& spec : CompressorParameters::specifications)
            {
                setParameter (processor, spec.id, (combination & bit) != 0 ? spec.maximum : spec.minimum);
                bit <<= 1;
            }
            processor.prepareToPlay (sampleRate, 256);
            CHECK (bufferIsFinite (renderConstant (processor, samplesFor (sampleRate, 120.0), 100.0f, -0.1f)));
            CHECK (bufferIsFinite (renderConstant (processor, 256, 0.0f, 0.0f)));
            bit = 1;
            for (const auto& spec : CompressorParameters::specifications)
            {
                setParameter (processor, spec.id, (combination & bit) != 0 ? spec.minimum : spec.maximum);
                bit <<= 1;
            }
            CHECK (bufferIsFinite (renderConstant (processor, 256, -0.01f, 1.0f)));
            CHECK (bufferIsFinite (renderConstant (processor, 256, 1.0e-30f, -1.0e-30f)));
            CHECK ((std::bit_cast<std::uint32_t> (processor.getGainReductionDb()) & 0x7f800000U) != 0x7f800000U);
        }
    }
}

TEST_CASE ("Meter reports a within-block peak and audio is invariant to block partitioning", "[compressor][meter]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE (sampleRate);
        PluginProcessor blockProcessor;
        PluginProcessor sampleProcessor;
        for (auto* processor : { &blockProcessor, &sampleProcessor })
        {
            setParameter (*processor, "attack", 0.2f);
            processor->prepareToPlay (sampleRate, 4096);
        }

        juce::AudioBuffer<float> block (2, 4096);
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
        {
            // The early loud section establishes compression; quiet material
            // leaves a much smaller envelope value by the end of the block.
            block.setSample (0, sample, sample < 512 ? 1.0f : 0.001f);
            block.setSample (1, sample, sample < 512 ? -0.25f : -0.00025f);
        }

        juce::AudioBuffer<float> reference (2, block.getNumSamples());
        juce::AudioBuffer<float> oneSample (2, 1);
        juce::MidiBuffer midi;
        auto expectedPeakDb = 0.0f;
        for (int sample = 0; sample < block.getNumSamples(); ++sample)
        {
            for (int channel = 0; channel < 2; ++channel)
                oneSample.setSample (channel, 0, block.getSample (channel, sample));
            sampleProcessor.processBlock (oneSample, midi);
            expectedPeakDb = juce::jmax (expectedPeakDb, sampleProcessor.getGainReductionDb());
            for (int channel = 0; channel < 2; ++channel)
                reference.setSample (channel, sample, oneSample.getSample (channel, 0));
        }

        blockProcessor.processBlock (block, midi);
        CHECK (blockProcessor.getGainReductionDb() == Catch::Approx (expectedPeakDb).margin (0.000001f));
        CHECK (blockProcessor.getGainReductionDb() > sampleProcessor.getGainReductionDb() + 5.0f);
        for (int channel = 0; channel < 2; ++channel)
        {
            CHECK (std::memcmp (block.getReadPointer (channel), reference.getReadPointer (channel),
                static_cast<std::size_t> (block.getNumSamples()) * sizeof (float)) == 0);
        }

        // Each new block reports its own peak, rather than latching the prior block.
        renderConstant (blockProcessor, 256, 0.001f, -0.00025f);
        CHECK (blockProcessor.getGainReductionDb() < expectedPeakDb - 5.0f);
        blockProcessor.prepareToPlay (sampleRate, 4096);
        CHECK (blockProcessor.getGainReductionDb() == 0.0f);
    }
}
