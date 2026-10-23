#include <PluginEditor.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <fstream>
#include <iomanip>
#include <thread>

namespace
{
bool sameFloat (float a, float b) { return std::bit_cast<std::uint32_t> (a) == std::bit_cast<std::uint32_t> (b); }
bool sameDouble (double a, double b) { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
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
    const TransformerVoicing::Values altered { 6.5f };
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
    incomplete["A"].getDynamicObject()->setProperty ("onsetDb", juce::var());
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
    const TransformerVoicing::Values a { -12.0f, 16.0f, 1.0f, 30.0f }, b { 12.0f, 0.0f, 0.0f, 0.5f };
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
    processor.getTransformerVoicing().setCurrent ({ 6.5f });
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
    auto* slider = dynamic_cast<juce::Slider*> (panel->findChildWithID ("voicing_onsetDb"));
    REQUIRE (slider != nullptr);
    slider->setValue (6.5, juce::sendNotificationSync);
    CHECK (processor.getTransformerVoicing().readAudio().voicing[TransformerVoicing::onset] == Catch::Approx (6.5f));
    auto* selectA = dynamic_cast<juce::Button*> (panel->findChildWithID ("voicingA"));
    REQUIRE (selectA != nullptr);
    selectA->onClick();
    CHECK (slider->getValue() == Catch::Approx (0.0).margin (1.0e-10));
    for (std::size_t i = 1; i < TransformerVoicing::descriptors.size(); ++i)
    {
        const auto& descriptor = TransformerVoicing::descriptors[i];
        auto* control = dynamic_cast<juce::Slider*> (panel->findChildWithID ("voicing_" + juce::String (descriptor.id)));
        REQUIRE (control);
        control->setValue (descriptor.maximum, juce::sendNotificationSync);
        CHECK (processor.getTransformerVoicing().readAudio().voicing[i] == Catch::Approx (descriptor.maximum));
    }
    for (auto* child : panel->getChildren())
        if (child->isVisible())
        {
            CHECK (child->getHeight() > 0);
            CHECK (panel->getLocalBounds().contains (child->getBounds()));
        }
    auto* selectB = dynamic_cast<juce::Button*> (panel->findChildWithID ("voicingB"));
    REQUIRE (selectB != nullptr);
    selectB->onClick();
    CHECK (slider->getValue() == Catch::Approx (6.5));
    auto* reset = dynamic_cast<juce::Button*> (panel->findChildWithID ("voicingReset"));
    REQUIRE (reset != nullptr);
    reset->onClick();
    CHECK (slider->getValue() == Catch::Approx (0.0).margin (1.0e-10));
    for (auto* child : editor->getChildren())
        if (child != panel && child->isVisible() && child->getComponentID() != "voicingToggle")
            CHECK_FALSE (child->getBounds().intersects (panel->getBounds()));
    // The stage switch shows the input transformer's own workbench in the same place.
    auto* inputStage = dynamic_cast<juce::Button*> (editor->findChildWithID ("voicingStageInput"));
    auto* inputPanel = editor->findChildWithID ("inputTransformerVoicingPanel");
    REQUIRE (inputStage != nullptr);
    REQUIRE (inputPanel != nullptr);
    CHECK_FALSE (inputPanel->isVisible());
    inputStage->onClick();
    CHECK (inputPanel->isVisible());
    CHECK_FALSE (panel->isVisible());
    CHECK (inputPanel->getBounds() == panel->getBounds());
    dynamic_cast<juce::Button*> (editor->findChildWithID ("voicingStageOutput"))->onClick();
    CHECK (panel->isVisible());
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
    voiced.getTransformerVoicing().setCurrent ({ 6.0f });
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
            VtbOutputTransformer stage;
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
                    stage.setVoicing ((i / 256) % 2 == 0 ? TransformerVoicing::Values { 6.0f }
                        : TransformerVoicing::Values { -6.0f }, useMatch && (i / 256) % 3 == 0);
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
            VtbOutputTransformer dc;
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
                    dc.setVoicing ((i / 256) % 2 == 0 ? TransformerVoicing::Values { 6.0f }
                        : TransformerVoicing::Values { -6.0f }, useMatch && (i / 256) % 3 == 0);
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
                stage.setVoicing ({ 12.0f }, true);
                stage.process (loud, 2);
                CHECK (finite (loud));
            }
        }
}

TEST_CASE ("Level matching converges for audition and skips exact transformer bypass", "[voicing][level-match]")
{
    const auto original = tone (48000.0, 1000.0, -12.0f, 144000);
    auto matched = original;
    VtbOutputTransformer stage;
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
    report << "# VTB1148 (variant C) output stage; level matching disabled. Model output, not hardware measurement.\n";
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

TEST_CASE ("Workbench descriptors define quantization defaults ranges and serialized controls", "[voicing][state]")
{
    TransformerVoicingState state;
    const auto exported = juce::JSON::parse (state.toJson());
    CHECK (static_cast<int> (exported["version"]) == TransformerVoicing::version);
    CHECK (exported["model"].toString() == TransformerVoicing::model);
    REQUIRE (exported["A"].getDynamicObject());
    CHECK (exported["A"].getDynamicObject()->getProperties().size() == static_cast<int> (TransformerVoicing::descriptors.size()));
    for (std::size_t i = 0; i < TransformerVoicing::descriptors.size(); ++i)
    {
        const auto& descriptor = TransformerVoicing::descriptors[i];
        CAPTURE (descriptor.id);
        CHECK (sameFloat (state.read().a[i], static_cast<float> (descriptor.defaultValue)));
        for (auto value : { descriptor.minimum, descriptor.maximum, descriptor.defaultValue })
        {
            TransformerVoicing::Values settings;
            settings[i] = static_cast<float> (value);
            const auto result = TransformerVoicing::sanitized (settings);
            CHECK (result[i] == Catch::Approx (value).margin (1.0e-6));
        }
        TransformerVoicing::Values settings;
        settings[i] = static_cast<float> (descriptor.minimum - 100.0);
        CHECK (sameFloat (TransformerVoicing::sanitized (settings)[i], static_cast<float> (descriptor.minimum)));
        settings[i] = static_cast<float> (descriptor.maximum + 100.0);
        CHECK (sameFloat (TransformerVoicing::sanitized (settings)[i], static_cast<float> (descriptor.maximum)));
        for (auto bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
        {
            settings[i] = bad;
            CHECK (sameFloat (TransformerVoicing::sanitized (settings)[i], static_cast<float> (descriptor.defaultValue)));
        }
        settings[i] = static_cast<float> (descriptor.minimum + 1.3 * descriptor.step);
        CHECK (TransformerVoicing::sanitized (settings)[i] == Catch::Approx (descriptor.minimum + descriptor.step));
    }
    CHECK (std::bit_cast<std::uint32_t> (state.readAudio().voicing[0]) == std::bit_cast<std::uint32_t> (0.0f));
}

TEST_CASE ("Retired Magnetic workbench files load VTB1148 defaults with a visible notice", "[voicing][state][editor]")
{
    const auto favorite = juce::File (__FILE__).getParentDirectory().getParentDirectory()
        .getChildFile ("docs/voicings/tanh-favorite-v1.json").loadFileAsString();
    REQUIRE (favorite.isNotEmpty());
    for (int version = 1; version <= 4; ++version)
    {
        CAPTURE (version);
        auto legacy = juce::JSON::parse (favorite);
        legacy.getDynamicObject()->setProperty ("version", version);
        legacy.getDynamicObject()->setProperty ("model", TransformerVoicing::retiredModels[static_cast<std::size_t> (version - 1)]);
        PluginProcessor processor;
        processor.getTransformerVoicing().setCurrent ({ 3.0f });
        REQUIRE (processor.getTransformerVoicing().restoreJson (juce::JSON::toString (legacy)));
        const auto migrated = processor.getTransformerVoicing().read();
        CHECK (migrated.legacyVoicingIgnored);
        CHECK (TransformerVoicing::equal (migrated.a, {}));
        CHECK (TransformerVoicing::equal (migrated.b, {}));
        CHECK_FALSE (migrated.reference);
        CHECK_FALSE (migrated.levelMatch);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto* panel = editor->findChildWithID ("transformerVoicingPanel");
        REQUIRE (panel);
        auto* status = dynamic_cast<juce::Label*> (panel->findChildWithID ("voicingStatus"));
        REQUIRE (status);
        CHECK (status->getText().contains ("not applied"));
        for (const auto& descriptor : TransformerVoicing::descriptors)
        {
            auto* slider = dynamic_cast<juce::Slider*> (panel->findChildWithID ("voicing_" + juce::String (descriptor.id)));
            REQUIRE (slider);
            CHECK (sameDouble (slider->getMinimum(), descriptor.minimum));
            CHECK (sameDouble (slider->getMaximum(), descriptor.maximum));
            CHECK (sameDouble (slider->getInterval(), descriptor.step));
        }
        // Project state carrying a retired workbench restores the same way.
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
        REQUIRE (xml);
        auto tree = juce::ValueTree::fromXml (*xml);
        tree.getChildWithName ("TRANSFORMER_VOICING").setProperty ("json", juce::JSON::toString (legacy), nullptr);
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), saved);
        PluginProcessor restored;
        restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        CHECK (restored.getTransformerVoicing().toJson() == processor.getTransformerVoicing().toJson());
        // An unknown future workbench is rejected and leaves the current state.
        const auto unchanged = restored.getTransformerVoicing().toJson();
        auto future = juce::JSON::parse (unchanged);
        future.getDynamicObject()->setProperty ("version", TransformerVoicing::version + 1);
        CHECK_FALSE (restored.getTransformerVoicing().restoreJson (juce::JSON::toString (future)));
        CHECK (restored.getTransformerVoicing().toJson() == unchanged);
    }
}

TEST_CASE ("Workbench publication keeps both slots and flags in one coherent snapshot", "[voicing][threading]")
{
    TransformerVoicingState state;
    auto first = juce::JSON::parse (state.toJson()), second = juce::JSON::parse (state.toJson());
    for (auto* value : { &first, &second })
    {
        const auto useFirst = value == &first;
        for (const auto& d : TransformerVoicing::descriptors)
        {
            (*value)["A"].getDynamicObject()->setProperty (d.id, useFirst ? d.minimum : d.maximum);
            (*value)["B"].getDynamicObject()->setProperty (d.id, useFirst ? d.maximum : d.minimum);
        }
        value->getDynamicObject()->setProperty ("selected", useFirst ? "A" : "B");
        value->getDynamicObject()->setProperty ("reference", useFirst);
        value->getDynamicObject()->setProperty ("levelMatch", ! useFirst);
        value->getDynamicObject()->setProperty ("legacyVoicingIgnored", useFirst);
    }
    const auto a = juce::JSON::toString (first), b = juce::JSON::toString (second);
    REQUIRE (state.restoreJson (a));
    const auto firstSnapshot = state.read();
    REQUIRE (state.restoreJson (b));
    const auto secondSnapshot = state.read();
    std::atomic<bool> done { false }, importsValid { true };
    std::thread writer ([&]
    {
        for (int i = 0; i < 20000; ++i)
            if (! state.restoreJson (i % 2 == 0 ? a : b)) importsValid.store (false);
        done.store (true);
    });
    auto valid = true;
    do
    {
        const auto snapshot = state.read();
        const auto isFirst = TransformerVoicing::equal (snapshot.a, firstSnapshot.a) && TransformerVoicing::equal (snapshot.b, firstSnapshot.b) && ! snapshot.useB
            && snapshot.reference && ! snapshot.levelMatch && snapshot.legacyVoicingIgnored;
        const auto isSecond = TransformerVoicing::equal (snapshot.a, secondSnapshot.a) && TransformerVoicing::equal (snapshot.b, secondSnapshot.b) && snapshot.useB
            && ! snapshot.reference && snapshot.levelMatch && ! snapshot.legacyVoicingIgnored;
        valid &= isFirst || isSecond;
    } while (! done.load());
    writer.join();
    CHECK (valid);
    CHECK (importsValid.load());
}
