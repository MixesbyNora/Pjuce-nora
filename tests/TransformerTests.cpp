#include <PluginProcessor.h>
#include <dsp/VtbOutputTransformer.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <complex>
#include <cstdint>
#include <limits>
#include <fstream>

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

bool isFinite (double value)
{
    return (std::bit_cast<std::uint64_t> (value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}

bool isFinite (const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if (! isFinite (static_cast<double> (audio.getSample (channel, i))))
                return false;
    return true;
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int start = 0)
{
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        if (std::memcmp (a.getReadPointer (channel, start), b.getReadPointer (channel, start),
            static_cast<std::size_t> (a.getNumSamples() - start) * sizeof (float)) != 0)
            return false;
    return true;
}

juce::AudioBuffer<float> sine (double sampleRate, double frequency, double levelDb, int samples, int channels = 2)
{
    juce::AudioBuffer<float> audio (channels, samples);
    const auto amplitude = juce::Decibels::decibelsToGain (levelDb);
    for (int i = 0; i < samples; ++i)
    {
        const auto value = static_cast<float> (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate));
        for (int channel = 0; channel < channels; ++channel)
            audio.setSample (channel, i, channel == 0 ? value : -value);
    }
    return audio;
}

juce::AudioBuffer<float> delayed (const juce::AudioBuffer<float>& input, int latency)
{
    juce::AudioBuffer<float> result (input.getNumChannels(), input.getNumSamples());
    result.clear();
    for (int channel = 0; channel < input.getNumChannels(); ++channel)
        result.copyFrom (channel, latency, input, channel, 0, input.getNumSamples() - latency);
    return result;
}

double harmonic (const juce::AudioBuffer<float>& audio, double frequency, double sampleRate, int start, int count)
{
    std::complex<double> sum {};
    for (int i = 0; i < count; ++i)
    {
        const auto angle = -2.0 * juce::MathConstants<double>::pi * frequency * (i + start) / sampleRate;
        sum += static_cast<double> (audio.getSample (0, i + start)) * std::complex<double> (std::cos (angle), std::sin (angle));
    }
    return std::abs (sum) * 2.0 / count;
}
}

TEST_CASE ("VTB1148 transformer is deterministic across block partitions and automation", "[transformer][vtb]")
{
    auto a = sine (48000.0, 73.0, -3.0, 8192), b = a;
    VtbOutputTransformer whole, split;
    whole.prepare (48000.0, 100.0f, false);
    split.prepare (48000.0, 100.0f, false);
    CHECK (whole.getLatencySamples() == 48); // Relaxed 4x half-band FIRs at 48 kHz.
    for (int start = 0; start < a.getNumSamples(); start += 128)
    {
        for (auto* stage : { &whole, &split })
        {
            if (start == 2048) stage->setParameters (25.0f, false);
            if (start == 4096) stage->setParameters (25.0f, true);
            if (start == 6144) stage->setParameters (100.0f, false);
        }
        std::array<float*, 2> pa { a.getWritePointer (0, start), a.getWritePointer (1, start) };
        juce::AudioBuffer<float> block (pa.data(), 2, 128);
        whole.process (block, 2);
        for (int offset : { 0, 64 })
        {
            std::array<float*, 2> pb { b.getWritePointer (0, start + offset), b.getWritePointer (1, start + offset) };
            juce::AudioBuffer<float> part (pb.data(), 2, 64);
            split.process (part, 2);
        }
    }
    CHECK (identical (a, b));
    CHECK (whole.getDiagnostics().failures == 0);
}

TEST_CASE ("Transformer parameters append stable IDs with In Out text and percent parsing", "[transformer][parameters]")
{
    PluginProcessor processor;
    auto* out = parameter (processor, "xfrmOut");
    auto* amount = parameter (processor, "xfrmAmount");
    REQUIRE (dynamic_cast<juce::AudioParameterBool*> (out) != nullptr);
    REQUIRE (dynamic_cast<juce::AudioParameterFloat*> (amount) != nullptr);
    CHECK (out->getValue() == 0.0f);
    CHECK (out->getText (0.0f, 32) == "In");
    CHECK (out->getText (1.0f, 32) == "Out");
    CHECK (out->getValueForText ("In") == 0.0f);
    CHECK (out->getValueForText ("Out") == 1.0f);
    // Defaults to the calibrated 0 dB drive, 400/7 % on the 0.1 grid.
    CHECK (amount->convertFrom0to1 (amount->getValue()) == Catch::Approx (57.1f).margin (0.01f));
    CHECK (amount->getNormalisableRange().start == 0.0f);
    CHECK (amount->getNormalisableRange().end == 100.0f);
    CHECK (amount->getNormalisableRange().interval == Catch::Approx (0.1f));
    CHECK (amount->getLabel() == "%");
    CHECK (amount->getText (amount->convertTo0to1 (35.5f), 32) == "35.5");
    CHECK (amount->convertFrom0to1 (amount->getValueForText ("35.5 %")) == Catch::Approx (35.5f));
    CHECK (processor.getParameters().indexOf (parameter (processor, "fbff")) == 13);
    CHECK (processor.getParameters().indexOf (out) == 14);
    CHECK (processor.getParameters().indexOf (amount) == 15);
    CHECK (out->getVersionHint() > parameter (processor, "fbff")->getVersionHint());
}

TEST_CASE ("Transformer Out preserves samples bit for bit at any Amount, including after fades", "[transformer][bypass]")
{
    // Owner decision: Amount is drive only; the Out switches are the bypass.
    const auto original = sine (48000.0, 73.0, -3.0, 8192);
    for (const auto amount : { 0.0f, 100.0f })
    {
        VtbOutputTransformer stage;
        stage.prepare (48000.0, amount, true);
        auto audio = original;
        stage.process (audio, 2);
        CHECK (identical (audio, delayed (original, stage.getLatencySamples())));
    }
    for (const auto amount : { 0.0f, 100.0f })
    {
        VtbOutputTransformer stage;
        stage.prepare (48000.0, 100.0f, false);
        auto audio = original;
        stage.process (audio, 2);
        CHECK_FALSE (identical (audio, original));
        stage.setParameters (amount, true);
        audio = original;
        stage.process (audio, 2);
        CHECK (identical (audio, delayed (original, stage.getLatencySamples()), 480));
    }
    // All existing BA features are active; with the output stage fully Out (tube
    // and transformer) the Amounts make no difference.
    PluginProcessor zero, out;
    for (auto* processor : { &zero, &out })
    {
        set (*processor, "xfrmOut", 1.0f);
        set (*processor, "outputTubeOut", 1.0f);
        set (*processor, "inGain", 3.0f);
        set (*processor, "outGain", -3.0f);
        set (*processor, "threshold", -30.0f);
        set (*processor, "makeup", 6.0f);
        set (*processor, "fbff", 25.0f);
        set (*processor, "scFilter", 4.0f);
        set (*processor, "lfm", 88.0f);
        set (*processor, "mix", 37.0f);
    }
    set (zero, "xfrmAmount", 0.0f);
    set (zero, "tubeAmount", 0.0f);
    set (out, "xfrmAmount", 100.0f);
    set (out, "tubeAmount", 100.0f);
    for (auto* processor : { &zero, &out })
        processor->prepareToPlay (48000.0, 256);
    auto a = original;
    auto b = original;
    juce::MidiBuffer midi;
    zero.processBlock (a, midi);
    out.processBlock (b, midi);
    CHECK (identical (a, b));
}

TEST_CASE ("VTB1148 transformer preserves small-signal gain above its low-frequency corner", "[transformer][unity]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double frequency : { 50.0, 1000.0, 8000.0 })
        {
            CAPTURE (sampleRate, frequency);
            const auto count = juce::roundToInt (sampleRate);
            const auto warmup = count / 4;
            const auto input = sine (sampleRate, frequency, -60.0, count + warmup);
            auto output = input;
            VtbOutputTransformer stage;
            stage.prepare (sampleRate, 100.0f, false);
            stage.process (output, 2);
            const auto gain = output.getRMSLevel (0, warmup, count) / input.getRMSLevel (0, warmup, count);
            CHECK (juce::Decibels::gainToDecibels (gain) == Catch::Approx (0.0).margin (0.1));
        }
}

TEST_CASE ("VTB1148 transformer distorts more at low frequencies with odd and even harmonics", "[transformer][harmonics]")
{
    // Published VTB1148 behavior (Woods 2019): harmonics fall with frequency and
    // include even orders; the calibration puts H2 roughly 20 dB under H3.
    auto highThirdDb = 0.0;
    for (double frequency : { 1000.0, 60.0 })
    {
        CAPTURE (frequency);
        auto audio = sine (48000.0, frequency, -6.0, 60000);
        VtbOutputTransformer stage;
        stage.prepare (48000.0, 100.0f, false);
        stage.process (audio, 2);
        const auto fundamental = harmonic (audio, frequency, 48000.0, 12000, 48000);
        const auto second = harmonic (audio, frequency * 2.0, 48000.0, 12000, 48000);
        const auto third = harmonic (audio, frequency * 3.0, 48000.0, 12000, 48000);
        const auto thirdDb = juce::Decibels::gainToDecibels (third / fundamental, -300.0);
        const auto secondDb = juce::Decibels::gainToDecibels (second / fundamental, -300.0);
        INFO ("H3 dBc = " << thirdDb << "; H2 dBc = " << secondDb);
        if (frequency > 500.0)
            highThirdDb = thirdDb;
        else
        {
            CHECK (thirdDb > highThirdDb + 10.0);
            CHECK (secondDb > -100.0);
            CHECK (secondDb < thirdDb);
        }
    }
}

TEST_CASE ("Transformer amount sweeps and bypass re-engagement stay free of control clicks", "[transformer][smoothing]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        CAPTURE (sampleRate);
        VtbOutputTransformer stage;
        stage.prepare (sampleRate, 0.0f, false);
        juce::AudioBuffer<float> frame (2, 1);
        auto previousInput = 0.0f;
        auto previousOutput = 0.0f;
        auto maximumExcessJump = 0.0f;
        const auto count = juce::roundToInt (sampleRate);
        for (int i = 0; i < count; ++i)
        {
            const auto time = static_cast<double> (i) / sampleRate;
            if (i % 64 == 0)
            {
                const auto amount = time < 0.4 ? static_cast<float> (time / 0.4 * 100.0)
                                              : time > 0.8 ? 0.0f : 100.0f;
                const auto out = (time > 0.2 && time < 0.3) || (time > 0.5 && time < 0.6);
                stage.setParameters (amount, out);
            }
            const auto input = static_cast<float> (0.95 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * time));
            frame.setSample (0, 0, input);
            frame.setSample (1, 0, -input);
            stage.process (frame, 2);
            const auto output = frame.getSample (0, 0);
            maximumExcessJump = juce::jmax (maximumExcessJump,
                std::abs (output - previousOutput) - std::abs (input - previousInput));
            previousInput = input;
            previousOutput = output;
        }
        CHECK (maximumExcessJump <= 0.05f);
    }
}

TEST_CASE ("Transformer and processor remain finite through loud DC noise block sizes and layouts", "[transformer][finite]")
{
    juce::ScopedNoDenormals noDenormals;
    juce::Random random (0x20102026);
    const auto loud = juce::Decibels::decibelsToGain (24.0f);
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
            for (int size : { 1, 7, 64, 256, 1024, 4096 })
            {
                CAPTURE (sampleRate, channels, size);
                VtbOutputTransformer stage;
                stage.prepare (sampleRate, 100.0f, false);
                PluginProcessor processor;
                if (channels == 1)
                {
                    auto layout = processor.getBusesLayout();
                    layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
                    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
                    REQUIRE (processor.setBusesLayout (layout));
                }
                set (processor, "xfrmAmount", 100.0f);
                processor.prepareToPlay (sampleRate, 64);
                juce::AudioBuffer<float> audio (channels, size);
                juce::MidiBuffer midi;
                for (int block = 0; block < 8; ++block)
                {
                    const auto out = block == 6;
                    stage.setParameters (100.0f, out);
                    set (processor, "xfrmOut", out ? 1.0f : 0.0f);
                    for (int channel = 0; channel < channels; ++channel)
                        for (int i = 0; i < size; ++i)
                        {
                            const auto input = block == 0 ? 0.0f : block == 1 ? loud : block == 2 ? -loud
                                                                                         : block == 5 ? 1.0e-30f : (random.nextFloat() * 2.0f - 1.0f) * loud;
                            audio.setSample (channel, i, input);
                        }
                    auto direct = audio;
                    stage.process (direct, channels);
                    processor.processBlock (audio, midi);
                    CHECK (isFinite (direct));
                    CHECK (isFinite (audio));
                }
            }
}

TEST_CASE ("Transformer leaves detection and meter positions intact and Delta still follows Mix", "[transformer][integration]")
{
    const auto dry = sine (48000.0, 73.0, -12.0, 8192);
    const auto latencyDry = delayed (dry, 48);
    const auto render = [&dry] (float amount, int monitor, float mix, float outGain = 0.0f)
    {
        PluginProcessor processor;
        set (processor, "threshold", -30.0f);
        set (processor, "makeup", 6.0f);
        set (processor, "xfrmAmount", amount);
        set (processor, "monitor", static_cast<float> (monitor));
        set (processor, "mix", mix);
        set (processor, "outGain", outGain);
        processor.prepareToPlay (48000.0, 256);
        auto audio = dry;
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        const auto levels = processor.readLevelPeaks();
        CHECK (levels.inL == Catch::Approx (dry.getMagnitude (0, 0, dry.getNumSamples())));
        CHECK (levels.inR == Catch::Approx (dry.getMagnitude (1, 0, dry.getNumSamples())));
        CHECK (levels.outL == Catch::Approx (audio.getMagnitude (0, 0, audio.getNumSamples())).margin (1.0e-7f));
        CHECK (levels.outR == Catch::Approx (audio.getMagnitude (1, 0, audio.getNumSamples())).margin (1.0e-7f));
        return std::pair { audio, processor.getGainReductionDb() };
    };
    const auto [clean, cleanGr] = render (0.0f, 0, 100.0f);
    const auto [wet, wetGr] = render (100.0f, 0, 100.0f);
    CHECK (wetGr == Catch::Approx (cleanGr).margin (0.000001f));
    CHECK_FALSE (identical (clean, wet));
    const auto [boosted, boostedGr] = render (100.0f, 0, 100.0f, 6.0f);
    CHECK (boostedGr == Catch::Approx (wetGr));
    for (float mix : { 0.0f, 50.0f, 100.0f })
    {
        const auto [delta, gr] = render (100.0f, 3, mix);
        CHECK (gr == Catch::Approx (wetGr));
        auto error = 0.0f;
        auto gainError = 0.0f;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < dry.getNumSamples(); ++i)
            {
                const auto expected = (wet.getSample (channel, i) - latencyDry.getSample (channel, i)) * mix * 0.01f;
                error = juce::jmax (error, std::abs (delta.getSample (channel, i) - expected));
                gainError = juce::jmax (gainError, std::abs (boosted.getSample (channel, i)
                    - wet.getSample (channel, i) * juce::Decibels::decibelsToGain (6.0f)));
            }
        CHECK (error < 0.000001f);
        CHECK (gainError < 0.000001f); // OUT gain follows the nonlinear stage.
    }
}
