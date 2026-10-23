#include <PluginProcessor.h>
#include <dsp/VtbOutputTransformer.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <complex>

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
    // Relaxed linear-phase design (docs/OversamplingOptions.md), flat to 20 kHz.
    for (const auto& [rate, expected] : { std::pair { 44100.0, 78 }, std::pair { 48000.0, 48 }, std::pair { 88200.0, 11 },
             std::pair { 96000.0, 11 }, std::pair { 176400.0, 0 }, std::pair { 192000.0, 0 } })
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

TEST_CASE ("VTB1148 stage Out is the exactly delayed input at any Amount", "[vtb][bypass]")
{
    for (const auto amount : { 0.0f, 100.0f })
    {
        VtbOutputTransformer stage;
        stage.prepare (48000.0, amount, true);
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

namespace
{
double harmonicDb (const juce::AudioBuffer<float>& audio, double hz, int order, int start, int count)
{
    std::complex<double> fundamental, harmonic;
    for (int i = start; i < start + count; ++i)
    {
        const auto x = static_cast<double> (audio.getSample (0, i));
        const auto phase = 2.0 * juce::MathConstants<double>::pi * hz * i / 48000.0;
        fundamental += x * std::polar (1.0, -phase);
        harmonic += x * std::polar (1.0, -phase * order);
    }
    return 20.0 * std::log10 (std::abs (harmonic) / std::abs (fundamental) + 1.0e-15);
}
juce::AudioBuffer<float> renderProcessor (PluginProcessor& processor, double hz, double levelDb, int samples)
{
    processor.prepareToPlay (48000.0, 512);
    auto audio = sine (48000.0, hz, levelDb, samples);
    juce::MidiBuffer midi;
    for (int start = 0; start < samples; start += 512)
    {
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, start, juce::jmin (512, samples - start));
        processor.processBlock (view, midi);
    }
    return audio;
}
}

TEST_CASE ("Input transformer is always in circuit and Digital IN drives it", "[vtb][input]")
{
    // Output stage out (both switches), compressor at threshold 0: only the input
    // transformer can add the 3rd harmonic of a 30 Hz tone.
    const auto thirdAt = [] (float inGainDb)
    {
        PluginProcessor processor;
        set (processor, "threshold", 0.0f);
        set (processor, "xfrmOut", 1.0f);
        set (processor, "outputTubeOut", 1.0f);
        set (processor, "inGain", inGainDb);
        // Window of whole 30 Hz periods after one second of settling.
        const auto audio = renderProcessor (processor, 30.0, -12.0, 48000 + 4800);
        return harmonicDb (audio, 30.0, 3, 48000, 4800);
    };
    const auto clean = thirdAt (-12.0f), driven = thirdAt (12.0f);
    CAPTURE (clean, driven);
    // Pushing Digital IN into the input iron raises H3 well above its quiet-level
    // hysteresis floor (about -64 dBc here; the VTB data's distortion falls with
    // level until saturation onset).
    CHECK (driven > -55.0);
    CHECK (driven > clean + 10.0);
}

TEST_CASE ("Input transformer voicing reaches the audio and round-trips through state", "[vtb][input][voicing]")
{
    const auto render = [] (bool voiced)
    {
        PluginProcessor processor;
        set (processor, "threshold", 0.0f);
        set (processor, "xfrmOut", 1.0f); // Output stage out: the input iron alone.
        set (processor, "outputTubeOut", 1.0f);
        set (processor, "inGain", 12.0f);
        if (voiced) processor.getInputTransformerVoicing().setCurrent ({ -12.0f, 24.0f });
        return renderProcessor (processor, 30.0, -12.0, 48000 + 4800);
    };
    const auto plain = render (false), voiced = render (true);
    // Earlier onset and more even harmonics: H2 rises.
    CHECK (harmonicDb (voiced, 30.0, 2, 48000, 4800) > harmonicDb (plain, 30.0, 2, 48000, 4800) + 6.0);
    // The two workbenches are independent and both persist.
    PluginProcessor processor;
    processor.getInputTransformerVoicing().setCurrent ({ 3.0f });
    processor.getTransformerVoicing().setCurrent ({ -3.0f });
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    PluginProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (restored.getInputTransformerVoicing().readAudio().voicing[TransformerVoicing::onset] == Catch::Approx (3.0f));
    CHECK (restored.getTransformerVoicing().readAudio().voicing[TransformerVoicing::onset] == Catch::Approx (-3.0f));
    // Sessions saved before the input transformer existed restore its defaults.
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.removeChild (tree.getChildWithName ("INPUT_TRANSFORMER_VOICING"), nullptr);
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), block);
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (TransformerVoicing::equal (restored.getInputTransformerVoicing().readAudio().voicing, {}));
    const auto exported = juce::JSON::parse (restored.exportTransformerVoicing (true));
    CHECK (exported["transformer"].toString() == "VTB9046 line input");
}

TEST_CASE ("Input XFRM bypass is appended and removes the input iron; Output XFRM keeps its ID", "[vtb][input][parameters]")
{
    PluginProcessor processor;
    auto* input = dynamic_cast<juce::AudioParameterBool*> (parameter (processor, "inputXfrmOut"));
    auto* output = dynamic_cast<juce::AudioParameterBool*> (parameter (processor, "xfrmOut"));
    REQUIRE (input != nullptr);
    REQUIRE (output != nullptr);
    CHECK (input->getName (32) == "Input XFRM");
    CHECK (output->getName (32) == "Output XFRM");
    CHECK (parameter (processor, "inputXfrmOut")->getText (0.0f, 32) == "In");
    CHECK (parameter (processor, "inputXfrmOut")->getText (1.0f, 32) == "Out");
    CHECK_FALSE (input->get());
    // Appended before Output Tube (hint 6) and Tube Amount (hint 7).
    CHECK (processor.getParameters().indexOf (input) == processor.getParameters().size() - 3);
    CHECK (input->getVersionHint() > parameter (processor, "xfrmAmount")->getVersionHint());
    // Driven 30 Hz with the output iron out: only the input iron adds H3.
    const auto thirdWith = [] (bool inputOut)
    {
        PluginProcessor p;
        set (p, "threshold", 0.0f);
        set (p, "xfrmOut", 1.0f);
        set (p, "outputTubeOut", 1.0f);
        set (p, "inGain", 12.0f);
        set (p, "input", -12.0f); // Analog Input down: keep the compressor out of its knee.
        set (p, "inputXfrmOut", inputOut ? 1.0f : 0.0f);
        return harmonicDb (renderProcessor (p, 30.0, -12.0, 48000 + 4800), 30.0, 3, 48000, 4800);
    };
    const auto in = thirdWith (false), out = thirdWith (true);
    CAPTURE (in, out);
    CHECK (out < in - 40.0);
    CHECK (out < -100.0);
    // Its state round-trips.
    set (processor, "inputXfrmOut", 1.0f);
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    PluginProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (dynamic_cast<juce::AudioParameterBool*> (parameter (restored, "inputXfrmOut"))->get());
}

TEST_CASE ("Toggling Input XFRM mid-stream ramps without a step", "[vtb][input][smoothing]")
{
    PluginProcessor processor;
    set (processor, "threshold", 0.0f);
    processor.prepareToPlay (48000.0, 256);
    const auto dry = sine (48000.0, 110.0, -6.0, 256 * 64);
    auto audio = dry;
    juce::MidiBuffer midi;
    for (int block = 0; block < 64; ++block)
    {
        if (block == 20) set (processor, "inputXfrmOut", 1.0f);
        if (block == 40) set (processor, "inputXfrmOut", 0.0f);
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, block * 256, 256);
        processor.processBlock (view, midi);
    }
    REQUIRE (finite (audio));
    float largestStep = 0.0f, inputStep = 0.0f;
    for (int i = 1024; i < audio.getNumSamples(); ++i)
    {
        largestStep = std::max (largestStep, std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)));
        inputStep = std::max (inputStep, std::abs (dry.getSample (0, i) - dry.getSample (0, i - 1)));
    }
    CHECK (largestStep < 1.5f * inputStep);
}
