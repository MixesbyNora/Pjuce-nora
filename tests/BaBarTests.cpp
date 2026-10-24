#include <PluginProcessor.h>
#include <GainLink.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <cstdint>

namespace
{
juce::RangedAudioParameter* parameter (PluginProcessor& processor, const char* id)
{
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->paramID == id)
                return ranged;
    return nullptr;
}

void set (PluginProcessor& processor, const char* id, float value)
{
    auto* p = parameter (processor, id);
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float get (PluginProcessor& processor, const char* id)
{
    auto* p = parameter (processor, id);
    REQUIRE (p != nullptr);
    return p->convertFrom0to1 (p->getValue());
}

CompressorParameters::Values neutral()
{
    CompressorParameters::Values values;
    values.thresholdDb = 0.0f;
    return values;
}

juce::AudioBuffer<float> signal (int channels, int samples, float amplitude = 0.125f, bool opposite = true)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    for (int channel = 0; channel < channels; ++channel)
        juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), channel == 1 && opposite ? -amplitude : amplitude, samples);
    return buffer;
}

// Steady tone; the analog chain (input transformer) blocks DC, so wet-path
// checks use AC. Right is inverted unless "same".
juce::AudioBuffer<float> tone (int channels, int samples, float amplitude = 0.125f, double hz = 1000.0, bool opposite = true)
{
    juce::AudioBuffer<float> buffer (channels, samples);
    for (int i = 0; i < samples; ++i)
    {
        const auto value = static_cast<float> (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / 48000.0));
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample (channel, i, channel == 1 && opposite ? -value : value);
    }
    return buffer;
}

double residualDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int start)
{
    double error = 0.0, power = 0.0;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int i = start; i < a.getNumSamples(); ++i)
        {
            error += std::pow (static_cast<double> (a.getSample (channel, i)) - b.getSample (channel, i), 2);
            power += std::pow (static_cast<double> (b.getSample (channel, i)), 2);
        }
    return 10.0 * std::log10 (error / std::max (power, 1.0e-30) + 1.0e-30);
}

juce::AudioBuffer<float> delayed (juce::AudioBuffer<float> input, int samples = 48)
{
    for (int channel = 0; channel < input.getNumChannels(); ++channel)
    {
        auto* data = input.getWritePointer (channel);
        std::move_backward (data, data + input.getNumSamples() - samples, data + input.getNumSamples());
        juce::FloatVectorOperations::clear (data, samples);
    }
    return input;
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() != b.getNumChannels())
        return false;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        if (std::memcmp (a.getReadPointer (channel), b.getReadPointer (channel),
            static_cast<std::size_t> (a.getNumSamples()) * sizeof (float)) != 0)
            return false;
    return true;
}

bool isFinite (const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            if ((std::bit_cast<std::uint32_t> (audio.getSample (channel, sample)) & 0x7f800000U) == 0x7f800000U)
                return false;
    return true;
}

juce::AudioBuffer<float> render (BaParameters::Values values, int channels = 2,
    const CompressorParameters::Values& compressor = neutral(), juce::AudioBuffer<float> audio = {})
{
    BaSignalPath path;
    path.prepare (48000.0, 64, compressor, values);
    if (audio.getNumSamples() == 0)
        audio = signal (channels, 4096);
    path.process (audio, channels, compressor, values);
    return audio;
}

void dispatchLink()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
}
}

TEST_CASE ("BA parameter types ranges defaults skew text and host bypass are exposed", "[ba][parameters]")
{
    PluginProcessor processor;
    REQUIRE (processor.getParameters().size() == 22);
    CHECK (processor.getBypassParameter() == parameter (processor, "bypass"));
    CHECK (dynamic_cast<juce::AudioParameterBool*> (parameter (processor, "bypass")) != nullptr);
    CHECK (dynamic_cast<juce::AudioParameterBool*> (parameter (processor, "ioLink")) != nullptr);
    CHECK (get (processor, "bypass") == 0.0f);
    CHECK (get (processor, "ioLink") == 0.0f);
    for (const auto& spec : { BaParameters::inGain, BaParameters::outGain, BaParameters::lfm, BaParameters::mix })
    {
        auto* p = dynamic_cast<juce::AudioParameterFloat*> (parameter (processor, spec.id));
        REQUIRE (p != nullptr);
        CHECK (p->range.start == Catch::Approx (spec.minimum));
        CHECK (p->range.end == Catch::Approx (spec.maximum));
        CHECK (p->range.interval == Catch::Approx (0.1f));
        CHECK (get (processor, spec.id) == Catch::Approx (spec.initial).margin (0.00001f));
    }
    auto* lfm = parameter (processor, "lfm");
    CHECK (lfm->convertFrom0to1 (0.5f) == Catch::Approx (70.0f).margin (0.1f));
    CHECK (lfm->getText (lfm->convertTo0to1 (19.9f), 64) == "Off");
    auto* monitor = dynamic_cast<juce::AudioParameterChoice*> (parameter (processor, "monitor"));
    REQUIRE (monitor != nullptr);
    const juce::StringArray choices { "LR", "M", "S", "D" };
    CHECK (monitor->choices == choices);
    CHECK (monitor->getIndex() == 0);
}

TEST_CASE ("BA defaults leave the analog chain near unity and the compressor in control", "[ba][defaults]")
{
    // Digital IN -> input transformer -> compressor -> output stage -> Digital OUT.
    // The output stage is in at defaults (Amounts are drive only); with its two
    // switches Out and a mid-band tone, only the input transformer's small
    // coloration and the FIR delay separate output from input.
    BaParameters::Values outputOut;
    outputOut.transformerOut = outputOut.outputTubeOut = true;
    for (int channels : { 1, 2 })
    {
        for (bool compressionActive : { false, true })
        {
            CAPTURE (channels, compressionActive);
            auto settings = neutral();
            if (compressionActive)
                settings.thresholdDb = -30.0f;
            BaSignalPath path;
            path.prepare (48000.0, 17, settings, outputOut);
            CHECK (path.getLatencySamples() == 48);
            const auto input = tone (channels, 9600);
            auto actual = input;
            const auto reduction = path.process (actual, channels, settings, outputOut);
            if (compressionActive)
            {
                // A peak detector on a -18 dBFS tone settles near 4.9 dB here (9 dB for DC).
                CHECK (reduction > 4.0f);
                CHECK (actual.getRMSLevel (0, 4800, 4800) < 0.7f * input.getRMSLevel (0, 4800, 4800));
            }
            else
            {
                CHECK (reduction == 0.0f);
                CHECK (residualDb (actual, delayed (input, path.getLatencySamples()), 4800) < -40.0);
            }
        }
    }
}

TEST_CASE ("BA input and output gains and linear mix follow the requested signal flow", "[ba][gain][mix]")
{
    const auto renderTone = [] (const char* id, float value)
    {
        PluginProcessor processor;
        set (processor, "threshold", 0.0f);
        if (id != nullptr) set (processor, id, value);
        processor.prepareToPlay (48000.0, 64);
        auto audio = tone (2, 9600, 0.03125f);
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        return audio;
    };
    const auto reference = renderTone (nullptr, 0.0f);
    // Digital IN drives the input transformer (near-linear at this level);
    // Digital OUT is a pure gain after it.
    const auto in = renderTone ("inGain", 6.0f);
    CHECK (juce::Decibels::gainToDecibels (in.getRMSLevel (0, 4800, 4800) / reference.getRMSLevel (0, 4800, 4800)) == Catch::Approx (6.0f).margin (0.05f));
    const auto out = renderTone ("outGain", 6.0f);
    for (int i = 4800; i < 9600; ++i)
        CHECK (out.getSample (0, i) == Catch::Approx (reference.getSample (0, i) * juce::Decibels::decibelsToGain (6.0f)).margin (1.0e-7f));
    BaParameters::Values values;
    values.outGainDb = 6.0f;
    const auto wet = render (values);
    values.mixPercent = 0.0f;
    const auto dry = render (values);
    CHECK (identical (dry, delayed (signal (2, 4096))));
    values.mixPercent = 50.0f;
    const auto half = render (values);
    for (int channel = 0; channel < 2; ++channel)
        CHECK (half.getSample (channel, 4000) == Catch::Approx (
            (dry.getSample (channel, 4000) + wet.getSample (channel, 4000)) * 0.5f).margin (0.0000001f));
}

TEST_CASE ("BA bypass crossfades to exact dry while the compressor keeps running", "[ba][bypass]")
{
    BaParameters::Values initiallyBypassed;
    initiallyBypassed.bypassed = true;
    initiallyBypassed.inGainDb = 6.0f;
    initiallyBypassed.outGainDb = -3.0f;
    initiallyBypassed.lfmHz = 150.0f;
    initiallyBypassed.mixPercent = 50.0f;
    CHECK (identical (render (initiallyBypassed), delayed (signal (2, 4096))));
    PluginProcessor processor;
    set (processor, "threshold", -30.0f);
    processor.prepareToPlay (48000.0, 64);
    juce::MidiBuffer midi;
    const auto input = tone (2, 4096);
    const auto dry = delayed (input, processor.getLatencySamples());
    auto audio = input;
    processor.processBlock (audio, midi);
    REQUIRE (processor.getGainReductionDb() > 4.0f);
    set (processor, "bypass", 1.0f);
    audio = input;
    processor.processBlock (audio, midi);
    // The compressor keeps running; the meter reports zero while bypassed.
    CHECK (processor.getGainReductionDb() == 0.0f);
    for (int sample = 480; sample < 4096; ++sample)
        CHECK (audio.getSample (0, sample) == Catch::Approx (dry.getSample (0, sample)).epsilon (0.0));
    set (processor, "bypass", 0.0f);
    audio = input;
    processor.processBlock (audio, midi);
    CHECK (processor.getGainReductionDb() > 4.0f);
    CHECK (audio.getRMSLevel (0, 2048, 2048) < 0.7f * dry.getRMSLevel (0, 2048, 2048));
    audio = input;
    processor.processBlockBypassed (audio, midi);
    CHECK (processor.getGainReductionDb() == 0.0f);
    for (int sample = 1000; sample < 4096; ++sample)
        CHECK (audio.getSample (0, sample) == Catch::Approx (dry.getSample (0, sample)).epsilon (0.0));
}

TEST_CASE ("Gain link preserves offsets in both directions and clamps without linking when disabled", "[ba][link]")
{
    CHECK (GainLink::compensate (2.0f, 3.0f, true, -24.0f, 24.0f) == Catch::Approx (-1.0f));
    CHECK (GainLink::compensate (5.0f, 3.0f, true, -24.0f, 24.0f) == Catch::Approx (2.0f));
    CHECK (GainLink::compensate (2.0f, 3.0f, false, -24.0f, 24.0f) == Catch::Approx (2.0f));
    CHECK (GainLink::compensate (-23.0f, 3.0f, true, -24.0f, 24.0f) == Catch::Approx (-24.0f));
    CHECK (GainLink::compensate (23.0f, -3.0f, true, -24.0f, 24.0f) == Catch::Approx (24.0f));
}

TEST_CASE ("Gain link moves its partner on the message thread without recursive compensation", "[ba][link]")
{
    PluginProcessor processor;
    set (processor, "outGain", 2.0f);
    set (processor, "ioLink", 1.0f);
    set (processor, "inGain", 3.0f);
    dispatchLink();
    CHECK (get (processor, "outGain") == Catch::Approx (-1.0f).margin (0.001f));
    set (processor, "outGain", 2.0f);
    dispatchLink();
    CHECK (get (processor, "inGain") == Catch::Approx (0.0f).margin (0.001f));
    dispatchLink();
    CHECK (get (processor, "outGain") == Catch::Approx (2.0f).margin (0.001f));
    set (processor, "ioLink", 0.0f);
    set (processor, "inGain", 6.0f);
    dispatchLink();
    CHECK (get (processor, "outGain") == Catch::Approx (2.0f).margin (0.001f));
    set (processor, "ioLink", 1.0f);
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    set (processor, "inGain", 12.0f);
    processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    dispatchLink();
    CHECK (get (processor, "inGain") == Catch::Approx (6.0f).margin (0.001f));
    CHECK (get (processor, "outGain") == Catch::Approx (2.0f).margin (0.001f));
}

TEST_CASE ("LFM attenuates low side content and preserves mid and high side content", "[ba][lfm]")
{
    for (const auto& [frequency, midOnly, expectedDb, tolerance] : std::array {
             std::tuple { 50.0, false, -19.0, 1.5 },
             std::tuple { 50.0, true, 0.0, 0.1 },
             std::tuple { 1000.0, false, 0.0, 0.1 } })
    {
        LowFrequencyMono lfm;
        lfm.prepare (48000.0, 150.0f);
        auto inputPower = 0.0;
        auto outputPower = 0.0;
        for (int sample = 0; sample < 48000; ++sample)
        {
            const auto value = static_cast<float> (0.125 * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * sample / 48000.0));
            auto left = value;
            auto right = midOnly ? value : -value;
            lfm.process (left, right);
            if (sample >= 24000)
            {
                inputPower += static_cast<double> (value) * value;
                outputPower += static_cast<double> (left) * left;
            }
        }
        CHECK (10.0 * std::log10 (outputPower / inputPower) == Catch::Approx (expectedDb).margin (tolerance));
    }
    LowFrequencyMono off;
    off.prepare (48000.0, 19.9f);
    for (int sample = 0; sample < 4096; ++sample)
    {
        const auto value = static_cast<float> (sample % 97 - 48) / 512.0f;
        auto left = value;
        auto right = -value;
        off.process (left, right);
        CHECK (std::bit_cast<std::uint32_t> (left) == std::bit_cast<std::uint32_t> (value));
        CHECK (std::bit_cast<std::uint32_t> (right) == std::bit_cast<std::uint32_t> (-value));
    }
    // Mono skips LF Mono entirely.
    BaParameters::Values values;
    values.lfmHz = 150.0f;
    const auto withLfm = render (values, 1, neutral(), tone (1, 4096, 0.125f, 50.0));
    values.lfmHz = 0.0f;
    CHECK (identical (withLfm, render (values, 1, neutral(), tone (1, 4096, 0.125f, 50.0))));
}

TEST_CASE ("Monitor mid and side are post-bypass", "[ba][monitor]")
{
    for (int mode : { 1, 2 })
    {
        for (bool opposite : { false, true })
        {
            CAPTURE (mode, opposite);
            const auto input = tone (2, 4096, 0.125f, 1000.0, opposite);
            const auto stereo = render ({}, 2, neutral(), input);
            BaParameters::Values values;
            values.monitor = mode;
            const auto monitored = render (values, 2, neutral(), input);
            for (int i = 2000; i < 4096; ++i)
            {
                const auto mid = 0.5f * (stereo.getSample (0, i) + stereo.getSample (1, i));
                const auto side = 0.5f * (stereo.getSample (0, i) - stereo.getSample (1, i));
                const auto expected = mode == 1 ? mid : side;
                CHECK (monitored.getSample (0, i) == Catch::Approx (expected).margin (1.0e-6f));
                CHECK (monitored.getSample (1, i) == Catch::Approx (expected).margin (1.0e-6f));
            }
        }
    }
    const auto mono = tone (1, 4096);
    BaParameters::Values values;
    values.monitor = 1;
    CHECK (identical (render (values, 1, neutral(), mono), render ({}, 1, neutral(), mono)));
    values.monitor = 2;
    const auto monoSide = render (values, 1, neutral(), mono);
    CHECK (monoSide.getMagnitude (0, monoSide.getNumSamples()) < 0.00001f);
}

TEST_CASE ("Processor Delta respects Mix and stays small for a neutral chain", "[ba][monitor][delta]")
{
    auto dry = signal (2, 8192);
    // Different channel content exercises stereo linking and the LFM wet path.
    for (int i = 0; i < dry.getNumSamples(); ++i)
    {
        const auto phase = 2.0f * juce::MathConstants<float>::pi * static_cast<float> (i) / 48000.0f;
        dry.setSample (0, i, 0.125f + 0.2f * std::sin (phase * 110.0f));
        dry.setSample (1, i, -0.0625f + 0.15f * std::sin (phase * 731.0f));
    }
    // The same tones without DC offsets: the input transformer blocks DC, which a
    // neutral chain's Delta would otherwise show as slow settling.
    auto acDry = dry;
    for (int i = 0; i < acDry.getNumSamples(); ++i)
    {
        acDry.setSample (0, i, acDry.getSample (0, i) - 0.125f);
        acDry.setSample (1, i, acDry.getSample (1, i) + 0.0625f);
    }
    const auto renderMonitor = [&dry, &acDry] (int mode, float mixPercent, bool bypassed, bool neutralChain = false)
    {
        PluginProcessor processor;
        // These host-notifying parameters are owned by the processor's APVTS;
        // exercise its real parameter reads and processBlock, not BaSignalPath directly.
        set (processor, "monitor", static_cast<float> (mode));
        set (processor, "mix", mixPercent);
        set (processor, "bypass", bypassed ? 1.0f : 0.0f);
        set (processor, "threshold", neutralChain ? 0.0f : -30.0f);
        if (neutralChain)
        {
            set (processor, "xfrmOut", 1.0f); // Output stage out: only the input iron remains.
            set (processor, "outputTubeOut", 1.0f);
        }
        if (! neutralChain)
        {
            set (processor, "inGain", 6.0f);
            set (processor, "input", 3.0f);
            set (processor, "makeup", 3.0f);
            set (processor, "outGain", -3.0f);
            set (processor, "lfm", 88.0f);
            set (processor, "fbff", 35.0f);
            set (processor, "scFilter", 3.0f);
        }
        processor.prepareToPlay (48000.0, 256);
        auto audio = neutralChain ? acDry : dry;
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        return audio;
    };

    const auto wet = renderMonitor (0, 100.0f, false);
    const auto fullDelta = renderMonitor (3, 100.0f, false);
    const auto halfDelta = renderMonitor (3, 50.0f, false);
    const auto dryDelta = renderMonitor (3, 0.0f, false);
    for (int channel = 0; channel < 2; ++channel)
    {
        CAPTURE (channel);
        CHECK (juce::Decibels::gainToDecibels (dryDelta.getMagnitude (channel, 0, dryDelta.getNumSamples()), -160.0f) < -100.0f);
        const auto fullRms = fullDelta.getRMSLevel (channel, 0, fullDelta.getNumSamples());
        REQUIRE (fullRms > 0.001f);
        const auto halfRms = halfDelta.getRMSLevel (channel, 0, halfDelta.getNumSamples());
        CHECK (juce::Decibels::gainToDecibels (halfRms / fullRms)
            == Catch::Approx (juce::Decibels::gainToDecibels (0.5f)).margin (0.05f));
        auto fullError = 0.0f;
        auto halfError = 0.0f;
        for (int i = 0; i < dry.getNumSamples(); ++i)
        {
            fullError = juce::jmax (fullError, std::abs (fullDelta.getSample (channel, i)
                - (wet.getSample (channel, i) - (i < 48 ? 0.0f : dry.getSample (channel, i - 48)))));
            halfError = juce::jmax (halfError, std::abs (halfDelta.getSample (channel, i)
                - 0.5f * fullDelta.getSample (channel, i)));
        }
        CHECK (fullError < 0.0000001f);
        CHECK (halfError < 0.0000001f);
    }
    for (float mixPercent : { 0.0f, 50.0f, 100.0f })
    {
        CAPTURE (mixPercent);
        const auto bypassed = renderMonitor (3, mixPercent, true);
        const auto neutralDelta = renderMonitor (3, mixPercent, false, true);
        CHECK (juce::Decibels::gainToDecibels (bypassed.getMagnitude (0, bypassed.getNumSamples()), -160.0f) < -100.0f);
        // A neutral chain still includes the always-in input transformer, so Delta
        // shows its small coloration (no longer a null) but stays well down.
        CHECK (juce::Decibels::gainToDecibels (neutralDelta.getMagnitude (0, 2048, neutralDelta.getNumSamples() - 2048), -160.0f) < -35.0f);
    }
}

TEST_CASE ("Processor Delta retains monitor bypass and Mix crossfades", "[ba][monitor][delta][smoothing]")
{
    // A twin processor with LR monitor, no bypass and full Mix supplies the wet
    // signal sample by sample; the analog chain is identical in both. The output
    // stage is out so the DC test signal stays near the dry level.
    PluginProcessor processor, reference;
    for (auto* p : { &processor, &reference })
    {
        set (*p, "xfrmOut", 1.0f);
        set (*p, "outputTubeOut", 1.0f);
        set (*p, "threshold", 0.0f);
        set (*p, "outGain", 6.0f);
        p->prepareToPlay (48000.0, 256);
    }
    juce::MidiBuffer midi;
    const auto process = [&processor, &reference, &midi]
    {
        auto audio = signal (2, 1024), wet = signal (2, 1024);
        processor.processBlock (audio, midi);
        reference.processBlock (wet, midi);
        return std::pair { audio, wet };
    };
    process();
    set (processor, "monitor", 3.0f);
    const auto [monitorRamp, wet1] = process();
    // Dry is the constant 0.125 here (one block in, past the FIR delay).
    CHECK (monitorRamp.getSample (0, 0) == Catch::Approx (wet1.getSample (0, 0) - 0.125f / 480.0f).margin (0.000001f));
    CHECK (monitorRamp.getSample (0, 479) == Catch::Approx (wet1.getSample (0, 479) - 0.125f).margin (0.000001f));
    set (processor, "bypass", 1.0f);
    const auto [bypassRamp, wet2] = process();
    CHECK (bypassRamp.getSample (0, 0) == Catch::Approx ((wet2.getSample (0, 0) - 0.125f) * (1.0f - 1.0f / 480.0f)).margin (0.000001f));
    CHECK (bypassRamp.getSample (0, 239) == Catch::Approx ((wet2.getSample (0, 239) - 0.125f) * 0.5f).margin (0.000001f));
    CHECK (bypassRamp.getMagnitude (479, 545) < 0.00001f);
    set (processor, "bypass", 0.0f);
    const auto [enabledRamp, wet3] = process();
    CHECK (enabledRamp.getSample (0, 0) == Catch::Approx ((wet3.getSample (0, 0) - 0.125f) / 480.0f).margin (0.000001f));
    CHECK (enabledRamp.getSample (0, 479) == Catch::Approx (wet3.getSample (0, 479) - 0.125f).margin (0.000001f));
    set (processor, "mix", 0.0f);
    const auto [mixRamp, wet4] = process();
    CHECK (mixRamp.getSample (0, 0) == Catch::Approx ((wet4.getSample (0, 0) - 0.125f) * (1.0f - 1.0f / 960.0f)).margin (0.000001f));
    CHECK (mixRamp.getSample (0, 479) == Catch::Approx ((wet4.getSample (0, 479) - 0.125f) * 0.5f).margin (0.000001f));
    CHECK (mixRamp.getMagnitude (959, 65) < 0.00001f);
}

TEST_CASE ("Mix gain bypass monitor and LFM changes use finite bounded crossfades", "[ba][smoothing]")
{
    BaSignalPath path, reference;
    BaParameters::Values values;
    path.prepare (48000.0, 64, neutral(), values);
    reference.prepare (48000.0, 64, neutral(), values);
    auto prime = tone (2, 4096), primeReference = prime;
    path.process (prime, 2, neutral(), values);
    reference.process (primeReference, 2, neutral(), values);
    values.outGainDb = 6.0f;
    auto audio = tone (2, 4096), wet = audio;
    path.process (audio, 2, neutral(), values);
    reference.process (wet, 2, neutral(), {});
    // Digital OUT ramps over 20 ms from unity to +6 dB.
    CHECK (audio.getSample (0, 1) == Catch::Approx (wet.getSample (0, 1)).margin (0.001f));
    CHECK (audio.getSample (0, 1000) == Catch::Approx (wet.getSample (0, 1000) * juce::Decibels::decibelsToGain (6.0f)).margin (1.0e-6f));
    values.mixPercent = 0.0f;
    audio = signal (2, 4096);
    path.process (audio, 2, neutral(), values);
    CHECK (audio.getSample (0, 1000) == Catch::Approx (0.125f));
    values.monitor = 1;
    audio = signal (2, 4096);
    path.process (audio, 2, neutral(), values);
    CHECK (audio.getSample (0, 0) > 0.124f);
    CHECK (std::abs (audio.getSample (0, 500)) < 0.000001f);
}

TEST_CASE ("BA processing is identical for normal and oversized chunked blocks", "[ba][chunks]")
{
    for (int channels : { 1, 2 })
    {
        for (int capacity : { 1, 17, 64 })
        {
            auto compressor = neutral();
            compressor.thresholdDb = -30.0f;
            BaParameters::Values values;
            BaSignalPath reference, chunked;
            reference.prepare (48000.0, 4096, compressor, values);
            chunked.prepare (48000.0, capacity, compressor, values);
            values.inGainDb = 6.0f;
            values.outGainDb = -3.0f;
            values.lfmHz = 150.0f;
            values.mixPercent = 37.0f;
            values.monitor = 2;
            auto expected = signal (channels, 4096);
            auto actual = signal (channels, 4096);
            reference.process (expected, channels, compressor, values);
            chunked.process (actual, channels, compressor, values);
            CHECK (identical (expected, actual));
        }
    }
}

TEST_CASE ("BA parameter extremes remain finite for mono stereo sample rates and host block sizes", "[ba][finite]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
            for (int size : { 1, 17, 64, 257, 4096 })
            {
                BaSignalPath path;
                auto settings = neutral();
                BaParameters::Values values;
                path.prepare (sampleRate, 64, settings, values);
                juce::AudioBuffer<float> audio (channels, size);
                for (int combination = 0; combination < 128; ++combination)
                {
                    CAPTURE (sampleRate, channels, size, combination);
                    values.inGainDb = (combination & 1) != 0 ? 24.0f : -24.0f;
                    values.outGainDb = (combination & 2) != 0 ? 24.0f : -24.0f;
                    values.lfmHz = (combination & 4) != 0 ? 250.0f : 0.0f;
                    values.mixPercent = (combination & 8) != 0 ? 100.0f : 0.0f;
                    values.bypassed = (combination & 16) != 0;
                    values.monitor = (combination / 32) % 4;
                    settings.scFilterMode = combination % 7;
                    settings.inputDb = (combination & 2) != 0 ? 12.0f : -12.0f;
                    settings.makeupDb = (combination & 1) != 0 ? 24.0f : 0.0f;
                    settings.thresholdDb = (combination & 4) != 0 ? -40.0f : 0.0f;
                    settings.attackMs = (combination & 8) != 0 ? 0.2f : 30.0f;
                    settings.releaseMs = (combination & 16) != 0 ? 50.0f : 2000.0f;
                    const auto amplitude = combination % 3 == 0 ? 0.0f : combination % 3 == 1 ? 0.25f : 1.0e-30f;
                    for (int channel = 0; channel < channels; ++channel)
                        juce::FloatVectorOperations::fill (audio.getWritePointer (channel), channel == 0 ? amplitude : -amplitude, size);
                    const auto reduction = path.process (audio, channels, settings, values);
                    CHECK ((std::bit_cast<std::uint32_t> (reduction) & 0x7f800000U) != 0x7f800000U);
                    CHECK (isFinite (audio));
                }
            }
}

TEST_CASE ("IN and OUT display signed one-decimal dB text and parse typed values", "[ba][text]")
{
    PluginProcessor processor;
    for (const auto* id : { "inGain", "outGain" })
    {
        auto* p = parameter (processor, id);
        REQUIRE (p != nullptr);
        for (const auto& [value, expected] : std::array {
                 std::pair { -24.0f, "-24.0 dB" }, std::pair { -6.5f, "-6.5 dB" },
                 std::pair { 0.0f, "0.0 dB" }, std::pair { 6.0f, "+6.0 dB" },
                 std::pair { 24.0f, "+24.0 dB" } })
        {
            CAPTURE (id, value);
            CHECK (p->getText (p->convertTo0to1 (value), 64) == expected);
        }
        for (const auto& [text, expected] : std::array {
                 std::pair { "-6", -6.0f }, std::pair { "-6 dB", -6.0f },
                 std::pair { "+3.5", 3.5f }, std::pair { "3.5 dB", 3.5f },
                 std::pair { "-6.5 dB", -6.5f } })
        {
            CAPTURE (id, text);
            CHECK (p->convertFrom0to1 (p->getValueForText (text)) == Catch::Approx (expected).margin (0.00001f));
        }
        CHECK (p->getLabel().isEmpty()); // Avoid duplicate "dB dB" in the generic editor.
    }
}

TEST_CASE ("Only LFM numeric values below twenty Hz display Off", "[ba][text]")
{
    PluginProcessor processor;
    auto* p = parameter (processor, "lfm");
    REQUIRE (p != nullptr);
    for (const auto& [value, expected] : std::array {
             std::pair { 0.0f, "Off" }, std::pair { 19.9f, "Off" },
             std::pair { 20.0f, "20.0 Hz" }, std::pair { 88.0f, "88.0 Hz" },
             std::pair { 250.0f, "250.0 Hz" } })
    {
        CAPTURE (value);
        CHECK (p->getText (p->convertTo0to1 (value), 64) == expected);
    }
    CHECK (p->convertFrom0to1 (p->getValueForText ("Off")) == 0.0f);
    CHECK (p->convertFrom0to1 (p->getValueForText ("88.0 Hz")) == Catch::Approx (88.0f).margin (0.1f));
    CHECK (p->getLabel().isEmpty());
}

TEST_CASE ("All other parameter text is correct at minimum default and maximum", "[ba][text]")
{
    PluginProcessor processor;
    struct ExpectedText
    {
        const char* id;
        std::array<const char*, 3> text;
    };
    for (const auto& expected : std::array {
             ExpectedText { "input", { "-12.00 dB", "0.00 dB", "12.00 dB" } },
             ExpectedText { "threshold", { "-40.00 dB", "-18.00 dB", "0.00 dB" } },
             ExpectedText { "attack", { "0.2 ms", "5.0 ms", "30.0 ms" } },
             ExpectedText { "release", { "50 ms", "200 ms", "2000 ms" } },
             ExpectedText { "makeup", { "0.00 dB", "0.00 dB", "24.00 dB" } },
             ExpectedText { "mix", { "0.0 %", "100.0 %", "100.0 %" } },
             ExpectedText { "scFilter", { "Off", "Off", "Custom" } },
             ExpectedText { "monitor", { "LR", "LR", "D" } },
             ExpectedText { "bypass", { "Off", "Off", "On" } },
             ExpectedText { "ioLink", { "Off", "Off", "On" } } })
    {
        auto* p = parameter (processor, expected.id);
        REQUIRE (p != nullptr);
        const std::array normalized { 0.0f, p->getDefaultValue(), 1.0f };
        for (std::size_t i = 0; i < normalized.size(); ++i)
        {
            auto text = p->getText (normalized[i], 64);
            if (p->getLabel().isNotEmpty())
                text += " " + p->getLabel();
            CAPTURE (expected.id, i);
            CHECK (text == expected.text[i]);
        }
    }
}

TEST_CASE ("Formatting and parsing leave parameter values and rendered audio untouched", "[ba][text][audio]")
{
    PluginProcessor formatted, reference;
    for (auto* processor : { &formatted, &reference })
    {
        set (*processor, "inGain", -6.5f);
        set (*processor, "outGain", 6.0f);
        set (*processor, "threshold", -30.0f);
        set (*processor, "lfm", 150.0f);
        set (*processor, "mix", 50.0f);
        processor->prepareToPlay (48000.0, 17);
    }
    for (auto* p : formatted.getParameters())
    {
        const auto before = p->getValue();
        for (float normalized : { 0.0f, p->getDefaultValue(), 1.0f })
            juce::ignoreUnused (p->getValueForText (p->getText (normalized, 64)));
        CHECK (p->getValue() == Catch::Approx (before).margin (0.00000001f));
    }
    auto actual = signal (2, 4096);
    auto expected = signal (2, 4096);
    juce::MidiBuffer midi;
    formatted.processBlock (actual, midi);
    reference.processBlock (expected, midi);
    CHECK (identical (actual, expected));
}
