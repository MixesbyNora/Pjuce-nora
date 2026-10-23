#include "helpers/CompressorUnderTest.h"
#include <PluginProcessor.h>
#include <TransformerVoicingPanel.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <complex>

// The vari-mu gain element and Calibration (docs/GainTube.md).
namespace
{
using GainTube::Tube;
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
// Harmonics of the memoryless cell for one period of a sine (complex amplitudes).
std::array<std::complex<double>, 8> harmonics (const GainTube::Cell& cell, double peak, double reductionDb)
{
    constexpr int count = 512;
    std::array<std::complex<double>, 8> h {};
    for (int n = 0; n < count; ++n)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * n / count;
        const auto y = cell.process (peak * std::sin (phase), reductionDb);
        for (std::size_t k = 0; k < h.size(); ++k) h[k] += y * std::polar (1.0, -static_cast<double> (k) * phase) * (2.0 / count);
    }
    return h;
}
double dbc (const std::array<std::complex<double>, 8>& h, std::size_t k) { return 20.0 * std::log10 (std::abs (h[k]) / std::abs (h[1]) + 1.0e-30); }
double thdDb (const std::array<std::complex<double>, 8>& h)
{
    double sum = 0;
    for (std::size_t k = 2; k < h.size(); ++k) sum += std::norm (h[k]);
    return 10.0 * std::log10 (sum / std::norm (h[1]) + 1.0e-30);
}
juce::AudioBuffer<float> sine (double rate, double hz, float peak, int samples)
{
    juce::AudioBuffer<float> audio (2, samples);
    for (int n = 0; n < samples; ++n)
        for (int ch = 0; ch < 2; ++ch)
            audio.setSample (ch, n, peak * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / rate)));
    return audio;
}
}

TEST_CASE ("Gain cell's small-signal gain is exactly the requested gain reduction for both tubes", "[gain-tube]")
{
    for (auto tube : { Tube::ge5670, Tube::ge6386 })
    {
        GainTube::Cell cell;
        cell.configure ({ tube, 0.0, 0.0 });
        for (double reduction : { 0.0, 3.0, 10.0, 20.0, 30.0, 40.0 })
        {
            CAPTURE (static_cast<int> (tube), reduction);
            const auto h = harmonics (cell, 0.001, reduction); // -60 dBFS: small signal.
            CHECK (20.0 * std::log10 (std::abs (h[1]) / 0.001) == Catch::Approx (-reduction).margin (0.02));
        }
    }
}

TEST_CASE ("Gain cell distortion follows the tube: rising with GR, and odd only when balanced", "[gain-tube]")
{
    constexpr double plus4dBu = 0.2; // Peak, with 0 dBFS = +18 dBu.
    GainTube::Cell sharp, remote;
    sharp.configure ({ Tube::ge5670, 0.0, 0.0 });
    remote.configure ({ Tube::ge6386, 0.0, 0.0 });
    // The 5670's distortion climbs steeply toward cut-off (docs/GainTube.md: about 0.002% to 0.16%).
    CHECK (thdDb (harmonics (sharp, plus4dBu, 20.0)) > thdDb (harmonics (sharp, plus4dBu, 0.0)) + 30.0);
    // Both stay under Manley's published 0.1% THD at nominal level without GR.
    CHECK (thdDb (harmonics (sharp, plus4dBu, 0.0)) < -60.0);
    CHECK (thdDb (harmonics (remote, plus4dBu, 0.0)) < -60.0);
    // A balanced, matched pair cancels even harmonics; imbalance brings H2 back.
    for (auto* cell : { &sharp, &remote })
        CHECK (dbc (harmonics (*cell, plus4dBu, 10.0), 2) < -120.0);
    GainTube::Cell mismatched;
    mismatched.configure ({ Tube::ge5670, 0.0, 0.05 });
    CHECK (dbc (harmonics (mismatched, plus4dBu, 10.0), 2) > -70.0);
    // More grid swing, more distortion at the same GR.
    GainTube::Cell hot;
    hot.configure ({ Tube::ge5670, 12.0, 0.0 });
    CHECK (thdDb (harmonics (hot, plus4dBu, 10.0)) > thdDb (harmonics (sharp, plus4dBu, 10.0)) + 15.0);
}

TEST_CASE ("Gain element off is the exact digital gain, on it changes only the color", "[gain-tube][compressor]")
{
    const auto render = [] (std::optional<StereoCompressor::GainElement> element)
    {
        StereoCompressor compressor;
        CompressorParameters::Values values;
        values.thresholdDb = -30.0f;
        if (element) compressor.setGainElement (*element);
        compressor.prepare (192000.0, values);
        auto audio = sine (192000.0, 1000.0, 0.25f, 96000);
        const auto reduction = compressor.process (audio, 2);
        return std::pair { audio, reduction };
    };
    const auto [plain, plainGr] = render (std::nullopt);
    const auto [off, offGr] = render (StereoCompressor::GainElement { false, {} });
    const auto [tube, tubeGr] = render (StereoCompressor::GainElement { true, {} });
    CHECK (std::memcmp (plain.getReadPointer (0), off.getReadPointer (0), sizeof (float) * 96000) == 0);
    CHECK (tubeGr == Catch::Approx (plainGr)); // The detector does not see the tube.
    CHECK (tubeGr > 6.0f);
    // Same level within 0.1 dB, but not identical.
    const auto level = [] (const juce::AudioBuffer<float>& a) { return juce::Decibels::gainToDecibels (a.getRMSLevel (0, 48000, 48000)); };
    CHECK (level (tube) == Catch::Approx (level (plain)).margin (0.1));
    CHECK (std::memcmp (plain.getReadPointer (0), tube.getReadPointer (0), sizeof (float) * 96000) != 0);
}

TEST_CASE ("Calibration moves only the detector's level", "[gain-tube][calibration][compressor]")
{
    // Steady GR with Calibration +6 dB equals the GR of a 6 dB hotter input at 0.
    const auto steadyGr = [] (float peak, float calibrationDb)
    {
        CompressorUnderTest compressor;
        compressor.set ("threshold", -30.0f);
        compressor.set ("calibration", calibrationDb);
        compressor.prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        auto audio = sine (48000.0, 1000.0, peak, 512);
        for (int block = 0; block < 400; ++block)
        {
            audio = sine (48000.0, 1000.0, peak, 512);
            compressor.processBlock (audio, midi);
        }
        return compressor.getGainReductionDb();
    };
    CHECK (steadyGr (0.1f, 6.0f) == Catch::Approx (steadyGr (0.2f, 0.0f)).margin (0.1));
    CHECK (steadyGr (0.1f, -6.0f) == Catch::Approx (steadyGr (0.05f, 0.0f)).margin (0.1));
    CHECK (steadyGr (0.1f, -6.0f) < steadyGr (0.1f, 0.0f) - 1.0f);
    // Below threshold either way, Calibration leaves the audio untouched.
    const auto quiet = [] (float calibrationDb)
    {
        CompressorUnderTest compressor;
        compressor.set ("threshold", 0.0f);
        compressor.set ("calibration", calibrationDb);
        compressor.prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        auto audio = sine (48000.0, 1000.0, 0.01f, 512);
        compressor.processBlock (audio, midi);
        return audio;
    };
    const auto a = quiet (0.0f), b = quiet (12.0f);
    CHECK (std::memcmp (a.getReadPointer (0), b.getReadPointer (0), sizeof (float) * 512) == 0);
}

TEST_CASE ("Gain tube switching and tube changes crossfade without a step", "[gain-tube][smoothing]")
{
    StereoCompressor compressor;
    CompressorParameters::Values values;
    values.thresholdDb = -30.0f;
    compressor.setGainElement ({ true, {} });
    compressor.prepare (192000.0, values);
    const auto dry = sine (192000.0, 110.0, 0.5f, 192000);
    auto audio = dry;
    for (int start = 0; start < audio.getNumSamples(); start += 1024)
    {
        if (start == 40960) compressor.setGainElement ({ true, { Tube::ge6386, 6.0, 0.1 } });
        if (start == 81920) compressor.setGainElement ({ false, {} });
        if (start == 122880) compressor.setGainElement ({ true, { Tube::ge5670, -6.0, 0.0 } });
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, start, 1024);
        compressor.process (view, 2);
    }
    float largest = 0, inputStep = 0;
    bool finite = true;
    for (int n = 20000; n < audio.getNumSamples(); ++n)
    {
        finite &= ModuleSpike::isFinite (audio.getSample (0, n));
        largest = std::max (largest, std::abs (audio.getSample (0, n) - audio.getSample (0, n - 1)));
        inputStep = std::max (inputStep, std::abs (dry.getSample (0, n) - dry.getSample (0, n - 1)));
    }
    CHECK (finite);
    CHECK (largest < 1.5f * inputStep);
}

TEST_CASE ("Calibration and Gain Tube are appended; the gain tube workbench round-trips", "[gain-tube][parameters][state][editor]")
{
    PluginProcessor processor;
    auto* calibration = parameter (processor, "calibration");
    auto* gainTube = dynamic_cast<juce::AudioParameterBool*> (parameter (processor, "gainTubeOut"));
    REQUIRE (calibration != nullptr);
    REQUIRE (gainTube != nullptr);
    CHECK (calibration->getName (32) == "Calibration");
    CHECK (calibration->getNormalisableRange().start == Catch::Approx (-24.0f));
    CHECK (calibration->getNormalisableRange().end == Catch::Approx (24.0f));
    CHECK (calibration->convertFrom0to1 (calibration->getDefaultValue()) == Catch::Approx (0.0f).margin (1.0e-6));
    CHECK (calibration->getVersionHint() == 8);
    CHECK (gainTube->getName (32) == "Gain Tube");
    CHECK_FALSE (gainTube->get());
    CHECK (gainTube->getVersionHint() == 8);
    CHECK (processor.getParameters().indexOf (gainTube) == processor.getParameters().size() - 1);
    CHECK (processor.getParameters().indexOf (calibration) == processor.getParameters().size() - 2);
    processor.getGainTubeVoicing().setCurrent ({ 1.0f, 4.5f, 2.5f });
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    PluginProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (GainTubeVoicing::equal (restored.getGainTubeVoicing().readAudio().voicing, { 1.0f, 4.5f, 2.5f }));
    CHECK (juce::JSON::parse (processor.exportGainTubeVoicing())["gainTube"].toString().isNotEmpty());
    // The editor's fourth stage shows the tube choice by name.
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* stage = dynamic_cast<juce::Button*> (editor->findChildWithID ("voicingStageGainTube"));
    auto* panel = editor->findChildWithID ("gainTubeVoicingPanel");
    REQUIRE (stage != nullptr);
    REQUIRE (panel != nullptr);
    stage->onClick();
    CHECK (panel->isVisible());
    auto* choice = dynamic_cast<juce::Slider*> (panel->findChildWithID ("voicing_tube"));
    REQUIRE (choice != nullptr);
    CHECK (choice->getTextFromValue (0.0) == "5670");
    CHECK (choice->getTextFromValue (1.0) == "6386");
    CHECK_FALSE (panel->findChildWithID ("voicingMatch")->isVisible());
    for (auto* child : panel->getChildren())
        if (child->isVisible())
            CHECK (panel->getLocalBounds().contains (child->getBounds()));
}

TEST_CASE ("Calibration reaches the plugin's detector", "[gain-tube][calibration][processor]")
{
    const auto meter = [] (float calibrationDb)
    {
        PluginProcessor processor;
        set (processor, "threshold", -30.0f);
        set (processor, "calibration", calibrationDb);
        processor.prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        float reduction = 0.0f;
        for (int block = 0; block < 100; ++block)
        {
            juce::AudioBuffer<float> audio (2, 512);
            for (int n = 0; n < 512; ++n)
                for (int ch = 0; ch < 2; ++ch)
                    audio.setSample (ch, n, 0.05f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (block * 512 + n) / 48000.0)));
            processor.processBlock (audio, midi);
            reduction = processor.getGainReductionDb();
        }
        return reduction;
    };
    CHECK (meter (12.0f) > meter (0.0f) + 3.0f); // About 2.2 -> 5.8 dB on this steady tone.
    CHECK (meter (-24.0f) < 0.01f);
}
