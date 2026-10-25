#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& processorToUse)
    : AudioProcessorEditor (processorToUse), processorRef (processorToUse), parameterEditor (processorToUse),
      voicingPanel (processorToUse.getTransformerVoicing(), [&processorToUse] { return processorToUse.exportTransformerVoicing(); }),
      inputVoicingPanel (processorToUse.getInputTransformerVoicing(), [&processorToUse] { return processorToUse.exportTransformerVoicing (true); },
          "Input Transformer Voicing", "inputTransformerVoicingPanel", ModuleSpike::vtb9046LineInputC(), "VTB9046 line input"),
      tubeVoicingPanel (processorToUse.getTubeVoicing(), [&processorToUse] { return processorToUse.exportTubeVoicing(); },
          "Output Tube Voicing", "tubeVoicingPanel"),
      gainTubeVoicingPanel (processorToUse.getGainTubeVoicing(), [&processorToUse] { return processorToUse.exportGainTubeVoicing(); },
          "Gain Tube Voicing", "gainTubeVoicingPanel", false),
      detectorVoicingPanel (processorToUse.getDetectorVoicing(), [&processorToUse] { return processorToUse.exportDetectorVoicing(); },
          "Detector Voicing", "detectorVoicingPanel", false)
{
    parameterEditor.setResizable (false, false);
    addAndMakeVisible (parameterEditor);
    addAndMakeVisible (meter);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (voicingPanel);
    addChildComponent (inputVoicingPanel);
    addChildComponent (tubeVoicingPanel);
    addChildComponent (gainTubeVoicingPanel);
    addChildComponent (detectorVoicingPanel);
    addAndMakeVisible (voicingToggle);
    addAndMakeVisible (inputStage);
    addAndMakeVisible (outputStage);
    addAndMakeVisible (tubeStage);
    addAndMakeVisible (gainTubeStage);
    addAndMakeVisible (detectorStage);
    inputStage.setComponentID ("voicingStageInput");
    outputStage.setComponentID ("voicingStageOutput");
    tubeStage.setComponentID ("voicingStageTube");
    gainTubeStage.setComponentID ("voicingStageGainTube");
    detectorStage.setComponentID ("voicingStageDetector");
    for (auto* button : { &inputStage, &outputStage, &tubeStage, &gainTubeStage, &detectorStage })
    {
        button->setClickingTogglesState (false);
        button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff287f9b));
    }
    inputStage.onClick = [this] { showStage (Stage::input); };
    outputStage.onClick = [this] { showStage (Stage::output); };
    tubeStage.onClick = [this] { showStage (Stage::tube); };
    gainTubeStage.onClick = [this] { showStage (Stage::gainTube); };
    detectorStage.onClick = [this] { showStage (Stage::detector); };
    outputStage.setToggleState (true, juce::dontSendNotification);
    voicingToggle.setComponentID ("voicingToggle");
    voicingToggle.onClick = [this]
    {
        const auto show = ! voicingVisible();
        updateStageVisibility (show);
        for (auto* button : { &inputStage, &outputStage, &tubeStage, &gainTubeStage, &detectorStage })
            button->setVisible (show);
        voicingToggle.setButtonText (show ? "Hide voicing" : "Show voicing");
        setSize (show ? 960 : 640, 936);
    };
    setResizable (false, false);
    setSize (960, 936);
    meter.setGainReductionDb (processorRef.getGainReductionDb());
    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
}

void PluginEditor::updateStageVisibility (bool show)
{
    voicingPanel.setVisible (show && shownStage == Stage::output);
    inputVoicingPanel.setVisible (show && shownStage == Stage::input);
    tubeVoicingPanel.setVisible (show && shownStage == Stage::tube);
    gainTubeVoicingPanel.setVisible (show && shownStage == Stage::gainTube);
    detectorVoicingPanel.setVisible (show && shownStage == Stage::detector);
}

void PluginEditor::showStage (Stage stage)
{
    shownStage = stage;
    inputStage.setToggleState (stage == Stage::input, juce::dontSendNotification);
    outputStage.setToggleState (stage == Stage::output, juce::dontSendNotification);
    tubeStage.setToggleState (stage == Stage::tube, juce::dontSendNotification);
    gainTubeStage.setToggleState (stage == Stage::gainTube, juce::dontSendNotification);
    detectorStage.setToggleState (stage == Stage::detector, juce::dontSendNotification);
    if (voicingVisible())
    {
        updateStageVisibility (true);
        resized();
    }
}

void PluginEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    graphics.setColour (juce::Colours::white);
    graphics.setFont (juce::FontOptions (18.0f));
    graphics.drawText ("Lumiere", 16, 12, 432, 28, juce::Justification::centredLeft);
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto bottom = area.removeFromBottom (160);
    inputMeter.setBounds (bottom.removeFromTop (78));
    bottom.removeFromTop (4);
    outputMeter.setBounds (bottom);
    area.removeFromBottom (16);
    if (voicingVisible())
    {
        auto panel = area.removeFromRight (300);
        panel.removeFromTop (32);
        auto stages = panel.removeFromTop (24);
        const auto fifth = stages.getWidth() / 5;
        inputStage.setBounds (stages.removeFromLeft (fifth).reduced (2, 0));
        outputStage.setBounds (stages.removeFromLeft (fifth).reduced (2, 0));
        tubeStage.setBounds (stages.removeFromLeft (fifth).reduced (2, 0));
        gainTubeStage.setBounds (stages.removeFromLeft (fifth).reduced (2, 0));
        detectorStage.setBounds (stages.reduced (2, 0));
        panel.removeFromTop (6);
        voicingPanel.setBounds (panel);
        inputVoicingPanel.setBounds (panel);
        tubeVoicingPanel.setBounds (panel);
        gainTubeVoicingPanel.setBounds (panel);
        detectorVoicingPanel.setBounds (panel);
        area.removeFromRight (20);
    }
    voicingToggle.setBounds (getWidth() - 316, 18, voicingVisible() ? 300 : 148, 24);
    meter.setBounds (area.removeFromRight (160));
    area.removeFromRight (16);
    area.removeFromTop (32);
    parameterEditor.setBounds (area);
}

void PluginEditor::timerCallback()
{
    voicingPanel.refresh();
    inputVoicingPanel.refresh();
    tubeVoicingPanel.refresh();
    gainTubeVoicingPanel.refresh();
    detectorVoicingPanel.refresh();
    meter.setGainReductionDb (processorRef.getGainReductionDb());
    const auto peaks = processorRef.readLevelPeaks();
    inputMeter.setLevelsDb (juce::Decibels::gainToDecibels (peaks.inL, -60.0f),
        juce::Decibels::gainToDecibels (peaks.inR, -60.0f));
    outputMeter.setLevelsDb (juce::Decibels::gainToDecibels (peaks.outL, -60.0f),
        juce::Decibels::gainToDecibels (peaks.outR, -60.0f));
}
