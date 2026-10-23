#include <PluginProcessor.h>
#include <dsp/VtbOutputTransformer.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>

namespace
{
juce::RangedAudioParameter* parameter (PluginProcessor& processor, const char* id)
{
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID == id)
            return ranged;
    return nullptr;
}
void set (PluginProcessor& processor, const char* id, float value)
{
    auto* p = parameter (processor, id);
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}
int model (PluginProcessor& processor)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (parameter (processor, BaParameters::xfrmModelId));
    REQUIRE (p != nullptr);
    return p->getIndex();
}
bool finite (const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if ((std::bit_cast<std::uint32_t> (audio.getSample (channel, i)) & 0x7f800000U) == 0x7f800000U)
                return false;
    return true;
}
juce::AudioBuffer<float> sine (double sampleRate, double frequency, double levelDb, int samples)
{
    juce::AudioBuffer<float> audio (2, samples);
    const auto amplitude = juce::Decibels::decibelsToGain (levelDb);
    for (int i = 0; i < samples; ++i)
        for (int channel = 0; channel < 2; ++channel)
            audio.setSample (channel, i, static_cast<float> ((channel == 0 ? 1 : -1) * amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate)));
    return audio;
}
}

TEST_CASE ("Transformer model choice is appended, defaults to VTB1148 and restores old sessions as Magnetic", "[vtb][parameters][state]")
{
    PluginProcessor processor;
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter (processor, BaParameters::xfrmModelId));
    REQUIRE (choice != nullptr);
    CHECK (choice->choices == juce::StringArray { "Magnetic", "VTB1148" });
    CHECK (choice->getIndex() == BaParameters::xfrmModelVtb1148);
    CHECK (processor.getParameters().indexOf (choice) == processor.getParameters().size() - 1);
    CHECK (choice->getVersionHint() > parameter (processor, "xfrmAmount")->getVersionHint());

    // A saved choice round-trips.
    set (processor, BaParameters::xfrmModelId, static_cast<float> (BaParameters::xfrmModelMagnetic));
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    PluginProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (model (restored) == BaParameters::xfrmModelMagnetic);

    // A session saved before the choice existed has no PARAM for it: Magnetic.
    PluginProcessor current;
    current.getStateInformation (block);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.removeChild (tree.getChildWithProperty ("id", BaParameters::xfrmModelId), nullptr);
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), block);
    PluginProcessor legacy;
    REQUIRE (model (legacy) == BaParameters::xfrmModelVtb1148);
    legacy.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (model (legacy) == BaParameters::xfrmModelMagnetic);
}

TEST_CASE ("VTB1148 Amount maps -24 to +18 dB over its calibrated nominal", "[vtb][parameters]")
{
    CHECK (VtbOutputTransformer::driveDbFor (0.0f) == Catch::Approx (-24.0));
    CHECK (VtbOutputTransformer::driveDbFor (100.0f) == Catch::Approx (18.0));
    CHECK (VtbOutputTransformer::driveDbFor (400.0f / 7.0f) == Catch::Approx (0.0).margin (1.0e-5));
}

TEST_CASE ("Reported latency is the same for both transformer models at every rate", "[vtb][latency]")
{
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        CAPTURE (rate);
        std::array<int, 2> latencies {};
        for (int index : { BaParameters::xfrmModelMagnetic, BaParameters::xfrmModelVtb1148 })
        {
            PluginProcessor processor;
            set (processor, BaParameters::xfrmModelId, static_cast<float> (index));
            set (processor, "xfrmAmount", 100.0f);
            processor.prepareToPlay (rate, 256);
            latencies[static_cast<std::size_t> (index)] = processor.getLatencySamples();
        }
        CHECK (latencies[0] == latencies[1]);
        VtbOutputTransformer stage;
        stage.prepare (rate, 100.0f, false, latencies[0]);
        CHECK (stage.getLatencySamples() == latencies[0]);
        CHECK (stage.getOversamplingFactor() == ModuleSpike::vtb1148OutputFactor (rate));
    }
}

TEST_CASE ("VTB1148 stage at zero Amount or Out is the exactly delayed input", "[vtb][bypass]")
{
    for (bool out : { false, true })
    {
        VtbOutputTransformer stage;
        stage.prepare (48000.0, out ? 100.0f : 0.0f, out, 144);
        const auto dry = sine (48000.0, 61.0, -6.0, 4096);
        auto audio = dry;
        stage.process (audio, 2);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 144; i < 4096; ++i)
                REQUIRE (std::bit_cast<std::uint32_t> (audio.getSample (channel, i)) == std::bit_cast<std::uint32_t> (dry.getSample (channel, i - 144)));
    }
}

TEST_CASE ("VTB1148 stage processes loud bass finitely with unity small-signal gain", "[vtb][audio]")
{
    for (float amount : { 400.0f / 7.0f, 100.0f })
    {
        CAPTURE (amount);
        VtbOutputTransformer stage;
        stage.prepare (48000.0, amount, false, 144);
        const auto dry = sine (48000.0, 50.0, -3.0, 48000);
        auto audio = dry;
        for (int start = 0; start < audio.getNumSamples(); start += 512)
        {
            juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), 2, start, juce::jmin (512, audio.getNumSamples() - start));
            stage.process (block, 2);
        }
        CHECK (finite (audio));
        CHECK (stage.getDiagnostics (0).failures == 0);
        CHECK (stage.getDiagnostics (1).failures == 0);
        // Output level stays within 1 dB of the delayed input over the last half second.
        double input = 0, output = 0;
        for (int i = 24000; i < 48000; ++i) { input += std::pow (dry.getSample (0, i - 144), 2); output += std::pow (audio.getSample (0, i), 2); }
        CHECK (std::abs (10 * std::log10 (output / input)) < 1.0);
    }
}

TEST_CASE ("Switching transformer model mid-stream crossfades without discontinuity", "[vtb][audio]")
{
    PluginProcessor processor;
    set (processor, "xfrmAmount", 100.0f);
    set (processor, BaParameters::xfrmModelId, static_cast<float> (BaParameters::xfrmModelMagnetic));
    processor.prepareToPlay (48000.0, 256);
    const auto dry = sine (48000.0, 110.0, -6.0, 256 * 64);
    auto audio = dry;
    juce::MidiBuffer midi;
    float largestStep = 0.0f, inputStep = 0.0f;
    for (int block = 0; block < 64; ++block)
    {
        if (block == 20) set (processor, BaParameters::xfrmModelId, static_cast<float> (BaParameters::xfrmModelVtb1148));
        if (block == 40) set (processor, BaParameters::xfrmModelId, static_cast<float> (BaParameters::xfrmModelMagnetic));
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, block * 256, 256);
        processor.processBlock (view, midi);
    }
    REQUIRE (finite (audio));
    for (int i = 1024; i < audio.getNumSamples(); ++i)
    {
        largestStep = std::max (largestStep, std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)));
        inputStep = std::max (inputStep, std::abs (dry.getSample (0, i) - dry.getSample (0, i - 1)));
    }
    // Both engines are near-transparent at this level; a hard switch or a cold
    // engine would show as a step far above the sine's own slope.
    CHECK (largestStep < 1.5f * inputStep);
}
