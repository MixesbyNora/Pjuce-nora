#include <PluginProcessor.h>
#include <MeterBallistics.h>
#include <PeakAccumulator.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <cstdint>
#include <thread>

namespace
{
void setParameter (PluginProcessor& processor, const char* id, float value)
{
    for (auto* parameter : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
            if (ranged->paramID == id)
            {
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
                return;
            }
    FAIL ("Missing parameter " << id);
}

bool finite (float value)
{
    return (std::bit_cast<std::uint32_t> (value) & 0x7f800000U) != 0x7f800000U;
}

void configure (PluginProcessor& processor, double sampleRate, int channels)
{
    if (channels == 1)
    {
        auto layout = processor.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        REQUIRE (processor.setBusesLayout (layout));
    }
    setParameter (processor, "threshold", 0.0f);
    processor.prepareToPlay (sampleRate, 64);
}

juce::AudioBuffer<float> sine (int channels, int samples, double sampleRate)
{
    juce::AudioBuffer<float> audio (channels, samples);
    const auto amplitude = juce::Decibels::decibelsToGain (-6.0f);
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < samples; ++sample)
            audio.setSample (channel, sample, amplitude * static_cast<float> (
                std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * sample / sampleRate)));
    return audio;
}
}

TEST_CASE ("Peak accumulator captures a mid-block spike and resets on read", "[meters][peaks]")
{
    std::array<float, 4096> samples {};
    samples[2048] = -0.9f;
    std::atomic<float> peak { 0.0f };
    PeakAccumulator::publish (peak, PeakAccumulator::findPeak (samples.data(), static_cast<int> (samples.size())));
    PeakAccumulator::publish (peak, 0.1f);
    CHECK (PeakAccumulator::read (peak) == Catch::Approx (0.9f));
    CHECK (PeakAccumulator::read (peak) == 0.0f);
    PeakAccumulator::publish (peak, 0.25f);
    CHECK (PeakAccumulator::read (peak) == Catch::Approx (0.25f));
}

TEST_CASE ("Concurrent peak publication and UI exchange do not lose a short peak", "[meters][peaks]")
{
    std::atomic<float> peak { 0.0f };
    std::atomic<bool> done { false };
    std::thread producer ([&]
    {
        for (int i = 0; i < 10000; ++i)
            PeakAccumulator::publish (peak, i == 5000 ? 1.0f : 0.125f);
        done.store (true, std::memory_order_release);
    });
    auto largestRead = 0.0f;
    while (! done.load (std::memory_order_acquire))
        largestRead = std::max (largestRead, PeakAccumulator::read (peak));
    producer.join();
    largestRead = std::max (largestRead, PeakAccumulator::read (peak));
    CHECK (largestRead == Catch::Approx (1.0f));
    CHECK (PeakAccumulator::read (peak) == 0.0f);
}

TEST_CASE ("Level meter ballistics have instant attack and 24 dB per second release", "[meters][ballistics]")
{
    MeterBallistics meter;
    meter.update (-6.0f, 0.0);
    CHECK (meter.getLevelDb() == Catch::Approx (-6.0f));
    meter.update (-60.0f, 0.5);
    CHECK (meter.getLevelDb() == Catch::Approx (-18.0f).margin (1.2f));
    meter.update (-60.0f, 0.5);
    CHECK (meter.getLevelDb() == Catch::Approx (-30.0f).margin (2.4f));
    meter.update (3.0f, 0.0);
    CHECK (meter.getLevelDb() == Catch::Approx (3.0f));
    meter.update (-60.0f, 10.0);
    CHECK (meter.getLevelDb() == Catch::Approx (-60.0f));
}

TEST_CASE ("Level meter peak marker holds for 1.5 seconds then falls at 24 dB per second", "[meters][ballistics]")
{
    MeterBallistics meter;
    meter.update (0.0f, 0.0);
    for (int frame = 0; frame < 45; ++frame)
        meter.update (-60.0f, 1.0 / 30.0);
    CHECK (meter.getPeakDb() == Catch::Approx (0.0f).margin (0.0001f));
    meter.update (-60.0f, 0.1);
    CHECK (meter.getPeakDb() == Catch::Approx (-2.4f).margin (0.001f));
    meter.update (-60.0f, 0.5);
    CHECK (meter.getPeakDb() == Catch::Approx (-14.4f).margin (0.001f));
    meter.update (6.0f, 0.0);
    CHECK (meter.getPeakDb() == Catch::Approx (6.0f));
    meter.update (-60.0f, 1.4);
    CHECK (meter.getPeakDb() == Catch::Approx (6.0f));
}

TEST_CASE ("Stereo sine levels are measured after IN gain and at the final output", "[meters][levels]")
{
    for (float gainDb : { 0.0f, 6.0f })
    {
        PluginProcessor processor;
        setParameter (processor, "inGain", gainDb);
        configure (processor, 48000.0, 2);
        auto audio = sine (2, 4096, 48000.0);
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        const auto peaks = processor.readLevelPeaks();
        CHECK (juce::Decibels::gainToDecibels (peaks.inL) == Catch::Approx (-6.0f + gainDb).margin (0.1f));
        CHECK (juce::Decibels::gainToDecibels (peaks.inR) == Catch::Approx (-6.0f + gainDb).margin (0.1f));
        if (gainDb <= 0.0f)
        {
            CHECK (juce::Decibels::gainToDecibels (peaks.outL) == Catch::Approx (-6.0f).margin (0.1f));
            CHECK (juce::Decibels::gainToDecibels (peaks.outR) == Catch::Approx (-6.0f).margin (0.1f));
        }
        CHECK (peaks.outL == Catch::Approx (PeakAccumulator::findPeak (audio.getReadPointer (0), audio.getNumSamples())));
        const auto cleared = processor.readLevelPeaks();
        CHECK (cleared.inL == 0.0f);
        CHECK (cleared.inR == 0.0f);
        CHECK (cleared.outL == 0.0f);
        CHECK (cleared.outR == 0.0f);
    }
}

TEST_CASE ("Output measurement includes monitor processing and input measurement precedes compression", "[meters][positions]")
{
    PluginProcessor processor;
    setParameter (processor, "inGain", 6.0f);
    setParameter (processor, "input", -12.0f);
    setParameter (processor, "monitor", 2.0f);
    configure (processor, 48000.0, 2);
    auto audio = sine (2, 4096, 48000.0); // L == R, so S monitoring must be silent.
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    auto peaks = processor.readLevelPeaks();
    CHECK (juce::Decibels::gainToDecibels (peaks.inL) == Catch::Approx (0.0f).margin (0.1f));
    CHECK (peaks.outL == 0.0f);
    CHECK (peaks.outR == 0.0f);

    setParameter (processor, "monitor", 0.0f);
    setParameter (processor, "bypass", 1.0f);
    processor.prepareToPlay (48000.0, 64);
    audio = sine (2, 4096, 48000.0);
    processor.processBlock (audio, midi);
    peaks = processor.readLevelPeaks();
    CHECK (juce::Decibels::gainToDecibels (peaks.inL) == Catch::Approx (0.0f).margin (0.1f));
    CHECK (juce::Decibels::gainToDecibels (peaks.outL) == Catch::Approx (-6.0f).margin (0.1f));
}

TEST_CASE ("Level taps never alter the pre-meter signal path output", "[meters][audio]")
{
    for (int channels : { 1, 2 })
    {
        BaSignalPath reference, metered;
        CompressorParameters::Values settings;
        BaParameters::Values values;
        reference.prepare (48000.0, 17, settings, values);
        metered.prepare (48000.0, 17, settings, values);
        LevelMeterPeaks peaks;
        for (int block = 0; block < 8; ++block)
        {
            values.inGainDb = block % 2 == 0 ? 6.0f : -6.0f;
            values.outGainDb = block % 2 == 0 ? -3.0f : 6.0f;
            values.lfmHz = block % 2 == 0 ? 150.0f : 0.0f;
            values.mixPercent = static_cast<float> (block * 10);
            values.monitor = block % 4;
            values.bypassed = block == 3;
            settings.scFilterMode = block % 7;
            auto actual = sine (channels, 4096, 48000.0);
            juce::AudioBuffer<float> expected;
            expected.makeCopyOf (actual);
            const auto expectedGr = reference.process (expected, channels, settings, values);
            const auto actualGr = metered.process (actual, channels, settings, values, &peaks);
            CHECK (actualGr == Catch::Approx (expectedGr).margin (0.0000001f));
            for (int channel = 0; channel < channels; ++channel)
                CHECK (std::memcmp (actual.getReadPointer (channel), expected.getReadPointer (channel),
                    static_cast<std::size_t> (actual.getNumSamples()) * sizeof (float)) == 0);
            peaks.read();
        }
    }
}

TEST_CASE ("Meter peaks persist through quieter blocks until the UI reads them", "[meters][peaks]")
{
    PluginProcessor processor;
    configure (processor, 48000.0, 2);
    auto audio = sine (2, 4096, 48000.0);
    audio.applyGain (1, 0, audio.getNumSamples(), 0.25f);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    audio.clear();
    processor.processBlock (audio, midi);
    const auto peaks = processor.readLevelPeaks();
    CHECK (juce::Decibels::gainToDecibels (peaks.inL) == Catch::Approx (-6.0f).margin (0.1f));
    CHECK (juce::Decibels::gainToDecibels (peaks.outL) == Catch::Approx (-6.0f).margin (0.1f));
    CHECK (juce::Decibels::gainToDecibels (peaks.inR) == Catch::Approx (-18.04f).margin (0.1f));
    CHECK (juce::Decibels::gainToDecibels (peaks.outR) == Catch::Approx (-18.04f).margin (0.1f));
}

TEST_CASE ("Meters stay finite for silence mono stereo block sizes and sample rates", "[meters][finite]")
{
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            PluginProcessor processor;
            configure (processor, sampleRate, channels);
            juce::MidiBuffer midi;
            for (int size : { 1, 17, 64, 257, 4096 })
                for (float amplitude : { 0.0f, 0.125f, 1.0e-30f })
                {
                    CAPTURE (sampleRate, channels, size, amplitude);
                    juce::AudioBuffer<float> audio (channels, size);
                    for (int channel = 0; channel < channels; ++channel)
                        juce::FloatVectorOperations::fill (audio.getWritePointer (channel), amplitude, size);
                    processor.processBlock (audio, midi);
                    const auto peaks = processor.readLevelPeaks();
                    for (float value : { peaks.inL, peaks.inR, peaks.outL, peaks.outR })
                    {
                        CHECK (finite (value));
                        CHECK (finite (juce::Decibels::gainToDecibels (value, -60.0f)));
                    }
                    if (channels == 1)
                    {
                        CHECK (peaks.inR == Catch::Approx (peaks.inL).margin (1.0e-30f));
                        CHECK (peaks.outR == Catch::Approx (peaks.outL).margin (1.0e-30f));
                    }
                    CHECK (finite (audio.getMagnitude (0, size)));
                }
        }
}
