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

TEST_CASE ("Sessions saved with the retired model choice restore without it", "[vtb][state]")
{
    PluginProcessor processor;
    const auto count = processor.getParameters().size();
    set (processor, "xfrmAmount", 40.0f);
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    // 2026-10-22 builds saved an xfrmModel choice; it no longer exists.
    tree.appendChild (juce::ValueTree ("PARAM", { { "id", "xfrmModel" }, { "value", 0.0f } }), nullptr);
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), block);
    PluginProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (restored.getParameters().size() == count);
    CHECK (parameter (restored, "xfrmModel") == nullptr);
    CHECK (parameter (restored, "xfrmAmount")->convertFrom0to1 (parameter (restored, "xfrmAmount")->getValue()) == Catch::Approx (40.0f));
}

TEST_CASE ("Default voicing reproduces the chosen VTB1148 calibration exactly", "[vtb][voicing]")
{
    const auto p = VtbOutputTransformer::parametersFor ({});
    const auto c = ModuleSpike::vtb1148OutputC();
    for (const auto& [value, expected] : { std::pair { p.saturation, c.saturation }, std::pair { p.knee, c.knee }, std::pair { p.width, c.width },
             std::pair { p.memory, c.memory }, std::pair { p.asymmetry, c.asymmetry }, std::pair { p.driveDb, c.driveDb } })
        CHECK (std::bit_cast<std::uint64_t> (value) == std::bit_cast<std::uint64_t> (expected));
    // Controls move the intended terms only.
    const auto later = VtbOutputTransformer::parametersFor ({ 6.0f });
    CHECK (later.saturation / c.saturation == Catch::Approx (std::pow (10.0, 6.0 / 20.0)));
    CHECK (later.knee / later.saturation == Catch::Approx (c.knee / c.saturation));
    CHECK (std::bit_cast<std::uint64_t> (later.asymmetry) == std::bit_cast<std::uint64_t> (c.asymmetry));
    const auto even = VtbOutputTransformer::parametersFor ({ 0.0f, -24.0f });
    CHECK (even.asymmetry == Catch::Approx (c.asymmetry * std::pow (10.0, -24.0 / 20.0)));
    const auto grain = VtbOutputTransformer::parametersFor ({ 0.0f, 0.0f, 12.0f });
    CHECK (grain.width == Catch::Approx (c.width * std::pow (10.0, 12.0 / 20.0)));
    const auto memory = VtbOutputTransformer::parametersFor ({ 0.0f, 0.0f, 0.0f, 114.0f });
    CHECK (memory.memory == Catch::Approx (1.0).epsilon (0.0));
    for (const auto& d : TransformerVoicing::descriptors)
        for (double value : { d.minimum, d.maximum })
        {
            TransformerVoicing::Values voice;
            voice[static_cast<std::size_t> (&d - TransformerVoicing::descriptors.data())] = static_cast<float> (value);
            const auto q = VtbOutputTransformer::parametersFor (voice);
            CAPTURE (d.id, value);
            ModuleSpike::TransformerModule module;
            module.prepare ({ 192000.0, 256, 1 });
            module.configure (ModuleSpike::Variant::play3Outer, ModuleSpike::Topology::shunt, q);
            // Every extreme stays inside the module's descriptor ranges (no silent clamping).
            const auto& m = ModuleSpike::transformerDescriptors;
            CHECK (q.saturation >= m[0].minimum); CHECK (q.knee >= m[1].minimum); CHECK (q.knee <= m[1].maximum);
            CHECK (q.width >= m[5].minimum); CHECK (q.asymmetry <= m[6].maximum);
        }
}

TEST_CASE ("VTB1148 Amount maps -24 to +18 dB over its calibrated nominal", "[vtb][parameters]")
{
    CHECK (VtbOutputTransformer::driveDbFor (0.0f) == Catch::Approx (-24.0));
    CHECK (VtbOutputTransformer::driveDbFor (100.0f) == Catch::Approx (18.0));
    CHECK (VtbOutputTransformer::driveDbFor (400.0f / 7.0f) == Catch::Approx (0.0).margin (1.0e-5));
}

TEST_CASE ("Reported latency is the VTB1148 stage's own FIR delay at every rate", "[vtb][latency]")
{
    for (const auto& [rate, expected] : { std::pair { 44100.0, 124 }, std::pair { 48000.0, 124 }, std::pair { 88200.0, 99 },
             std::pair { 96000.0, 99 }, std::pair { 176400.0, 0 }, std::pair { 192000.0, 0 } })
    {
        CAPTURE (rate);
        PluginProcessor processor;
        set (processor, "xfrmAmount", 100.0f);
        processor.prepareToPlay (rate, 256);
        CHECK (processor.getLatencySamples() == expected);
        VtbOutputTransformer stage;
        stage.prepare (rate, 100.0f, false);
        CHECK (stage.getLatencySamples() == expected);
        CHECK (stage.getOversamplingFactor() == ModuleSpike::vtb1148OutputFactor (rate));
    }
}

TEST_CASE ("VTB1148 stage at zero Amount or Out is the exactly delayed input", "[vtb][bypass]")
{
    for (bool out : { false, true })
    {
        VtbOutputTransformer stage;
        stage.prepare (48000.0, out ? 100.0f : 0.0f, out);
        const auto latency = stage.getLatencySamples();
        const auto dry = sine (48000.0, 61.0, -6.0, 4096);
        auto audio = dry;
        stage.process (audio, 2);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = latency; i < 4096; ++i)
                REQUIRE (std::bit_cast<std::uint32_t> (audio.getSample (channel, i)) == std::bit_cast<std::uint32_t> (dry.getSample (channel, i - latency)));
    }
}

TEST_CASE ("VTB1148 stage processes loud bass finitely with unity small-signal gain", "[vtb][audio]")
{
    for (float amount : { 400.0f / 7.0f, 100.0f })
    {
        CAPTURE (amount);
        VtbOutputTransformer stage;
        stage.prepare (48000.0, amount, false);
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
        for (int i = 24000; i < 48000; ++i) { input += std::pow (dry.getSample (0, i - stage.getLatencySamples()), 2); output += std::pow (audio.getSample (0, i), 2); }
        CHECK (std::abs (10 * std::log10 (output / input)) < 1.0);
    }
}

TEST_CASE ("VTB1148 voicing changes crossfade without discontinuity and reach the audio", "[vtb][voicing][audio]")
{
    VtbOutputTransformer stage, reference;
    stage.prepare (48000.0, 100.0f, false);
    reference.prepare (48000.0, 100.0f, false);
    const auto dry = sine (48000.0, 40.0, -3.0, 256 * 96);
    auto audio = dry, unchanged = dry;
    float largestStep = 0.0f, inputStep = 0.0f;
    for (int block = 0; block < 96; ++block)
    {
        if (block == 20) stage.setVoicing ({ -12.0f, 24.0f, 12.0f, 50.0f });
        if (block == 60) stage.setVoicing ({});
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, block * 256, 256);
        juce::AudioBuffer<float> control (unchanged.getArrayOfWritePointers(), 2, block * 256, 256);
        stage.process (view, 2);
        reference.process (control, 2);
    }
    REQUIRE (finite (audio));
    CHECK (stage.getDiagnostics (0).failures == 0);
    CHECK (TransformerVoicing::equal (stage.getAppliedVoicing(), {}));
    for (int i = 1024; i < audio.getNumSamples(); ++i)
    {
        largestStep = std::max (largestStep, std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)));
        inputStep = std::max (inputStep, std::abs (dry.getSample (0, i) - dry.getSample (0, i - 1)));
    }
    CHECK (largestStep < 2.0f * inputStep);
    // The extreme voicing audibly changed the output while it was applied.
    double difference = 0;
    for (int i = 256 * 40; i < 256 * 56; ++i) difference = std::max (difference, static_cast<double> (std::abs (audio.getSample (0, i) - unchanged.getSample (0, i))));
    CHECK (difference > 1.0e-3);
}
