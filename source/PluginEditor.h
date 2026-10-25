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
    // Which stage the workbench shows: VTB9046 input, VTB1148 output or the 12BH7 output tube.
    enum class Stage { input, output, tube, gainTube, detector };
    void showStage (Stage);
    void updateStageVisibility (bool show);
    bool voicingVisible() const noexcept
    {
        return voicingPanel.isVisible() || inputVoicingPanel.isVisible() || tubeVoicingPanel.isVisible() || gainTubeVoicingPanel.isVisible()
            || detectorVoicingPanel.isVisible();
    }
    TransformerVoicingPanel voicingPanel, inputVoicingPanel;
    TubeVoicingPanel tubeVoicingPanel;
    GainTubeVoicingPanel gainTubeVoicingPanel;
    DetectorVoicingPanel detectorVoicingPanel;
    juce::TextButton voicingToggle { "Hide voicing" };
    juce::TextButton inputStage { "Input xfmr" }, outputStage { "Output xfmr" }, tubeStage { "Output tube" }, gainTubeStage { "Gain tube" },
        detectorStage { "Detector" };
    Stage shownStage = Stage::output;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
