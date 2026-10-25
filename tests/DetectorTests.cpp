#include <PluginProcessor.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <functional>

// The analog 12AL5 detector (owner decisions 2026-10-24, docs/Detector.md).
namespace
{
constexpr double rate = 192000.0;
juce::RangedAudioParameter* parameter (PluginProcessor& processor, const char* id)
{
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID == id)
            return ranged;
    return nullptr;
}
CompressorParameters::Values settings (float fbff = 0.0f)
{
    CompressorParameters::Values v;
    v.thresholdDb = -18.0f;
    v.fbffPercent = fbff;
    return v;
}
// A 1 kHz sine whose level (dBFS peak) follows levelAt (seconds) through the
// compressor alone at the internal rate; returns the gain reduction per sample.
std::vector<float> reductionTrace (bool analog, const CompressorParameters::Values& v, std::function<double (double)> levelAt, double seconds,
    double sidechainGainDb = 0.0, std::function<void (StereoCompressor&, int)> during = {})
{
    StereoCompressor compressor;
    compressor.prepare (rate, v);
    compressor.setDetector ({ analog, sidechainGainDb });
    std::vector<float> trace;
    juce::AudioBuffer<float> frame (2, 1);
    for (int n = 0; n < static_cast<int> (seconds * rate); ++n)
    {
        if (during) during (compressor, n);
        const auto t = n / rate;
        const auto x = static_cast<float> (std::pow (10.0, levelAt (t) / 20.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * t));
        frame.setSample (0, 0, x);
        frame.setSample (1, 0, x);
        compressor.process (frame, 2);
        trace.push_back (compressor.getGainReductionDb());
    }
    return trace;
}
double settled (bool analog, double levelDb, double sidechainGainDb = 0.0, float fbff = 0.0f, float calibrationDb = 0.0f)
{
    auto v = settings (fbff);
    v.calibrationDb = calibrationDb;
    return reductionTrace (analog, v, [levelDb] (double) { return levelDb; }, 1.0, sidechainGainDb).back();
}
// Samples after `start` until the trace crosses `fraction` of its value at the end
// (rising) or of its value at `start` (falling).
double millisecondsTo (const std::vector<float>& trace, double startSeconds, float fraction, bool rising)
{
    auto n = static_cast<std::size_t> (startSeconds * rate);
    const auto target = fraction * (rising ? trace.back() : trace[n]);
    while (n < trace.size() && (rising ? trace[n] < target : trace[n] > target)) ++n;
    return static_cast<double> (n - static_cast<std::size_t> (startSeconds * rate)) / rate * 1000.0;
}
}

TEST_CASE ("The 12AL5 law follows the Tung-Sol forward curve and self-biases", "[detector][fit]")
{
    // Digitized from the sheet's p2 forward curve (task detector-fit).
    const std::array<std::pair<double, double>, 6> forward { { { 0.75, 1.88 }, { 2.25, 8.13 }, { 3.75, 16.23 }, { 5.25, 25.72 }, { 7.25, 39.66 }, { 8.25, 46.88 } } };
    const auto law = ModuleSpike::tungSol12al5();
    for (const auto& [volts, milliamps] : forward)
        CHECK (ModuleSpike::diodeCurrent (law, volts).i * 1000.0 == Catch::Approx (milliamps).epsilon (0.03));
    // With no signal it charges a 1 MOhm load to about half a volt (contact
    // potential; the sheet's detector curves show about 0.6 V).
    double lo = 0, hi = 3;
    for (int i = 0; i < 60; ++i)
    {
        const auto u = 0.5 * (lo + hi);
        (ModuleSpike::diodeCurrent (law, -u).i > u / 1.0e6 ? lo : hi) = u;
    }
    CHECK (0.5 * (lo + hi) == Catch::Approx (0.52).margin (0.08));
    // Smooth and monotone through the onset.
    double previous = 0;
    for (double v = -2; v < 10; v += 0.01)
    {
        const auto d = ModuleSpike::diodeCurrent (law, v);
        CHECK (d.i >= previous);
        CHECK (d.slope >= 0.0);
        previous = d.i;
    }
}

TEST_CASE ("The 12AL5 static curve tracks the old 4:1 near threshold and steepens like the Manley", "[detector][curve]")
{
    // Feedback with the 5670, 0 dB sidechain gain, threshold -18 dBFS: within 0.3 dB of
    // the Digital law's feedforward 4:1 (FB/FF's old default) up to about 6 dB of GR.
    for (double level : { -15.0, -12.0, -9.0 })
    {
        const auto analog = settled (true, level), digital = settled (false, level, 0.0, 100.0f);
        CAPTURE (level, analog, digital);
        CHECK (analog == Catch::Approx (digital).margin (0.3));
    }
    // The ratio rises with gain reduction (the manual's "soft round knee").
    const auto ratio = [] (double level, double sidechainDb)
    {
        const auto a = settled (true, level, sidechainDb), b = settled (true, level + 3.0, sidechainDb);
        return 3.0 / (3.0 - (b - a)); // Input change over output change.
    };
    const auto gentle = ratio (-15.0, 0.0), steep = ratio (0.0, 0.0);
    CAPTURE (gentle, steep);
    CHECK (steep > 1.5 * gentle);
    // At -12 dB sidechain gain it is near the Manley's Compress curves: about 1.5:1
    // at 2 dB of GR and 2:1 at 5 dB.
    const auto atTwo = ratio (-15.0, -12.0), atFive = ratio (-9.0, -12.0);
    CAPTURE (atTwo, atFive, settled (true, -15.0, -12.0), settled (true, -9.0, -12.0));
    CHECK (atTwo == Catch::Approx (1.6).margin (0.3));
    CHECK (atFive == Catch::Approx (2.1).margin (0.4));
}

TEST_CASE ("12AL5 attack is the 63 percent time of a 10 dB step and depends on the hit", "[detector][ballistics]")
{
    for (float attack : { 1.0f, 5.0f, 30.0f })
    {
        auto v = settings();
        v.attackMs = attack;
        const auto step = [&v] (double overDb)
        {
            return millisecondsTo (reductionTrace (true, v, [overDb] (double t) { return t < 0.3 ? -40.0 : -18.0 + overDb; }, 0.6), 0.3, 0.632f, true);
        };
        const auto ten = step (10.0), small = step (3.0), big = step (20.0);
        CAPTURE (attack, ten, small, big);
        CHECK (ten == Catch::Approx (attack).epsilon (0.3));
        // A peak detector catches big hits faster and small ones slower.
        CHECK (big < 0.7 * ten);
        CHECK (small > 1.5 * ten);
    }
}

TEST_CASE ("Release Mod is a dual time constant: short hits recover faster than sustained compression", "[detector][release]")
{
    const auto recovery = [] (float mod, double burst)
    {
        auto v = settings();
        v.attackMs = 1.0f;
        v.releaseMs = 200.0f;
        v.releaseModPercent = mod;
        const auto trace = reductionTrace (true, v, [burst] (double t) { return t < 0.1 || t >= 0.1 + burst ? -60.0 : -6.0; }, 0.1 + burst + 1.0);
        return millisecondsTo (trace, 0.1 + burst, 0.368f, false);
    };
    // At 0% it is one RC: the same recovery whatever came before (161 ms measured:
    // GR is not linear in the control voltage).
    const auto shortPlain = recovery (0.0f, 0.03), longPlain = recovery (0.0f, 2.0);
    CAPTURE (shortPlain, longPlain);
    CHECK (shortPlain == Catch::Approx (longPlain).epsilon (0.02));
    CHECK (longPlain == Catch::Approx (160.0).epsilon (0.15));
    // At the default 50%: a 30 ms hit recovers much sooner than 2 s of compression.
    const auto shortHit = recovery (50.0f, 0.03), sustained = recovery (50.0f, 2.0);
    CAPTURE (shortHit, sustained);
    CHECK (shortHit < 0.8 * sustained);
    CHECK (sustained > 0.8 * longPlain);
}

TEST_CASE ("12AL5 Calibration is the sidechain amplifier's gain", "[detector][calibration]")
{
    // Feedforward: +6 dB of Calibration is the same as a 6 dB hotter input. Just over
    // the threshold: feedforward exhausts the 5670's control range about 8 dB over.
    for (double level : { -22.0, -20.0 })
    {
        const auto calibrated = settled (true, level, 0.0, 100.0f, 6.0f), hotter = settled (true, level + 6.0, 0.0, 100.0f);
        CAPTURE (level, calibrated, hotter);
        CHECK (calibrated > 1.0);
        CHECK (calibrated < 20.0);
        CHECK (calibrated == Catch::Approx (hotter).margin (0.05));
    }
}

TEST_CASE ("12AL5 stays finite and bounded, also over-compressing in feedforward", "[detector][finite]")
{
    // Feedforward with a sharp tube runs away toward the 60 dB floor on hot material
    // (real FF vari-mu behaviour, docs/Detector.md); it must stay finite and bounded.
    for (float fbff : { 0.0f, 100.0f })
        for (double sidechainDb : { -12.0, 12.0 })
        {
            auto v = settings (fbff);
            v.attackMs = 0.2f;
            v.releaseMs = 50.0f;
            v.releaseModPercent = 100.0f;
            const auto trace = reductionTrace (true, v, [] (double t) { return t < 0.2 ? 12.0 : -80.0; }, 0.6, sidechainDb);
            const auto bounded = std::all_of (trace.begin(), trace.end(), [] (float gr)
            {
                return ModuleSpike::isFinite (gr) && gr >= 0.0f && gr <= static_cast<float> (GainTube::Curve::maximumReductionDb) + 1.0e-3f;
            });
            CHECK (bounded);
            CHECK (trace.back() < 0.5f); // Recovered after the hit.
        }
    CHECK (settled (true, 6.0, 0.0, 100.0f) > 40.0);
}

TEST_CASE ("Switching detectors carries the gain reduction across", "[detector][smoothing]")
{
    // Sustained compression; at 0.5 s Digital switches to the 12AL5, at 1 s back.
    auto v = settings();
    const auto trace = reductionTrace (false, v, [] (double) { return -8.0; }, 1.5, 0.0, [] (StereoCompressor& c, int n)
    {
        if (n == static_cast<int> (0.5 * rate)) c.setDetector ({ true, 0.0 });
        if (n == static_cast<int> (1.0 * rate)) c.setDetector ({ false, 0.0 });
    });
    for (double at : { 0.5, 1.0 })
    {
        const auto n = static_cast<std::size_t> (at * rate);
        CAPTURE (at, trace[n - 1], trace[n]);
        CHECK (trace[n] == Catch::Approx (trace[n - 1]).margin (0.05));
    }
    float largest = 0;
    for (std::size_t n = static_cast<std::size_t> (0.2 * rate); n < trace.size(); ++n)
        largest = std::max (largest, std::abs (trace[n] - trace[n - 1]));
    CHECK (largest < 0.05f);
}

TEST_CASE ("Detector defaults to the 12AL5 with FB, round-trips, and older sessions keep the Digital law", "[detector][state][editor]")
{
    PluginProcessor processor;
    CHECK (DetectorVoicing::dspValue (processor.getDetectorVoicing().readAudio().voicing, DetectorVoicing::detector) < 0.5);
    CHECK (parameter (processor, "fbff")->convertFrom0to1 (parameter (processor, "fbff")->getDefaultValue()) == Catch::Approx (0.0f).margin (0.0));
    processor.getDetectorVoicing().setCurrent ({ 0.0f, -4.5f });
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    PluginProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (DetectorVoicing::equal (restored.getDetectorVoicing().readAudio().voicing, { 0.0f, -4.5f }));
    // A session from before the detector restores the Digital law: its FB/FF was
    // set for it.
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.removeChild (tree.getChildWithName ("DETECTOR_VOICING"), nullptr);
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), block);
    PluginProcessor older;
    older.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (DetectorVoicing::dspValue (older.getDetectorVoicing().readAudio().voicing, DetectorVoicing::detector) > 0.5);
    CHECK (juce::JSON::parse (processor.exportDetectorVoicing())["detector"].toString().isNotEmpty());
    // The editor's fifth stage.
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* stage = dynamic_cast<juce::Button*> (editor->findChildWithID ("voicingStageDetector"));
    auto* panel = editor->findChildWithID ("detectorVoicingPanel");
    REQUIRE (stage != nullptr);
    REQUIRE (panel != nullptr);
    stage->onClick();
    CHECK (panel->isVisible());
    auto* choice = dynamic_cast<juce::Slider*> (panel->findChildWithID ("voicing_detector"));
    REQUIRE (choice != nullptr);
    CHECK (choice->getTextFromValue (0.0) == "12AL5");
    CHECK (choice->getTextFromValue (1.0) == "Digital law");
    for (auto* child : panel->getChildren())
        if (child->isVisible())
            CHECK (panel->getLocalBounds().contains (child->getBounds()));
}
