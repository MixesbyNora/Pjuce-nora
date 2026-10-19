#pragma once

#include "GainReductionMeter.h"
#include "StereoLevelMeter.h"
#include "PluginProcessor.h"

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
