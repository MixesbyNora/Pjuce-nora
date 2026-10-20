#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& processorToUse)
    : AudioProcessorEditor (processorToUse), processorRef (processorToUse), parameterEditor (processorToUse)
{
    parameterEditor.setResizable (false, false);
    addAndMakeVisible (parameterEditor);
    addAndMakeVisible (meter);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    setResizable (false, false);
    setSize (640, 936);
    meter.setGainReductionDb (processorRef.getGainReductionDb());
    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
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
    meter.setBounds (area.removeFromRight (160));
    area.removeFromRight (16);
    area.removeFromTop (32);
    parameterEditor.setBounds (area);
}

void PluginEditor::timerCallback()
{
    meter.setGainReductionDb (processorRef.getGainReductionDb());
    const auto peaks = processorRef.readLevelPeaks();
    inputMeter.setLevelsDb (juce::Decibels::gainToDecibels (peaks.inL, -60.0f),
        juce::Decibels::gainToDecibels (peaks.inR, -60.0f));
    outputMeter.setLevelsDb (juce::Decibels::gainToDecibels (peaks.outL, -60.0f),
        juce::Decibels::gainToDecibels (peaks.outR, -60.0f));
}
