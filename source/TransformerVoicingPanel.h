#pragma once

#include "TransformerVoicingState.h"
#include "TransformerMeasurementView.h"
#include "VoicingPanel.h"

// A transformer workbench: the generic voicing panel plus its measurement tab.
class TransformerVoicingPanel : public VoicingPanel<TransformerVoicing::Schema>
{
public:
    explicit TransformerVoicingPanel (TransformerVoicingState& state, std::function<juce::String()> exportSettings = {},
        juce::String titleText = "Output Transformer Voicing", juce::String componentId = "transformerVoicingPanel",
        ModuleSpike::TransformerParameters calibration = ModuleSpike::vtb1148OutputC(), juce::String stage = "VTB1148 output");

private:
    TransformerMeasurementView measurements;
};
using TubeVoicingPanel = VoicingPanel<TubeVoicing::Schema>;
using GainTubeVoicingPanel = VoicingPanel<GainTubeVoicing::Schema>;
