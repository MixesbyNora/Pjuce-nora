#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& processorToUse)
    : AudioProcessorEditor (processorToUse), processorRef (processorToUse), parameterEditor (processorToUse),
      voicingPanel (processorToUse.getTransformerVoicing(), [&processorToUse] { return processorToUse.exportTransformerVoicing(); }),
      inputVoicingPanel (processorToUse.getInputTransformerVoicing(), [&processorToUse] { return processorToUse.exportTransformerVoicing (true); },
          "Input Transformer Voicing", "inputTransformerVoicingPanel", ModuleSpike::vtb9046LineInputC(), "VTB9046 line input")
{
    parameterEditor.setResizable (false, false);
    addAndMakeVisible (parameterEditor);
    addAndMakeVisible (meter);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (voicingPanel);
    addChildComponent (inputVoicingPanel);
    addAndMakeVisible (voicingToggle);
    addAndMakeVisible (inputStage);
    addAndMakeVisible (outputStage);
    inputStage.setComponentID ("voicingStageInput");
    outputStage.setComponentID ("voicingStageOutput");
    for (auto* button : { &inputStage, &outputStage })
    {
        button->setClickingTogglesState (false);
        button->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff287f9b));
    }
    inputStage.onClick = [this] { showStage (true); };
    outputStage.onClick = [this] { showStage (false); };
    outputStage.setToggleState (true, juce::dontSendNotification);
    voicingToggle.setComponentID ("voicingToggle");
    voicingToggle.onClick = [this]
    {
        const auto show = ! voicingVisible();
        voicingPanel.setVisible (show && ! showingInput);
        inputVoicingPanel.setVisible (show && showingInput);
        inputStage.setVisible (show);
        outputStage.setVisible (show);
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

void PluginEditor::showStage (bool input)
{
    showingInput = input;
    inputStage.setToggleState (input, juce::dontSendNotification);
    outputStage.setToggleState (! input, juce::dontSendNotification);
    if (voicingVisible())
    {
        voicingPanel.setVisible (! input);
        inputVoicingPanel.setVisible (input);
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
        inputStage.setBounds (stages.removeFromLeft (stages.getWidth() / 2).reduced (2, 0));
        outputStage.setBounds (stages.reduced (2, 0));
        panel.removeFromTop (6);
        voicingPanel.setBounds (panel);
        inputVoicingPanel.setBounds (panel);
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
    meter.setGainReductionDb (processorRef.getGainReductionDb());
    const auto peaks = processorRef.readLevelPeaks();
    inputMeter.setLevelsDb (juce::Decibels::gainToDecibels (peaks.inL, -60.0f),
        juce::Decibels::gainToDecibels (peaks.inR, -60.0f));
    outputMeter.setLevelsDb (juce::Decibels::gainToDecibels (peaks.outL, -60.0f),
        juce::Decibels::gainToDecibels (peaks.outR, -60.0f));
}
