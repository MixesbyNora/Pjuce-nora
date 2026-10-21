#pragma once

#include "TransformerVoicingState.h"
#include "TransformerMeasurementView.h"
#include <juce_gui_basics/juce_gui_basics.h>

class TransformerVoicingPanel : public juce::Component
{
public:
    explicit TransformerVoicingPanel (TransformerVoicingState& state, std::function<juce::String()> exportSettings = {});
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showMeasurements (bool show);
    TransformerVoicingState& state;
    TransformerMeasurementView measurements;
    std::array<juce::Label, 3> labels;
    std::array<juce::Slider, 3> sliders;
    juce::TextButton a { "A" }, b { "B" }, copySlot { "B > A" }, reset { "Reset selected" };
    juce::TextButton copy { "Copy settings" }, paste { "Paste settings" };
    juce::TextButton voicingTab { "Voicing" }, measurementTab { "Measurements" };
    juce::ToggleButton reference { "Default magnetic voicing" }, match { "Match level (audition)" };
    juce::Label title, hint, status;
    juce::String pasteError;
};
