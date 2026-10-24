#include <PluginProcessor.h>
#include <dsp/AmpTube.h>
#include <dsp/GainTube.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <complex>

// Overdrive physics added after the owner's listening (docs/Overdrive.md): grid
// current, blocking, and the Release Mod parameter.

TEST_CASE ("Grid current follows the 5670 sheet and clamps a driven grid smoothly", "[overdrive][grid-current]")
{
    // Published dashed lines: 2.0 / 5.75 / 10 mA at +2 / +4 / +6 V.
    CHECK (ModuleSpike::gridCurrent (2.0).ig * 1000 == Catch::Approx (1.98).margin (0.15));
    CHECK (ModuleSpike::gridCurrent (4.0).ig * 1000 == Catch::Approx (5.6).margin (0.3));
    CHECK (ModuleSpike::gridCurrent (6.0).ig * 1000 == Catch::Approx (10.3).margin (0.5));
    CHECK (ModuleSpike::gridCurrent (-3.0).ig < 1.0e-9);
    // Below cut-off the grid follows its drive; driven positive it is held down,
    // monotonically and without a corner.
    using GainTube::Curve;
    CHECK (Curve::gridFor (-5.0) == Catch::Approx (-5.0).margin (1.0e-6));
    CHECK (Curve::gridFor (4.0) < 1.5);
    double previous = Curve::gridFor (-2.0), previousSlope = 1.0;
    for (double drive = -1.99; drive < 10.0; drive += 0.01)
    {
        const auto grid = Curve::gridFor (drive);
        const auto slope = (grid - previous) / 0.01;
        CHECK (slope > 0.0);
        CHECK (slope <= previousSlope + 1.0e-3); // Slope only falls: no corner, no kink upward.
        previous = grid; previousSlope = slope;
    }
}

TEST_CASE ("5751 grid network keeps unity gain, blocks after hot bursts and recovers", "[overdrive][blocking]")
{
    constexpr double rate = 192000.0;
    AmpTube::Grid grid;
    grid.prepare (rate);
    const auto& amp = AmpTube::stage();
    // Small signal at 1 kHz: unity through network and stage (the leak's divide is undone).
    std::complex<double> fundamental;
    for (int n = 0; n < static_cast<int> (rate); ++n)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * 1000.0 * n / rate;
        const auto y = amp.outputForGrid (grid.process (0.001 * std::sin (phase)));
        if (n >= rate / 2) fundamental += y * std::polar (1.0, -phase);
    }
    CHECK (20.0 * std::log10 (std::abs (fundamental) * 2.0 / (rate / 2) / 0.001) == Catch::Approx (0.0).margin (0.01));
    // A 200 ms burst hot enough to drive the grid past its cathode, then silence.
    grid.reset();
    double peakGrid = -1.0e9;
    for (int n = 0; n < static_cast<int> (0.2 * rate); ++n)
        peakGrid = std::max (peakGrid, grid.process (24.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * n / rate)));
    // Grid current held the peaks to about the cathode voltage, not the 2.3 V + swing.
    CHECK (peakGrid < amp.cathodeVolts() + 1.5);
    const auto blocked = grid.process (0.0);
    CHECK (blocked < -0.5); // The charged capacitor biases the stage colder.
    double later = blocked;
    for (int n = 0; n < static_cast<int> (0.5 * rate); ++n) later = grid.process (0.0);
    CHECK (later > -0.05); // About 100 ms time constant: recovered after 0.5 s.
    CHECK (later > blocked);
}

TEST_CASE ("Release Mod is appended with version hint 10 and defaults to 50%", "[overdrive][release][parameters]")
{
    PluginProcessor processor;
    juce::RangedAudioParameter* mod = nullptr;
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID == "releaseMod")
            mod = ranged;
    REQUIRE (mod != nullptr);
    CHECK (mod->getName (32) == "Release Mod");
    CHECK (mod->convertFrom0to1 (mod->getDefaultValue()) == Catch::Approx (50.0f));
    CHECK (mod->getVersionHint() == 10);
    CHECK (processor.getParameters().indexOf (mod) == processor.getParameters().size() - 2);
}

TEST_CASE ("Headroom sets every saturating stage's reference and keeps the level", "[overdrive][headroom]")
{
    // Default 14 dB: 0 dBu = -14 dBFS. More headroom, less saturation at the same
    // level; the level itself is compensated stage by stage.
    const auto render = [] (float headroomDb)
    {
        PluginProcessor processor;
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                if (ranged->paramID == "headroom") ranged->setValueNotifyingHost (ranged->convertTo0to1 (headroomDb));
                if (ranged->paramID == "threshold") ranged->setValueNotifyingHost (ranged->convertTo0to1 (0.0f));
            }
        processor.prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;
        std::complex<double> h1, h2, h3;
        constexpr int total = 48000 * 2;
        for (int start = 0; start < total; start += 512)
        {
            juce::AudioBuffer<float> audio (2, 512);
            for (int n = 0; n < 512; ++n)
                for (int ch = 0; ch < 2; ++ch)
                    audio.setSample (ch, n, 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * (start + n) / 48000.0)));
            processor.processBlock (audio, midi);
            for (int n = 0; n < 512; ++n)
                if (start + n >= 48000)
                {
                    const auto phase = 2.0 * juce::MathConstants<double>::pi * 100.0 * (start + n) / 48000.0;
                    const auto y = static_cast<double> (audio.getSample (0, n));
                    h1 += y * std::polar (1.0, -phase); h2 += y * std::polar (1.0, -2 * phase); h3 += y * std::polar (1.0, -3 * phase);
                }
        }
        return std::pair { 20.0 * std::log10 (std::abs (h1) * 2.0 / 48000.0), 20.0 * std::log10 (std::sqrt (std::norm (h2) + std::norm (h3)) / std::abs (h1)) };
    };
    PluginProcessor defaults;
    for (auto* p : defaults.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID == "headroom")
        {
            CHECK (ranged->convertFrom0to1 (ranged->getDefaultValue()) == Catch::Approx (14.0f));
            CHECK (ranged->getVersionHint() == 11);
            CHECK (defaults.getParameters().indexOf (ranged) == defaults.getParameters().size() - 1);
        }
    const auto [tightLevel, tightDistortion] = render (8.0f);
    const auto [defaultLevel, defaultDistortion] = render (14.0f);
    const auto [roomyLevel, roomyDistortion] = render (26.0f);
    CHECK (tightDistortion > defaultDistortion + 3.0); // About 5 dB for 6 dB less headroom here.
    CHECK (defaultDistortion > roomyDistortion + 3.0);
    CHECK (tightLevel == Catch::Approx (defaultLevel).margin (0.3));
    CHECK (roomyLevel == Catch::Approx (defaultLevel).margin (0.3));
}
