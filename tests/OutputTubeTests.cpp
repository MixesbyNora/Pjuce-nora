#include <PluginProcessor.h>
#include <TransformerVoicingPanel.h>
#include <dsp/VtbOutputTransformer.h>
#include "modules/ShuntAnalysis.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <complex>

// The live output stage: 12BH7 cathode follower solved jointly with the VTB1148
// (docs/TubeStage.md, "Live output stage").
namespace
{
using Mode = VtbTransformerCore::Mode;
constexpr double internalRate = 192000.0;
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
VtbOutputStage stageFor (Mode mode, TubeVoicing::Values voicing = {})
{
    return { voicing, mode != Mode::transformer, mode != Mode::tube };
}
// A sine through one core at the internal rate; returns the left channel.
std::vector<double> run (Mode mode, double hz, double levelDb, double seconds, TubeVoicing::Values voicing = {}, double extraDriveDb = 0.0,
    VtbTransformerCore* inspect = nullptr)
{
    VtbTransformerCore local;
    auto& core = inspect != nullptr ? *inspect : local;
    core.prepare (internalRate, ModuleSpike::vtb1148OutputC(), extraDriveDb, {}, false, stageFor (mode, voicing));
    const auto amplitude = juce::Decibels::decibelsToGain (levelDb);
    std::vector<double> out;
    for (int n = 0; n < static_cast<int> (seconds * internalRate); ++n)
    {
        std::array<double, 2> frame;
        frame.fill (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / internalRate));
        core.processFrame (frame, 2);
        out.push_back (frame[0]);
    }
    return out;
}
// Harmonic amplitude over the last whole periods (window of `periods`).
double harmonic (const std::vector<double>& x, double hz, int order, int periods)
{
    const auto count = static_cast<std::size_t> (std::round (periods * internalRate / hz));
    std::complex<double> sum;
    for (auto n = x.size() - count; n < x.size(); ++n)
        sum += x[n] * std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * hz * order * static_cast<double> (n) / internalRate);
    return 2.0 * std::abs (sum) / static_cast<double> (count);
}
double gainDb (const std::vector<double>& a, const std::vector<double>& b, double hz)
{
    return 20.0 * std::log10 (harmonic (a, hz, 1, 20) / harmonic (b, hz, 1, 20));
}
bool allFinite (const std::vector<double>& x)
{
    return std::all_of (x.begin(), x.end(), [] (double v) { return ModuleSpike::isFinite (v); });
}
}

TEST_CASE ("Default tube voicing is the push-pull 12BH7 follower at 10 mA per section", "[tube][voicing]")
{
    // White cathode follower (docs/Overdrive.md): both sections at 10 mA with the
    // same plate-to-cathode voltage; 220 uF to the primary.
    const auto design = VtbTransformerCore::designFor ({});
    REQUIRE (design.pushPull);
    const auto output = ModuleSpike::CathodeFollower<ModuleSpike::ResistiveLoad>::quiescentCathode (design);
    const auto upper = ModuleSpike::triodeCurrent (design.tube, design.quiescentPlate - output, design.bias - output);
    const auto lower = ModuleSpike::triodeCurrent (design.tube, output - design.lowerCathode, -design.lowerCathode);
    CHECK (upper.ia == Catch::Approx (0.010).epsilon (1.0e-4));
    CHECK (lower.ia == Catch::Approx (0.010).epsilon (1.0e-4));
    CHECK (design.quiescentPlate - output == Catch::Approx (output - design.lowerCathode).margin (0.01));
    CHECK (design.quiescentPlate == Catch::Approx (285.0));
    CHECK (design.couplingFarads == Catch::Approx (220.0e-6));
    // Single-ended wiring keeps the original design: cathode at half the supply.
    TubeVoicing::Values single;
    single[TubeVoicing::wiring] = 1.0f;
    const auto plain = VtbTransformerCore::designFor (single);
    CHECK_FALSE (plain.pushPull);
    const auto cathode = ModuleSpike::CathodeFollower<ModuleSpike::ResistiveLoad>::quiescentCathode (plain);
    CHECK (cathode == Catch::Approx (150.0).margin (0.01));
    CHECK (ModuleSpike::triodeCurrent (plain.tube, plain.supply - cathode, plain.bias - cathode).ia == Catch::Approx (0.010).epsilon (1.0e-4));
    CHECK (plain.cathodeOhms == Catch::Approx (7500.0));
    CHECK (plain.sections == Catch::Approx (2.0));
}

TEST_CASE ("Push-pull follower saturates the iron far more gracefully than single-ended", "[tube][overdrive]")
{
    // Both Amounts at 100% (+18 dB) and 100 Hz at -6 dBFS, the owner's "Amounts all
    // the way" case (docs/Overdrive.md): the single-ended follower cuts off on the
    // iron's magnetizing current; the push-pull one keeps driving it.
    const auto distortion = [] (float wiring)
    {
        TubeVoicing::Values voicing;
        voicing[TubeVoicing::wiring] = wiring;
        VtbTransformerCore core;
        auto stage = stageFor (Mode::tubeAndTransformer, voicing);
        stage.tubeExtraDriveDb = 18.0;
        core.prepare (internalRate, ModuleSpike::vtb1148OutputC(), 18.0, {}, false, stage);
        std::vector<double> out;
        for (int n = 0; n < static_cast<int> (internalRate); ++n)
        {
            std::array<double, 2> frame;
            frame.fill (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * n / internalRate));
            core.processFrame (frame, 2);
            out.push_back (frame[0]);
        }
        CHECK (core.getTubeDiagnostics (0).failures == 0);
        double sum = 0;
        for (int h = 2; h <= 30; ++h) sum += std::pow (harmonic (out, 100.0, h, 20), 2);
        return 100.0 * std::sqrt (sum) / harmonic (out, 100.0, 1, 20);
    };
    const auto pushPull = distortion (0.0f), single = distortion (1.0f);
    CAPTURE (pushPull, single);
    CHECK (pushPull < 0.5 * single);
}

TEST_CASE ("Tube in or out keeps the output stage's 1 kHz level", "[tube][audio]")
{
    const auto transformer = run (Mode::transformer, 1000.0, -18.0, 0.2);
    const auto joint = run (Mode::tubeAndTransformer, 1000.0, -18.0, 0.2);
    const auto tubeOnly = run (Mode::tube, 1000.0, -18.0, 0.2);
    std::vector<double> input;
    for (int n = 0; n < static_cast<int> (0.2 * internalRate); ++n)
        input.push_back (juce::Decibels::decibelsToGain (-18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * n / internalRate));
    CHECK (std::abs (gainDb (joint, transformer, 1000.0)) < 0.1);
    CHECK (std::abs (gainDb (tubeOnly, input, 1000.0)) < 0.1);
    CHECK (joint != transformer);
}

TEST_CASE ("The tube's source resistance raises the iron's bass distortion", "[tube][audio]")
{
    const auto transformer = run (Mode::transformer, 20.0, -6.0, 1.0);
    const auto joint = run (Mode::tubeAndTransformer, 20.0, -6.0, 1.0);
    const auto h3 = [] (const std::vector<double>& x) { return 20.0 * std::log10 (harmonic (x, 20.0, 3, 4) / harmonic (x, 20.0, 1, 4)); };
    // Measured in the coupling test: about +18 to +21 dB at low and moderate levels.
    CHECK (h3 (joint) > h3 (transformer) + 10.0);
}

TEST_CASE ("Every tube control reaches the audio and extremes solve finitely when driven hard", "[tube][voicing][audio]")
{
    const auto reference = run (Mode::tubeAndTransformer, 30.0, 0.0, 0.25, {}, 12.0);
    for (std::size_t i = 0; i < TubeVoicing::Schema::descriptors.size(); ++i)
    {
        const auto& d = TubeVoicing::Schema::descriptors[i];
        TubeVoicing::Values voicing;
        voicing[i] = static_cast<float> (std::abs (d.minimum - d.defaultValue) > std::abs (d.maximum - d.defaultValue) ? d.minimum : d.maximum);
        if (i == TubeVoicing::sections) voicing[TubeVoicing::wiring] = 1.0f; // Sections apply to single-ended wiring.
        CAPTURE (d.id);
        const auto changed = run (Mode::tubeAndTransformer, 30.0, 0.0, 0.25, voicing, 12.0);
        double difference = 0;
        for (std::size_t n = 0; n < changed.size(); ++n) difference = std::max (difference, std::abs (changed[n] - reference[n]));
        CHECK (difference > 1.0e-3);
    }
    for (const auto& voicing : { TubeVoicing::Values { 3.0f, 22.0f, 200.0f, 1.0f, 0.0f }, TubeVoicing::Values { 20.0f, 1000.0f, 400.0f, 2.0f, 0.0f },
             TubeVoicing::Values { 3.0f, 22.0f, 200.0f, 1.0f, 1.0f }, TubeVoicing::Values { 20.0f, 1000.0f, 400.0f, 2.0f, 1.0f } })
        for (auto mode : { Mode::tubeAndTransformer, Mode::tube })
        {
            VtbTransformerCore core;
            const auto out = run (mode, 30.0, 0.0, 0.25, voicing, 18.0, &core);
            CHECK (allFinite (out));
            CHECK (core.getTubeDiagnostics (0).failures == 0);
            CHECK (core.getDiagnostics (0).failures == 0);
        }
}

TEST_CASE ("Tube voicing and switching crossfade through the warmed bank without a step", "[tube][smoothing]")
{
    VtbTransformerCore core;
    core.prepare (internalRate, ModuleSpike::vtb1148OutputC(), 0.0, {}, false, stageFor (Mode::tubeAndTransformer));
    float largest = 0, inputStep = 0;
    double previous = 0, previousInput = 0;
    for (int n = 0; n < static_cast<int> (1.2 * internalRate); ++n)
    {
        if (n == static_cast<int> (0.2 * internalRate)) core.setTube (stageFor (Mode::transformer));
        if (n == static_cast<int> (0.5 * internalRate)) core.setTube (stageFor (Mode::tubeAndTransformer, { 3.0f, 22.0f, 200.0f, 1.0f }));
        if (n == static_cast<int> (0.8 * internalRate)) core.setTube (stageFor (Mode::tube));
        const auto x = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * n / internalRate);
        std::array<double, 2> frame { x, x };
        core.processFrame (frame, 2);
        if (n > 2000)
        {
            largest = std::max (largest, static_cast<float> (std::abs (frame[0] - previous)));
            inputStep = std::max (inputStep, static_cast<float> (std::abs (x - previousInput)));
        }
        previous = frame[0]; previousInput = x;
    }
    CHECK (core.getAppliedMode() == Mode::tube);
    CHECK (largest < 1.5f * inputStep);
}

TEST_CASE ("Output Tube and Tube Amount are appended and the tube workbench round-trips through state", "[tube][parameters][state]")
{
    PluginProcessor processor;
    auto* tube = dynamic_cast<juce::AudioParameterBool*> (parameter (processor, "outputTubeOut"));
    REQUIRE (tube != nullptr);
    CHECK (tube->getName (32) == "Output Tube");
    CHECK (parameter (processor, "outputTubeOut")->getText (0.0f, 32) == "In");
    CHECK_FALSE (tube->get());
    CHECK (processor.getParameters().indexOf (tube) == processor.getParameters().size() - 7);
    CHECK (tube->getVersionHint() == 6);
    auto* amount = parameter (processor, "tubeAmount");
    REQUIRE (amount != nullptr);
    CHECK (amount->getName (32) == "Tube Amount");
    CHECK (processor.getParameters().indexOf (amount) == processor.getParameters().size() - 6);
    CHECK (amount->getVersionHint() == 7);
    // Both Amounts default to the calibrated 0 dB drive (57.1% on the 0.1 grid).
    for (const auto* id : { "tubeAmount", "xfrmAmount" })
        CHECK (VtbOutputTransformer::driveDbFor (parameter (processor, id)->convertFrom0to1 (parameter (processor, id)->getDefaultValue())) == Catch::Approx (0.0).margin (0.02));
    processor.getTubeVoicing().setCurrent ({ 6.5f, 47.0f, 250.0f, 1.0f });
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    PluginProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (TubeVoicing::equal (restored.getTubeVoicing().readAudio().voicing, { 6.5f, 47.0f, 250.0f, 1.0f }));
    // Sessions from before the output tube restore its defaults.
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.removeChild (tree.getChildWithName ("TUBE_VOICING"), nullptr);
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), block);
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (TubeVoicing::equal (restored.getTubeVoicing().readAudio().voicing, {}));
    const auto exported = juce::JSON::parse (processor.exportTubeVoicing());
    CHECK (exported["tube"].toString() == "12BH7 cathode follower");
    CHECK (exported["model"].toString() == TubeVoicing::Schema::model);
}

TEST_CASE ("Output tube workbench is a third stage with one slider per control", "[tube][editor]")
{
    PluginProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* stage = dynamic_cast<juce::Button*> (editor->findChildWithID ("voicingStageTube"));
    auto* panel = editor->findChildWithID ("tubeVoicingPanel");
    auto* outputPanel = editor->findChildWithID ("transformerVoicingPanel");
    REQUIRE (stage != nullptr);
    REQUIRE (panel != nullptr);
    CHECK_FALSE (panel->isVisible());
    stage->onClick();
    CHECK (panel->isVisible());
    CHECK_FALSE (outputPanel->isVisible());
    CHECK (panel->getBounds() == outputPanel->getBounds());
    for (std::size_t i = 0; i < TubeVoicing::Schema::descriptors.size(); ++i)
    {
        const auto& d = TubeVoicing::Schema::descriptors[i];
        auto* slider = dynamic_cast<juce::Slider*> (panel->findChildWithID ("voicing_" + juce::String (d.id)));
        REQUIRE (slider != nullptr);
        slider->setValue (d.minimum, juce::sendNotificationSync);
        CHECK (processor.getTubeVoicing().readAudio().voicing[i] == Catch::Approx (d.minimum));
    }
    for (auto* child : panel->getChildren())
        if (child->isVisible())
            CHECK (panel->getLocalBounds().contains (child->getBounds()));
}

TEST_CASE ("Toggling Output Tube and Output XFRM in the plugin ramps without a step", "[tube][smoothing][processor]")
{
    PluginProcessor processor;
    set (processor, "threshold", 0.0f);
    set (processor, "xfrmAmount", 57.1f);
    processor.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> dry (2, 256 * 96);
    for (int i = 0; i < dry.getNumSamples(); ++i)
        for (int channel = 0; channel < 2; ++channel)
            dry.setSample (channel, i, static_cast<float> (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * i / 48000.0)));
    auto audio = dry;
    juce::MidiBuffer midi;
    for (int block = 0; block < 96; ++block)
    {
        if (block == 20) set (processor, "outputTubeOut", 1.0f);
        if (block == 40) set (processor, "outputTubeOut", 0.0f);
        if (block == 60) set (processor, "xfrmOut", 1.0f);
        if (block == 75) set (processor, "outputTubeOut", 1.0f); // Both out: the stage ramps to bypass.
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, block * 256, 256);
        processor.processBlock (view, midi);
    }
    float largestStep = 0.0f, inputStep = 0.0f;
    for (int i = 1024; i < audio.getNumSamples(); ++i)
    {
        REQUIRE (ModuleSpike::isFinite (audio.getSample (0, i)));
        largestStep = std::max (largestStep, std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)));
        inputStep = std::max (inputStep, std::abs (dry.getSample (0, i) - dry.getSample (0, i - 1)));
    }
    CHECK (largestStep < 1.5f * inputStep);
}

TEST_CASE ("Tube Amount drives the tube alone and keeps the level", "[tube][amount]")
{
    // Tube Amount scales the tube's signal level relative to the iron's; the
    // output is compensated, so only the tube's nonlinearity changes.
    const auto render = [] (Mode mode, double hz, double levelDb, double tubeDb)
    {
        VtbTransformerCore core;
        auto stage = stageFor (mode);
        stage.tubeExtraDriveDb = tubeDb;
        core.prepare (internalRate, ModuleSpike::vtb1148OutputC(), 0.0, {}, false, stage);
        const auto amplitude = juce::Decibels::decibelsToGain (levelDb);
        std::vector<double> out;
        for (int n = 0; n < static_cast<int> (0.5 * internalRate); ++n)
        {
            std::array<double, 2> frame;
            frame.fill (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / internalRate));
            core.processFrame (frame, 2);
            out.push_back (frame[0]);
        }
        CHECK (core.getTubeDiagnostics (0).failures == 0);
        return out;
    };
    const auto nominal = render (Mode::tube, 1000.0, -18.0, 0.0);
    for (double tubeDb : { -24.0, 18.0 })
        CHECK (std::abs (gainDb (render (Mode::tube, 1000.0, -18.0, tubeDb), nominal, 1000.0)) < 0.1);
    // Driven hard, the tube alone distorts much more at +18 dB than at 0 dB.
    const auto thd = [] (const std::vector<double>& x)
    {
        double sum = 0;
        for (int h = 2; h <= 7; ++h) sum += std::pow (harmonic (x, 100.0, h, 20), 2);
        return 10.0 * std::log10 (sum) - 20.0 * std::log10 (harmonic (x, 100.0, 1, 20));
    };
    CHECK (thd (render (Mode::tube, 100.0, 0.0, 18.0)) > thd (render (Mode::tube, 100.0, 0.0, 0.0)) + 10.0);
    // With the iron in, the iron's drive is unchanged: Tube Amount still changes the result.
    const auto joint = render (Mode::tubeAndTransformer, 100.0, 0.0, 0.0), hot = render (Mode::tubeAndTransformer, 100.0, 0.0, 18.0);
    double difference = 0;
    for (std::size_t n = 0; n < joint.size(); ++n) difference = std::max (difference, std::abs (hot[n] - joint[n]));
    CHECK (difference > 1.0e-3);
}

TEST_CASE ("Tube Amount sweeps in the plugin ramp without a step", "[tube][amount][smoothing][processor]")
{
    // Also with the capacitor-coupled output grid: the charge it builds at 100%
    // must not reappear larger when the Amount comes back down (docs/Overdrive.md).
    const auto grid = GENERATE (0.0f, 2.0f);
    CAPTURE (grid);
    PluginProcessor processor;
    set (processor, "threshold", 0.0f);
    TubeVoicing::Values voicing;
    voicing[TubeVoicing::outputGrid] = grid;
    processor.getTubeVoicing().setCurrent (voicing);
    processor.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> dry (2, 256 * 64);
    for (int i = 0; i < dry.getNumSamples(); ++i)
        for (int channel = 0; channel < 2; ++channel)
            dry.setSample (channel, i, static_cast<float> (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * i / 48000.0)));
    auto audio = dry;
    juce::MidiBuffer midi;
    for (int block = 0; block < 64; ++block)
    {
        if (block == 20) set (processor, "tubeAmount", 100.0f);
        if (block == 40) set (processor, "tubeAmount", 0.0f);
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), 2, block * 256, 256);
        processor.processBlock (view, midi);
    }
    float largestStep = 0.0f, inputStep = 0.0f;
    bool finite = true;
    for (int i = 1024; i < audio.getNumSamples(); ++i)
    {
        finite &= ModuleSpike::isFinite (audio.getSample (0, i));
        largestStep = std::max (largestStep, std::abs (audio.getSample (0, i) - audio.getSample (0, i - 1)));
        inputStep = std::max (inputStep, std::abs (dry.getSample (0, i) - dry.getSample (0, i - 1)));
    }
    CHECK (finite);
    CHECK (largestStep < 1.5f * inputStep);
}

TEST_CASE ("The tabulated 12BH7 matches Koren's law and falls back to it outside its range", "[tube][cpu]")
{
    // The per-sample solve's table (docs/Overdrive.md, "CPU").
    const auto& table = ModuleSpike::rca12bh7aTable();
    const auto tube = ModuleSpike::rca12bh7a();
    double worstCurrent = 0, worstSlope = 0;
    for (double va = 1.01; va < 450; va += 0.37)
        for (double vg = -60; vg < 15; vg += 0.13)
        {
            const auto exact = ModuleSpike::triodeCurrent (tube, va, vg), fast = table (va, vg);
            if (exact.ia < 1.0e-6) continue;
            worstCurrent = std::max (worstCurrent, std::abs (fast.ia / exact.ia - 1));
            worstSlope = std::max ({ worstSlope, std::abs (fast.dVa / exact.dVa - 1), std::abs (fast.dVg / exact.dVg - 1) });
        }
    CAPTURE (worstCurrent, worstSlope);
    CHECK (worstCurrent < 1.0e-8);
    CHECK (worstSlope < 1.0e-6);
    // Below 1 V on the plate (and past the grid table) it is the law itself.
    for (const auto [va, vg] : { std::pair { 0.5, -1.0 }, std::pair { 0.0, 2.0 }, std::pair { 20.0, 60.0 } })
    {
        const auto exact = ModuleSpike::triodeCurrent (tube, va, vg), fast = table (va, vg);
        CHECK (std::bit_cast<std::uint64_t> (fast.ia) == std::bit_cast<std::uint64_t> (exact.ia));
        CHECK (std::bit_cast<std::uint64_t> (fast.dVa) == std::bit_cast<std::uint64_t> (exact.dVa));
        CHECK (std::bit_cast<std::uint64_t> (fast.dVg) == std::bit_cast<std::uint64_t> (exact.dVg));
    }
}

TEST_CASE ("Hot drums through the push-pull follower keep a bounded solver cost", "[tube][overdrive][cpu]")
{
    // Owner report 2026-10-24: pushed hard, loud hits spiked the CPU and glitched.
    // The drum fixture at Makeup +24's level into the output stage (about 14x full
    // scale), with each Output grid choice: every sample solves, the damped joint
    // Newton almost never falls back, and the worst sample stays a bounded handful
    // of evaluations (docs/Overdrive.md, "CPU").
    const auto grid = GENERATE (0.0f, 1.0f, 2.0f);
    CAPTURE (grid);
    TubeVoicing::Values voicing;
    voicing[TubeVoicing::outputGrid] = grid;
    VtbTransformerCore core;
    core.prepare (internalRate, ModuleSpike::vtb1148OutputC(), 0.0, {}, false, stageFor (Mode::tubeAndTransformer, voicing));
    for (int n = 0; n < static_cast<int> (internalRate); ++n)
    {
        std::array<double, 2> frame;
        for (int ch = 0; ch < 2; ++ch) frame[static_cast<std::size_t> (ch)] = 14.0 * ShuntResearch::material (1, ch, n / 4.0);
        core.processFrame (frame, 2);
    }
    const auto& d = core.getTubeDiagnostics (0);
    const auto perSample = static_cast<double> (d.evaluations) / static_cast<double> (d.samples);
    const auto fallbacks = static_cast<double> (d.fallbacks) / static_cast<double> (d.samples);
    CAPTURE (perSample, fallbacks, d.maximumIterations);
    CHECK (d.failures == 0);
    CHECK (perSample < 3.0);   // 2.4-2.5 measured (3.7 before the damped joint Newton)
    CHECK (fallbacks < 0.001); // under 0.02% measured (2% before)
    CHECK (d.maximumIterations <= 40); // 31-33 measured; was 43 with bisection alone
}

TEST_CASE ("The fast output-stage solve sounds the same as the exact one", "[tube][cpu]")
{
    // The tabulated 12BH7 and the first-order last Newton step against the exact
    // law with every root confirmed, on bass and drums from nominal to Makeup +24's
    // level, with the ideal and the capacitor-coupled grid.
    const auto p = ModuleSpike::vtb1148OutputC();
    const auto unit = ModuleSpike::transformerUnitVolts (p);
    for (float grid : { 0.0f, 2.0f })
        for (double level : { 0.5, 14.0 })
            for (int type : { 0, 1 })
            {
                TubeVoicing::Values voicing;
                voicing[TubeVoicing::outputGrid] = grid;
                const auto fastDesign = VtbTransformerCore::designFor (voicing);
                auto exactDesign = fastDesign;
                exactDesign.table = nullptr;
                exactDesign.strictSolve = true;
                ModuleSpike::CathodeFollower<ModuleSpike::MagneticLoad> fast, exact;
                fast.prepare (internalRate, fastDesign, p);
                exact.prepare (internalRate, exactDesign, p);
                double peak = 0, error = 0;
                for (int n = 0; n < static_cast<int> (internalRate / 2); ++n)
                {
                    const auto x = level * ShuntResearch::material (type, 0, n / 4.0) * unit;
                    const auto a = fast.process (x), b = exact.process (x);
                    peak = std::max (peak, std::abs (b));
                    error = std::max (error, std::abs (a - b));
                }
                CAPTURE (grid, level, type, peak, error);
                CHECK (fast.diagnostics.failures + exact.diagnostics.failures == 0);
                CHECK (error < 1.0e-8 * peak); // -160 dB; about -190 dB measured
                CHECK (fast.diagnostics.evaluations < exact.diagnostics.evaluations);
            }
}
