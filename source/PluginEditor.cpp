#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& processorToUse)
    : AudioProcessorEditor (processorToUse), processorRef (processorToUse), parameterEditor (processorToUse),
      voicingPanel (processorToUse.getTransformerVoicing(), [&processorToUse] { return processorToUse.exportTransformerVoicing(); }),
      inputVoicingPanel (processorToUse.getInputTransformerVoicing(), [&processorToUse] { return processorToUse.exportTransformerVoicing (true); },
          "Input Transformer Voicing", "inputTransformerVoicingPanel", ModuleSpike::vtb9046LineInputC(), "VTB9046 line input"),
      tubeVoicingPanel (processorToUse.getTubeVoicing(), [&processorToUse] { return processorToUse.exportTubeVoicing(); },
          "Output Tube Voicing", "tubeVoicingPanel")
{
    parameterEditor.setResizable (false, false);
    addAndMakeVisible (parameterEditor);
    addAndMakeVisible (meter);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (voicingPanel);
    addChildComponent (inputVoicingPanel);
    addChildComponent (tubeVoicingPanel);
    addAndMakeVisible (voicingToggle);
    addAndMakeVisible (inputStage);
    addAndMakeVisible (outputStage);
    addAndMakeVisible (tubeStage);
    inputStage.setComponentID ("voicingStageInput");
    outputStage.setComponentID ("voicingStageOutput");
    tubeStage.setComponentID ("voicingStageTube");
    for (auto* button : { &inputStage, &outputStage, &tubeStage })
    {
        button->setClickingTogglesState (false);
        button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff287f9b));
    }
    inputStage.onClick = [this] { showStage (Stage::input); };
    outputStage.onClick = [this] { showStage (Stage::output); };
    tubeStage.onClick = [this] { showStage (Stage::tube); };
    outputStage.setToggleState (true, juce::dontSendNotification);
    voicingToggle.setComponentID ("voicingToggle");
    voicingToggle.onClick = [this]
    {
        const auto show = ! voicingVisible();
        updateStageVisibility (show);
        for (auto* button : { &inputStage, &outputStage, &tubeStage })
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
}

void PluginEditor::showStage (Stage stage)
{
    shownStage = stage;
    inputStage.setToggleState (stage == Stage::input, juce::dontSendNotification);
    outputStage.setToggleState (stage == Stage::output, juce::dontSendNotification);
    tubeStage.setToggleState (stage == Stage::tube, juce::dontSendNotification);
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
        const auto third = stages.getWidth() / 3;
        inputStage.setBounds (stages.removeFromLeft (third).reduced (2, 0));
        outputStage.setBounds (stages.removeFromLeft (third).reduced (2, 0));
        tubeStage.setBounds (stages.reduced (2, 0));
        panel.removeFromTop (6);
        voicingPanel.setBounds (panel);
        inputVoicingPanel.setBounds (panel);
        tubeVoicingPanel.setBounds (panel);
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
    meter.setGainReductionDb (processorRef.getGainReductionDb());
    const auto peaks = processorRef.readLevelPeaks();
    inputMeter.setLevelsDb (juce::Decibels::gainToDecibels (peaks.inL, -60.0f),
        juce::Decibels::gainToDecibels (peaks.inR, -60.0f));
    outputMeter.setLevelsDb (juce::Decibels::gainToDecibels (peaks.outL, -60.0f),
        juce::Decibels::gainToDecibels (peaks.outR, -60.0f));
}
