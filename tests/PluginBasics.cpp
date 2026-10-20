#include "helpers/test_helpers.h"
#include <PluginProcessor.h>
#include <PluginEditor.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

TEST_CASE ("Plugin instance", "[instance]")
{
    PluginProcessor testPlugin;

    SECTION ("name")
    {
        CHECK_THAT (testPlugin.getName().toStdString(),
            Catch::Matchers::Equals ("Lumiere Test v0.1"));
    }

    SECTION ("program name")
    {
        // Steinberg's VST3 validator fails plugins whose programs have no name
        CHECK (testPlugin.getProgramName (0).isNotEmpty());
    }
}

TEST_CASE ("Editor contains fixed-size generic controls and a separate meter", "[editor]")
{
    PluginProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    REQUIRE (dynamic_cast<PluginEditor*> (editor.get()) != nullptr);
    CHECK_FALSE (editor->isResizable());
    CHECK (editor->getWidth() == 640);
    CHECK (editor->getHeight() == 936);

    juce::GenericAudioProcessorEditor* controls = nullptr;
    GainReductionMeter* meter = nullptr;
    std::vector<StereoLevelMeter*> levelMeters;
    for (auto* child : editor->getChildren())
    {
        if (auto* generic = dynamic_cast<juce::GenericAudioProcessorEditor*> (child))
            controls = generic;
        if (auto* display = dynamic_cast<GainReductionMeter*> (child))
            meter = display;
        if (auto* levels = dynamic_cast<StereoLevelMeter*> (child))
            levelMeters.push_back (levels);
    }
    REQUIRE (controls != nullptr);
    REQUIRE (meter != nullptr);
    CHECK (controls->isVisible());
    CHECK (meter->isVisible());
    CHECK_FALSE (controls->getBounds().intersects (meter->getBounds()));
    CHECK (editor->getLocalBounds().contains (controls->getBounds()));
    CHECK (editor->getLocalBounds().contains (meter->getBounds()));
    REQUIRE (levelMeters.size() == 2);
    for (auto* levels : levelMeters)
    {
        CHECK (levels->isVisible());
        CHECK (editor->getLocalBounds().contains (levels->getBounds()));
        CHECK_FALSE (levels->getBounds().intersects (controls->getBounds()));
        CHECK_FALSE (levels->getBounds().intersects (meter->getBounds()));
    }
    CHECK_FALSE (levelMeters[0]->getBounds().intersects (levelMeters[1]->getBounds()));

    // Optional local snapshot for inspecting layout without opening an audio device.
    if (const auto* previewPath = std::getenv ("LUMIERE_EDITOR_PREVIEW"))
    {
        meter->setGainReductionDb (12.0f);
        juce::Thread::sleep (60);
        meter->setGainReductionDb (8.0f);
        levelMeters[0]->setLevelsDb (-6.0f, -12.0f);
        levelMeters[1]->setLevelsDb (3.0f, -24.0f);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream output { juce::File (juce::String::fromUTF8 (previewPath)) };
        REQUIRE (output.openedOk());
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, output));
    }
}


#ifdef PAMPLEJUCE_IPP
    #include <ipp.h>

TEST_CASE ("IPP version", "[ipp]")
{
    #if defined(__APPLE__)
        // macOS uses 2021.9.1 from pip wheel (only x86_64 version available)
        CHECK_THAT (ippsGetLibVersion()->Version, Catch::Matchers::Equals ("2021.9.1 (r0x7e208212)"));
    #else
        CHECK_THAT (ippsGetLibVersion()->Version, Catch::Matchers::Equals ("2026.0.0 (r0xa7ad6ebc)"));
    #endif
}
#endif
