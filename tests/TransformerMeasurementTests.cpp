#include <PluginEditor.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <complex>

namespace
{
bool isFiniteMeasurement (double value)
{
    return (std::bit_cast<std::uint64_t> (value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}

juce::File favoriteFile()
{
    return juce::File (__FILE__).getParentDirectory().getParentDirectory()
        .getChildFile ("docs/voicings/tanh-favorite-v1.json");
}

void parameter (PluginProcessor& processor, const char* id, float value)
{
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID == id)
        {
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (value));
            return;
        }
    FAIL ("Missing parameter: " << id);
}
}

TEST_CASE ("Offline transformer measurements retain unity and mark out-of-band harmonics", "[measurement]")
{
    for (double frequency : { 50.0, 1000.0, 8000.0 })
    {
        CAPTURE (frequency);
        // Amount 0 is the minimum drive (-24 dB), not a bypass (owner decision).
        const auto minimum = TransformerMeasurement::measurePoint ({}, frequency, -24.0f, 0.0f);
        REQUIRE (minimum);
        CHECK (minimum->fundamentalGainDb == Catch::Approx (0.0).margin (0.1));
        CHECK (minimum->rmsGainDb == Catch::Approx (0.0).margin (0.1));
        for (std::size_t h = 0; h < 4; ++h)
            if (minimum->available[h])
                CHECK (minimum->harmonicDbc[h] < -40.0);
        const auto quiet = TransformerMeasurement::measurePoint ({}, frequency, -60.0f, 100.0f);
        REQUIRE (quiet);
        CHECK (quiet->fundamentalGainDb == Catch::Approx (0.0).margin (0.1));
        for (const auto value : quiet->harmonicDbc)
            CHECK (isFiniteMeasurement (value));
    }
    const auto nyquist = TransformerMeasurement::measurePoint ({}, 8000.0, -6.0f, 100.0f);
    REQUIRE (nyquist);
    CHECK ((nyquist->available == std::array<bool, 4> { true, false, false, false }));
    for (const auto voice : { TransformerVoicing::Values { -12.0f }, TransformerVoicing::Values { 12.0f } })
        for (double frequency : { 20.0, 8000.0 })
        {
            const auto extreme = TransformerMeasurement::measurePoint (voice, frequency, -6.0f, 100.0f);
            REQUIRE (extreme);
            CHECK (isFiniteMeasurement (extreme->fundamentalGainDb));
            CHECK (isFiniteMeasurement (extreme->rmsGainDb));
            for (const auto value : extreme->harmonicDbc)
                CHECK (isFiniteMeasurement (value));
        }
}

TEST_CASE ("VTB1148 descriptors reach offline measurements and captured CSV metadata", "[measurement][vtb-voicing]")
{
    // Even harmonics scales the hysteresis asymmetry; H2 follows it about 1:1 in dB.
    const TransformerVoicing::Values alternate { 0.0f, 12.0f, 0.0f, 100.0f };
    const auto nominal = 400.0f / 7.0f;
    const auto baseline = TransformerMeasurement::measurePoint ({}, 20.0, -24.0f, nominal);
    const auto changed = TransformerMeasurement::measurePoint (alternate, 20.0, -24.0f, nominal);
    REQUIRE (baseline);
    REQUIRE (changed);
    REQUIRE (baseline->available[0]);
    CHECK (isFiniteMeasurement (changed->harmonicDbc[0]));
    CHECK (changed->harmonicDbc[0] - baseline->harmonicDbc[0] == Catch::Approx (12.0).margin (3.0));
    CHECK (changed->fundamentalGainDb == Catch::Approx (baseline->fundamentalGainDb).margin (0.05));
    TransformerMeasurement::Result captured;
    captured.request.a = {};
    captured.request.b = alternate;
    captured.request.contextJson = "{}";
    const auto csv = TransformerMeasurement::toCsv (captured);
    for (const auto& d : TransformerVoicing::descriptors) CHECK (csv.contains (d.id));
    CHECK (csv.contains (TransformerVoicing::model));
}

TEST_CASE ("Streaming measurement matches an independent full-buffer Fourier analysis", "[measurement]")
{
    constexpr double frequency = 1000.0;
    constexpr float level = -12.0f;
    const TransformerVoicing::Values voice { -6.25f };
    const auto measured = TransformerMeasurement::measurePoint (voice, frequency, level, 100.0f);
    REQUIRE (measured);
    juce::AudioBuffer<float> audio (1, 60000);
    const auto amplitude = juce::Decibels::decibelsToGain (static_cast<double> (level));
    for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample (0, i, static_cast<float> (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / 48000.0)));
    const auto inputRms = audio.getRMSLevel (0, 12000, 48000);
    VtbOutputTransformer transformer;
    transformer.prepare (48000.0, 100.0f, false, voice);
    transformer.process (audio, 1);
    std::array<double, 5> magnitudes {};
    constexpr std::array orders { 1, 2, 3, 5, 7 };
    for (std::size_t h = 0; h < orders.size(); ++h)
    {
        std::complex<double> sum {};
        for (int i = 12000; i < 60000; ++i)
        {
            const auto phase = -2.0 * juce::MathConstants<double>::pi * frequency * orders[h] * i / 48000.0;
            sum += static_cast<double> (audio.getSample (0, i)) * std::complex<double> (std::cos (phase), std::sin (phase));
        }
        magnitudes[h] = 2.0 * std::abs (sum) / 48000.0;
    }
    CHECK (measured->fundamentalGainDb == Catch::Approx (juce::Decibels::gainToDecibels (magnitudes[0] / amplitude)).margin (1.0e-5));
    CHECK (measured->rmsGainDb == Catch::Approx (juce::Decibels::gainToDecibels (audio.getRMSLevel (0, 12000, 48000) / inputRms)).margin (1.0e-5));
    for (std::size_t h = 1; h < 4; ++h) // H2 is below the useful numerical floor for this symmetric curve.
        CHECK (measured->harmonicDbc[h] == Catch::Approx (juce::Decibels::gainToDecibels (magnitudes[h + 1] / magnitudes[0], -300.0)).margin (0.001));
}

TEST_CASE ("Offline sweep cancels promptly without publishing partial results", "[measurement][cancel]")
{
    int checks = 0;
    float lastProgress = 0.0f;
    const auto result = TransformerMeasurement::measure ({}, [&] { return ++checks > 250; },
        [&] (float progress) { CHECK (progress >= lastProgress); lastProgress = progress; });
    CHECK_FALSE (result);
    CHECK (checks == 251);
    CHECK (lastProgress > 0.0f);
    CHECK (lastProgress < 1.0f);
}

TEST_CASE ("Measurement view captures favorite A B without altering processor audio state or meters", "[measurement][editor][audio]")
{
    PluginProcessor processor, control;
    const auto favorite = favoriteFile().loadFileAsString();
    // A retired Magnetic favorite loads VTB1148 defaults; B is then voiced here.
    REQUIRE (processor.getTransformerVoicing().restoreJson (favorite));
    processor.getTransformerVoicing().selectB (true);
    processor.getTransformerVoicing().setCurrent ({ 0.0f, 0.0f, 0.0f, 50.0f });
    const auto context = juce::JSON::parse (favorite)["processorParameters"];
    REQUIRE (context.isObject());
    for (const auto& property : context.getDynamicObject()->getProperties())
        parameter (processor, property.name.toString().toRawUTF8(), static_cast<float> (property.value));
    juce::MemoryBlock original;
    processor.getStateInformation (original);
    control.setStateInformation (original.getData(), static_cast<int> (original.getSize()));
    for (auto* p : { &processor, &control })
        p->prepareToPlay (48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* panel = editor->findChildWithID ("transformerVoicingPanel");
    REQUIRE (panel);
    auto* tab = dynamic_cast<juce::Button*> (panel->findChildWithID ("measurementTab"));
    auto* view = dynamic_cast<TransformerMeasurementView*> (panel->findChildWithID ("transformerMeasurementView"));
    REQUIRE (tab);
    REQUIRE (view);
    tab->onClick();
    CHECK (view->isVisible());
    CHECK (panel->getLocalBounds().contains (view->getBounds()));
    auto* run = dynamic_cast<juce::Button*> (view->findChildWithID ("measurementRun"));
    REQUIRE (run);
    run->onClick();
    REQUIRE (view->isMeasuring());
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> a (2, 256), b (2, 256);
    auto identical = true;
    for (int block = 0; block < 64; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            const auto sample = static_cast<float> (0.7 * std::sin ((block * 256 + i) * 0.071));
            a.setSample (0, i, sample);
            a.setSample (1, i, -sample * 0.8f);
        }
        b.makeCopyOf (a, true);
        processor.processBlock (a, midi);
        control.processBlock (b, midi);
        for (int channel = 0; channel < 2; ++channel)
            identical &= std::memcmp (a.getReadPointer (channel), b.getReadPointer (channel), 256 * sizeof (float)) == 0;
    }
    CHECK (identical);
    CHECK (std::bit_cast<std::uint32_t> (processor.getGainReductionDb()) == std::bit_cast<std::uint32_t> (control.getGainReductionDb()));
    const auto p = processor.readLevelPeaks(), c = control.readLevelPeaks();
    CHECK (std::bit_cast<std::uint32_t> (p.inL) == std::bit_cast<std::uint32_t> (c.inL));
    CHECK (std::bit_cast<std::uint32_t> (p.inR) == std::bit_cast<std::uint32_t> (c.inR));
    CHECK (std::bit_cast<std::uint32_t> (p.outL) == std::bit_cast<std::uint32_t> (c.outL));
    CHECK (std::bit_cast<std::uint32_t> (p.outR) == std::bit_cast<std::uint32_t> (c.outR));
    juce::MemoryBlock after;
    processor.getStateInformation (after);
    CHECK (after == original);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 900000.0;
    while (view->isMeasuring() && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep (10);
    REQUIRE_FALSE (view->isMeasuring());
    view->refresh();
    const auto* result = view->getResult();
    REQUIRE (result);
    CHECK (result->request.a[TransformerVoicing::memory] == 100.0f);
    CHECK (result->request.b[TransformerVoicing::memory] == Catch::Approx (50.0f));
    CHECK (processor.getTransformerVoicing().read().legacyVoicingIgnored);
    const auto& point = result->at (4, 3, 2);
    CHECK (point.frequencyHz == 1000.0);
    CHECK (point.inputDbfs == Catch::Approx (-6.0f));
    CHECK (point.amountPercent == Catch::Approx (100.0f));
    CHECK (std::abs (point.voices[1].fundamentalGainDb - point.voices[0].fundamentalGainDb) > 1.0e-7);
    for (const auto& row : result->rows)
        for (const auto& metrics : row.voices)
        {
            REQUIRE (isFiniteMeasurement (metrics.fundamentalGainDb));
            REQUIRE (isFiniteMeasurement (metrics.rmsGainDb));
            for (const auto value : metrics.harmonicDbc)
                REQUIRE (isFiniteMeasurement (value));
        }
    const auto csv = TransformerMeasurement::toCsv (*result);
    CHECK (csv.contains ("B: onsetDb=0.00, evenHarmonicsDb=0.00, grainDb=0.00, memoryPercent=50"));
    CHECK (csv.contains ("B,48000,8000.000000,-6.000000,100.000000,"));
    CHECK (csv.contains (",NA,NA,NA"));
    if (const auto* output = std::getenv ("LUMIERE_MEASUREMENT_REPORT"))
        REQUIRE (juce::File (juce::String::fromUTF8 (output)).replaceWithText (csv));
    if (const auto* output = std::getenv ("LUMIERE_MEASUREMENT_PREVIEW"))
    {
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream stream { juce::File (juce::String::fromUTF8 (output)) };
        REQUIRE (stream.openedOk());
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
    }
    processor.getTransformerVoicing().setCurrent ({ 3.0f });
    view->refresh();
    auto stale = false;
    for (auto* child : view->getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
            stale |= label->getText().contains ("Voicing changed");
    CHECK (stale);
    run->onClick();
    run->onClick(); // Cancel a second capture; keep the completed first report.
    const auto cancelDeadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (view->isMeasuring() && juce::Time::getMillisecondCounterHiRes() < cancelDeadline)
        juce::Thread::sleep (1);
    CHECK_FALSE (view->isMeasuring());
    view->refresh();
    REQUIRE (view->getResult());
    CHECK (view->getResult()->request.b[TransformerVoicing::memory] == Catch::Approx (50.0f));
    run->onClick(); // Closing the editor cancels/joins the in-flight worker.
    editor.reset();
}
