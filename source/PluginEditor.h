#pragma once

#include "GainReductionMeter.h"
#include "StereoLevelMeter.h"
#include "PluginProcessor.h"
#include "TransformerVoicingPanel.h"

class PluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor& processorToUse);
    ~PluginEditor() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;

    PluginProcessor& processorRef;
    juce::GenericAudioProcessorEditor parameterEditor;
    GainReductionMeter meter;
    StereoLevelMeter inputMeter { "IN" }, outputMeter { "OUT" };
    void showStage (bool input);
    bool voicingVisible() const noexcept { return voicingPanel.isVisible() || inputVoicingPanel.isVisible(); }
    TransformerVoicingPanel voicingPanel, inputVoicingPanel;
    juce::TextButton voicingToggle { "Hide voicing" };
    // Which transformer the workbench shows: VTB9046 input or VTB1148 output.
    juce::TextButton inputStage { "Input xfmr" }, outputStage { "Output xfmr" };
    bool showingInput = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
