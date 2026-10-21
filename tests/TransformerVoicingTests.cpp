#include <PluginEditor.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <fstream>
#include <iomanip>
#include <thread>

namespace
{
void set (PluginProcessor& processor, const char* id, float value)
{
    for (auto* p : processor.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (p); parameter != nullptr && parameter->paramID == id)
        {
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            return;
        }
    FAIL ("Missing APVTS parameter " << id);
}

bool finite (const juce::AudioBuffer<float>& audio)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            if ((std::bit_cast<std::uint32_t> (audio.getSample (channel, i)) & 0x7f800000U) == 0x7f800000U)
                return false;
    return true;
}

juce::AudioBuffer<float> tone (double rate, double frequency, float levelDb, int samples)
{
    juce::AudioBuffer<float> audio (2, samples);
    const auto amplitude = juce::Decibels::decibelsToGain (static_cast<double> (levelDb));
    for (int i = 0; i < samples; ++i)
    {
        const auto value = static_cast<float> (amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / rate));
        audio.setSample (0, i, value);
        audio.setSample (1, i, -value);
    }
    return audio;
}

}

TEST_CASE ("Private voicing snapshots export recall reset and reject incompatible data", "[voicing][state]")
{
    TransformerVoicingState state;
    const TransformerVoicing::Values altered { 6.5f, 18.0f, 240.0f };
    CHECK (TransformerVoicing::equal (state.readAudio().voicing, {}));
    CHECK_FALSE (state.readAudio().levelMatch);
    state.setCurrent (altered);
    state.selectB (false);
    CHECK (TransformerVoicing::equal (state.readAudio().voicing, {}));
    state.selectB (true);
    CHECK (TransformerVoicing::equal (state.readAudio().voicing, altered));
    state.copyToOther();
    CHECK (TransformerVoicing::equal (state.read().a, altered));
    state.setReference (true);
    state.setLevelMatch (true);
    CHECK (TransformerVoicing::equal (state.readAudio().voicing, {}));
    TransformerVoicingState restored;
    REQUIRE (restored.restoreJson (state.toJson()));
    CHECK (restored.toJson() == state.toJson());
    restored.setReference (false);
    CHECK (TransformerVoicing::equal (restored.readAudio().voicing, altered));
    const auto saved = restored.toJson();
    CHECK_FALSE (restored.restoreJson ("{}"));
    CHECK_FALSE (restored.restoreJson (saved.replace (TransformerVoicing::model, "future-model")));
    auto incomplete = juce::JSON::parse (saved);
    incomplete["A"].getDynamicObject()->setProperty ("cornerHz", juce::var());
    CHECK_FALSE (restored.restoreJson (juce::JSON::toString (incomplete)));
    CHECK (restored.toJson() == saved);
    restored.resetCurrent();
    CHECK (TransformerVoicing::equal (restored.readAudio().voicing, {}));
    CHECK_FALSE (restored.readAudio().levelMatch);
    restored.selectB (false);
    CHECK (TransformerVoicing::equal (restored.readAudio().voicing, altered));
}

TEST_CASE ("Packed voicing publication never exposes a torn snapshot", "[voicing][threading]")
{
    TransformerVoicingState state;
    const TransformerVoicing::Values a { -12.0f, 0.0f, 30.0f }, b { 12.0f, 24.0f, 500.0f };
    std::atomic<bool> done { false };
    std::thread writer ([&]
    {
        for (int i = 0; i < 20000; ++i)
            state.setCurrent (i % 2 == 0 ? a : b);
        done.store (true);
    });
    auto valid = true;
    do
    {
        const auto values = state.readAudio().voicing;
        valid &= TransformerVoicing::equal (values, {}) || TransformerVoicing::equal (values, a) || TransformerVoicing::equal (values, b);
    } while (! done.load());
    writer.join();
    CHECK (valid);
}

TEST_CASE ("Plugin state retains private voicing without adding or reordering host parameters", "[voicing][processor][state]")
{
    PluginProcessor processor;
    set (processor, "xfrmAmount", 75.0f);
    processor.getTransformerVoicing().setCurrent ({ 6.5f, 18.0f, 240.0f });
    processor.getTransformerVoicing().setLevelMatch (true);
    const auto feedback = juce::JSON::parse (processor.exportTransformerVoicing());
    CHECK (static_cast<float> (feedback["processorParameters"]["xfrmAmount"]) == Catch::Approx (75.0f));
    TransformerVoicingState pasted;
    REQUIRE (pasted.restoreJson (processor.exportTransformerVoicing()));
    CHECK (pasted.toJson() == processor.getTransformerVoicing().toJson());
    juce::MemoryBlock block;
    processor.getStateInformation (block);
    PluginProcessor restored;
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (restored.getTransformerVoicing().toJson() == processor.getTransformerVoicing().toJson());
    REQUIRE (restored.getParameters().size() == 16);
    for (int i = 0; i < processor.getParameters().size(); ++i)
    {
        const auto* original = dynamic_cast<juce::RangedAudioParameter*> (processor.getParameters()[i]);
        const auto* copy = dynamic_cast<juce::RangedAudioParameter*> (restored.getParameters()[i]);
        REQUIRE (original != nullptr);
        REQUIRE (copy != nullptr);
        CHECK (copy->paramID == original->paramID);
        CHECK (copy->getValue() == Catch::Approx (original->getValue()));
    }
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    REQUIRE (xml != nullptr);
    auto tree = juce::ValueTree::fromXml (*xml);
    tree.removeChild (tree.getChildWithName ("TRANSFORMER_VOICING"), nullptr);
    juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), block);
    restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (TransformerVoicing::equal (restored.getTransformerVoicing().readAudio().voicing, {}));
    CHECK_FALSE (restored.getTransformerVoicing().readAudio().levelMatch);
}

TEST_CASE ("Voicing panel controls real processor state and collapses without disturbing generic controls", "[voicing][editor]")
{
    PluginProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* panel = dynamic_cast<TransformerVoicingPanel*> (editor->findChildWithID ("transformerVoicingPanel"));
    REQUIRE (panel != nullptr);
    auto* slider = dynamic_cast<juce::Slider*> (panel->findChildWithID ("voicingDrive"));
    REQUIRE (slider != nullptr);
    slider->setValue (6.5, juce::sendNotificationSync);
    CHECK (processor.getTransformerVoicing().readAudio().voicing.driveCalibrationDb == Catch::Approx (6.5f));
    auto* selectA = dynamic_cast<juce::Button*> (panel->findChildWithID ("voicingA"));
    REQUIRE (selectA != nullptr);
    selectA->onClick();
    CHECK (slider->getValue() == Catch::Approx (0.0).margin (1.0e-10));
    auto* selectB = dynamic_cast<juce::Button*> (panel->findChildWithID ("voicingB"));
    REQUIRE (selectB != nullptr);
    selectB->onClick();
    CHECK (slider->getValue() == Catch::Approx (6.5));
    auto* reset = dynamic_cast<juce::Button*> (panel->findChildWithID ("voicingReset"));
    REQUIRE (reset != nullptr);
    reset->onClick();
    CHECK (slider->getValue() == Catch::Approx (0.0).margin (1.0e-10));
    for (auto* child : editor->getChildren())
        if (child != panel && child->getComponentID() != "voicingToggle")
            CHECK_FALSE (child->getBounds().intersects (panel->getBounds()));
    auto* toggle = dynamic_cast<juce::Button*> (editor->findChildWithID ("voicingToggle"));
    REQUIRE (toggle != nullptr);
    toggle->onClick();
    CHECK_FALSE (panel->isVisible());
    CHECK (editor->getWidth() == 640);
    toggle->onClick();
    CHECK (panel->isVisible());
    CHECK (editor->getWidth() == 960);
}

TEST_CASE ("Voicing changes reach the transformer while detection meters and bypass stay intact", "[voicing][audio]")
{
    PluginProcessor reference, voiced;
    for (auto* processor : { &reference, &voiced })
    {
        set (*processor, "xfrmAmount", 100.0f);
        set (*processor, "threshold", -30.0f);
        set (*processor, "makeup", 12.0f);
    }
    voiced.getTransformerVoicing().setCurrent ({ 6.0f, 18.0f, 240.0f });
    for (auto* processor : { &reference, &voiced })
        processor->prepareToPlay (48000.0, 256);
    const auto dry = tone (48000.0, 73.0, -6.0f, 8192);
    auto a = dry, b = dry;
    juce::MidiBuffer midi;
    reference.processBlock (a, midi);
    voiced.processBlock (b, midi);
    CHECK (voiced.getGainReductionDb() == Catch::Approx (reference.getGainReductionDb()));
    CHECK (std::memcmp (a.getReadPointer (0), b.getReadPointer (0), 8192 * sizeof (float)) != 0);
    const auto levels = voiced.readLevelPeaks();
    CHECK (levels.inL == Catch::Approx (dry.getMagnitude (0, 0, 8192)));
    CHECK (levels.outL == Catch::Approx (b.getMagnitude (0, 0, 8192)));
    set (voiced, "xfrmAmount", 0.0f);
    set (reference, "xfrmAmount", 0.0f);
    voiced.getTransformerVoicing().setLevelMatch (true);
    a = dry; b = dry;
    reference.processBlock (a, midi);
    voiced.processBlock (b, midi);
    CHECK (std::memcmp (a.getReadPointer (0, 2048), b.getReadPointer (0, 2048), 6144 * sizeof (float)) == 0);
}

TEST_CASE ("Voicing changes and level-match toggles are smooth finite and bounded", "[voicing][smoothing]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (bool useMatch : { false, true })
        {
            CAPTURE (rate, useMatch);
            TransformerSaturation stage;
            stage.prepare (rate, 100.0f, false);
            juce::AudioBuffer<float> frame (2, 1);
            float controlStep = 0.0f, previousOutput = 0.0f, previousInput = 0.0f;
            auto valid = true;
            for (int i = 0; i < static_cast<int> (rate * 0.4); ++i)
            {
                const auto input = static_cast<float> (0.95 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / rate));
                frame.setSample (0, 0, input);
                frame.setSample (1, 0, -input);
                const auto changed = i % 256 == 0;
                if (changed)
                {
                    stage.setVoicing ((i / 256) % 2 == 0 ? TransformerVoicing::Values { 6.0f, 18.0f, 240.0f }
                        : TransformerVoicing::Values { -6.0f, 6.0f, 60.0f }, useMatch && (i / 256) % 3 == 0);
                }
                stage.process (frame, 2);
                const auto output = frame.getSample (0, 0);
                controlStep = juce::jmax (controlStep, std::abs (output - previousOutput) - std::abs (input - previousInput));
                previousOutput = output; previousInput = input;
                valid &= finite (frame);
            }
            CHECK (valid);
            CHECK (controlStep < 0.05f);
            // On DC every change is a control/filter transient, rather than a
            // steeper harmonic waveform. Cover complete fades and queued edits,
            // including the samples where an old transition finishes.
            TransformerSaturation dc;
            dc.prepare (rate, 100.0f, false);
            juce::AudioBuffer<float> settled (2, static_cast<int> (rate));
            for (int channel = 0; channel < 2; ++channel)
                juce::FloatVectorOperations::fill (settled.getWritePointer (channel), 0.95f, settled.getNumSamples());
            dc.process (settled, 2);
            auto previous = settled.getSample (0, settled.getNumSamples() - 1);
            float maximumDcStep = 0.0f;
            for (int i = 0; i < static_cast<int> (rate * 0.4); ++i)
            {
                if (i % 256 == 0)
                    dc.setVoicing ((i / 256) % 2 == 0 ? TransformerVoicing::Values { 6.0f, 18.0f, 240.0f }
                        : TransformerVoicing::Values { -6.0f, 6.0f, 60.0f }, useMatch && (i / 256) % 3 == 0);
                frame.setSample (0, 0, 0.95f);
                frame.setSample (1, 0, 0.95f);
                dc.process (frame, 2);
                maximumDcStep = juce::jmax (maximumDcStep, std::abs (frame.getSample (0, 0) - previous));
                previous = frame.getSample (0, 0);
            }
            CHECK (maximumDcStep < 0.05f);
            for (int size : { 1, 64, 4096 })
            {
                auto loud = tone (rate, 50.0, 24.0f, size);
                stage.setVoicing ({ 12.0f, 24.0f, 30.0f }, true);
                stage.process (loud, 2);
                CHECK (finite (loud));
            }
        }
}

TEST_CASE ("Level matching converges for audition and skips exact transformer bypass", "[voicing][level-match]")
{
    const auto original = tone (48000.0, 1000.0, -12.0f, 144000);
    auto matched = original;
    TransformerSaturation stage;
    stage.prepare (48000.0, 100.0f, false, {}, true);
    stage.process (matched, 2);
    const auto gain = matched.getRMSLevel (0, 96000, 48000) / original.getRMSLevel (0, 96000, 48000);
    CHECK (juce::Decibels::gainToDecibels (gain) == Catch::Approx (0.0f).margin (0.1f));
    stage.setParameters (100.0f, true);
    matched = original;
    stage.process (matched, 2);
    const auto latency = stage.getLatencySamples();
    CHECK (std::memcmp (matched.getReadPointer (0, 480), original.getReadPointer (0, 480 - latency), 143520 * sizeof (float)) == 0);
}

TEST_CASE ("Export repeatable transformer gain and harmonic report", "[voicing-report]")
{
    const auto* outputPath = std::getenv ("LUMIERE_VOICING_REPORT");
    if (outputPath == nullptr)
    {
        SUCCEED ("Set LUMIERE_VOICING_REPORT to generate CSV; optional LUMIERE_VOICING_SETTINGS selects exported JSON.");
        return;
    }
    TransformerVoicingState settings;
    if (const auto* inputPath = std::getenv ("LUMIERE_VOICING_SETTINGS"))
    {
        const auto file = juce::File (juce::String::fromUTF8 (inputPath));
        REQUIRE (settings.restoreJson (file.loadFileAsString()));
    }
    const auto voice = settings.readAudio().voicing;
    std::ofstream report (outputPath);
    REQUIRE (report.good());
    report << "# " << settings.toJson() << '\n';
    report << "# Current magnetic model; level matching disabled. Not VTB1148 measurement data.\n";
    report << "frequency_hz,input_dbfs,amount_percent,fundamental_gain_db,rms_gain_db,H2_dBc,H3_dBc,H5_dBc,H7_dBc\n";
    report << std::fixed << std::setprecision (6);
    for (double frequency : { 20.0, 50.0, 60.0, 200.0, 1000.0, 5000.0, 8000.0 })
        for (float level : { -60.0f, -24.0f, -12.0f, -6.0f })
            for (float amount : { 25.0f, 50.0f, 100.0f })
            {
                const auto metrics = TransformerMeasurement::measurePoint (voice, frequency, level, amount);
                REQUIRE (metrics.has_value());
                report << frequency << ',' << level << ',' << amount << ','
                       << metrics->fundamentalGainDb << ',' << metrics->rmsGainDb;
                for (std::size_t n = 0; n < TransformerMeasurement::harmonics.size(); ++n)
                {
                    if (! metrics->available[n])
                        report << ",NA";
                    else
                        report << ',' << metrics->harmonicDbc[n];
                }
                report << '\n';
            }
    REQUIRE (report.good());
}
