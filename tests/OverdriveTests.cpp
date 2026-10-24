#include <PluginProcessor.h>
#include <dsp/AmpTube.h>
#include <dsp/GainTube.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <complex>
#include <functional>

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

// ---- Output grid current (owner decision 2026-10-24: a workbench choice, Ideal by default) ----
namespace
{
// A 100 Hz sine through the output stage at both Amounts' +18 dB; returns the left
// channel after `settle` seconds and the core's diagnostics.
struct OutputRun { std::vector<double> out; ModuleSpike::StageDiagnostics diagnostics; };
OutputRun runOutputStage (float grid, double amplitude, double settle = 3.0, double seconds = 1.0)
{
    constexpr double rate = 192000.0;
    TubeVoicing::Values voicing;
    voicing[TubeVoicing::outputGrid] = grid;
    VtbTransformerCore core;
    core.prepare (rate, ModuleSpike::vtb1148OutputC(), 18.0, {}, false, { voicing, true, true, 18.0 });
    OutputRun run;
    for (int n = 0; n < static_cast<int> ((settle + seconds) * rate); ++n)
    {
        std::array<double, 2> frame;
        frame.fill (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * n / rate));
        core.processFrame (frame, 2);
        if (n >= static_cast<int> (settle * rate)) run.out.push_back (frame[0]);
    }
    run.diagnostics = core.getTubeDiagnostics (0);
    return run;
}
double harmonicAt (const std::vector<double>& x, double hz)
{
    std::complex<double> sum;
    for (std::size_t n = 0; n < x.size(); ++n)
        sum += x[n] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * hz * static_cast<double> (n) / 192000.0);
    return std::abs (sum);
}
}

TEST_CASE ("Output grid clamp solves the source law exactly and smoothly", "[overdrive][grid-current]")
{
    const ModuleSpike::ResistiveGrid grid (35.0e3, 1.0);
    // Far below the cathode it is the drive itself.
    const auto off = grid (-12.0);
    CHECK (off.vgk == Catch::Approx (-12.0).margin (0.0));
    CHECK (off.ig == Catch::Approx (0.0).margin (0.0));
    double previous = grid (-9.99).vgk;
    for (double d = -9.98; d < 4000.0; d += (d < 80 ? 0.0137 : 1.37))
    {
        const auto p = grid (d);
        const auto law = ModuleSpike::gridCurrent (p.vgk);
        CHECK (std::abs (p.ig - law.ig) <= 1.0e-9 * law.ig + 1.0e-15); // (d - w) / R = Ig(w)
        CHECK (p.slope == Catch::Approx (1 / (1 + 35.0e3 * law.slope)).epsilon (1.0e-5));
        CHECK (p.vgk > previous); // Monotone.
        previous = p.vgk;
    }
    // Driven 40 V above the cathode through 35 kOhm the grid holds near +1.3 V.
    CHECK (grid (40.0).vgk == Catch::Approx (1.3).margin (0.1));
}

TEST_CASE ("Output grid choices leave the stage alone until it is overdriven", "[overdrive][grid-current][tube]")
{
    // Ideal is the default and today's sound.
    CHECK (TubeVoicing::Values {}[TubeVoicing::outputGrid] == Catch::Approx (0.0f).margin (0.0));
    CHECK (VtbTransformerCore::designFor ({}).gridClamp == nullptr);
    // At the clean point (both Amounts +18 dB, the sine 18 dB below full scale)
    // neither network draws grid current or changes the 100 Hz level and THD.
    const auto ideal = runOutputStage (0.0f, 0.125, 1.0, 0.5);
    for (float grid : { 1.0f, 2.0f })
    {
        const auto run = runOutputStage (grid, 0.125, 1.0, 0.5);
        CAPTURE (grid, run.diagnostics.maximumGridMa);
        CHECK (run.diagnostics.maximumGridMa < 1.0e-6);
        CHECK (20 * std::log10 (harmonicAt (run.out, 100) / harmonicAt (ideal.out, 100)) == Catch::Approx (0.0).margin (0.02));
        CHECK (harmonicAt (run.out, 300) == Catch::Approx (harmonicAt (ideal.out, 300)).epsilon (0.02));
    }
    // Pushed 12 dB harder the grid conducts. It limits the current the upper
    // section can push into the saturated iron, so the stage clips harder: more
    // high harmonics (measured: H11+ -39 dBc ideal, about -30 dBc with either
    // network). The capacitor-coupled grid settles to a steady, periodic state
    // (blocking, no squegging): the last two cycles of the window match.
    const auto hot = runOutputStage (0.0f, 1.0, 1.0, 0.5);
    for (float grid : { 1.0f, 2.0f })
    {
        const auto run = runOutputStage (grid, 1.0);
        CAPTURE (grid, run.diagnostics.maximumGridMa);
        CHECK (run.diagnostics.maximumGridMa > 1.0);
        CHECK (run.diagnostics.failures == 0);
        const auto high = [] (const std::vector<double>& x)
        {
            double sum = 0;
            for (int h = 11; h <= 60; ++h) sum += std::pow (harmonicAt (x, 100.0 * h), 2);
            return 10 * std::log10 (sum / std::pow (harmonicAt (x, 100.0), 2));
        };
        const auto peak = [] (const std::vector<double>& x) { double p = 0; for (auto v : x) p = std::max (p, std::abs (v)); return p; };
        CAPTURE (high (run.out), high (hot.out));
        CHECK (high (run.out) > high (hot.out) + 5.0);
        double drift = 0;
        const auto cycle = static_cast<std::size_t> (1920), end = run.out.size();
        for (std::size_t n = end - cycle; n < end; ++n) drift = std::max (drift, std::abs (run.out[n] - run.out[n - cycle]));
        CHECK (drift < 1.0e-3 * peak (run.out));
    }
}

TEST_CASE ("A capacitor-coupled output grid blocks after hot hits and recovers", "[overdrive][grid-current][blocking][tube]")
{
    // A hot 100 Hz burst, then a quiet 1 kHz probe. The capacitor's charge biases the
    // upper section colder, so right after the burst the probe's level is shifted and
    // returns to the DC-coupled stage's within about half a second.
    const auto probeLevel = [] (float grid)
    {
        constexpr double rate = 192000.0;
        TubeVoicing::Values voicing;
        voicing[TubeVoicing::outputGrid] = grid;
        VtbTransformerCore core;
        core.prepare (rate, ModuleSpike::vtb1148OutputC(), 18.0, {}, false, { voicing, true, true, 18.0 });
        std::vector<double> early, late;
        for (int n = 0; n < static_cast<int> (1.3 * rate); ++n)
        {
            const auto t = n / rate;
            std::array<double, 2> frame;
            frame.fill (t < 0.5 ? 1.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * t)
                                : 0.06 * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
            core.processFrame (frame, 2);
            if (t >= 0.52 && t < 0.57) early.push_back (frame[0]);
            if (t >= 1.25) late.push_back (frame[0]);
        }
        const auto rms = [] (const std::vector<double>& x) { double s = 0; for (auto v : x) s += v * v; return std::sqrt (s / static_cast<double> (x.size())); };
        return std::pair { rms (early), rms (late) };
    };
    const auto dc = probeLevel (1.0f), capacitor = probeLevel (2.0f);
    CAPTURE (dc.first, dc.second, capacitor.first, capacitor.second);
    // Recovered: both match after the network's time constant.
    CHECK (capacitor.second == Catch::Approx (dc.second).epsilon (0.01));
    // Right after the burst the capacitor-coupled stage still differs.
    CHECK (std::abs (capacitor.first / dc.first - 1) > 2 * std::abs (capacitor.second / dc.second - 1));
}

TEST_CASE ("Tube workbench version 2 loads with Output grid at Ideal", "[overdrive][grid-current][state]")
{
    TubeVoicingState state;
    state.setCurrent ({ 6.5f, 47.0f, 250.0f, 1.0f, 1.0f, 2.0f });
    auto saved = juce::JSON::parse (state.toJson());
    CHECK (state.readAudio().voicing[TubeVoicing::outputGrid] == Catch::Approx (2.0f).margin (0.0));
    // Rewrite as a version 2 save: no Output grid.
    saved.getDynamicObject()->setProperty ("version", 2);
    for (const auto* slot : { "A", "B" }) saved[slot].getDynamicObject()->removeProperty ("outputGrid");
    TubeVoicingState restored;
    REQUIRE (restored.restoreJson (juce::JSON::toString (saved)));
    const auto values = restored.readAudio().voicing;
    CHECK (TubeVoicing::equal (values, { 6.5f, 47.0f, 250.0f, 1.0f, 1.0f, 0.0f }));
    // Version 1 (before Wiring) still loads nothing; a version 3 save missing the
    // field is malformed.
    saved.getDynamicObject()->setProperty ("version", 1);
    CHECK_FALSE (restored.restoreJson (juce::JSON::toString (saved)));
    saved.getDynamicObject()->setProperty ("version", 3);
    CHECK_FALSE (restored.restoreJson (juce::JSON::toString (saved)));
}

TEST_CASE ("Automating Headroom ramps every stage without a step", "[overdrive][headroom][smoothing][processor]")
{
    // Headroom used to reach the gain tube and the 5751 through the gain element's
    // settings, whose changes crossfade one at a time: automation moved in 20 ms
    // stages. Now it ramps like Makeup (docs/Overdrive.md). A 110 Hz sine with gain
    // reduction, Headroom swept 6 -> 30 -> 6 dB a little every block: no sample step
    // may exceed what either end produces when held.
    const auto render = [] (std::function<float (int)> headroomAt)
    {
        PluginProcessor processor;
        const auto set = [&processor] (const char* id, float value)
        {
            for (auto* p : processor.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID == id)
                    ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
        };
        set ("threshold", -20.0f);
        set ("headroom", headroomAt (0));
        processor.prepareToPlay (48000.0, 128);
        juce::MidiBuffer midi;
        std::vector<float> out;
        for (int block = 0; block < 600; ++block)
        {
            set ("headroom", headroomAt (block));
            juce::AudioBuffer<float> audio (2, 128);
            for (int n = 0; n < 128; ++n)
                for (int ch = 0; ch < 2; ++ch)
                    audio.setSample (ch, n, 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * (block * 128 + n) / 48000.0)));
            processor.processBlock (audio, midi);
            for (int n = 0; n < 128; ++n) out.push_back (audio.getSample (0, n));
        }
        float largest = 0.0f;
        bool finite = true;
        for (std::size_t i = 48000 / 2; i < out.size(); ++i)
        {
            finite &= ModuleSpike::isFinite (out[i]);
            largest = std::max (largest, std::abs (out[i] - out[i - 1]));
        }
        CHECK (finite);
        return largest;
    };
    const auto low = render ([] (int) { return 6.0f; }), high = render ([] (int) { return 30.0f; });
    const auto swept = render ([] (int block)
    {
        // Hold for the first 0.5 s, then 6 -> 30 dB over 200 blocks (0.53 s) and back.
        const auto phase = std::clamp (block - 188, 0, 400);
        return 6.0f + 24.0f * static_cast<float> (phase < 200 ? phase : 400 - phase) / 200.0f;
    });
    CAPTURE (low, high, swept);
    CHECK (swept < 1.1f * std::max (low, high));
}

TEST_CASE ("A small Headroom move keeps the 5751's blocking state", "[overdrive][headroom][blocking]")
{
    // A full-scale 100 Hz sine at Makeup +24: the 5751's grid conducts and its
    // capacitor charges (blocking). Moving Headroom 0.1 dB must change the sound by
    // about 0.1 dB worth. Before, a Headroom change crossfaded to a fresh gain
    // element whose capacitor started discharged; forcing such a crossfade here
    // (any element change does it) shows that difference.
    const auto run = [] (int change)
    {
        StereoCompressor compressor;
        CompressorParameters::Values values;
        values.thresholdDb = 0.0f;
        values.makeupDb = 24.0f;
        compressor.setGainElement ({ false, {}, true });
        compressor.prepare (192000.0, values);
        std::vector<float> out;
        for (int start = 0; start < 115200; start += 1024)
        {
            if (start == 96256 && change == 1) compressor.setHeadroomDriveDb (0.1);
            if (start == 96256 && change == 2) compressor.setGainElement ({ false, { GainTube::Tube::ge5670, 1.0e-3, 0.0 }, true });
            juce::AudioBuffer<float> audio (2, 1024);
            for (int n = 0; n < 1024; ++n)
                for (int ch = 0; ch < 2; ++ch)
                    audio.setSample (ch, n, static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * (start + n) / 192000.0)));
            compressor.process (audio, 2);
            for (int n = 0; n < 1024; ++n) out.push_back (audio.getSample (0, n));
        }
        return out;
    };
    const auto held = run (0), moved = run (1), fresh = run (2);
    float peak = 0.0f, movedError = 0.0f, freshError = 0.0f;
    for (std::size_t n = 96256; n < held.size(); ++n)
    {
        peak = std::max (peak, std::abs (held[n]));
        movedError = std::max (movedError, std::abs (moved[n] - held[n]));
        freshError = std::max (freshError, std::abs (fresh[n] - held[n]));
    }
    CAPTURE (peak, movedError, freshError);
    CHECK (movedError < 0.02f * peak);
    CHECK (movedError < 0.25f * freshError);
}
