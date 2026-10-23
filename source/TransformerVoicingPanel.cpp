#include "TransformerVoicingPanel.h"

TransformerVoicingPanel::TransformerVoicingPanel (TransformerVoicingState& stateToUse, std::function<juce::String()> exportSettings,
    juce::String titleText, juce::String componentId, ModuleSpike::TransformerParameters calibration, juce::String stage)
    : VoicingPanel (stateToUse, exportSettings, std::move (titleText), std::move (componentId)),
      measurements (stateToUse, exportSettings, calibration, std::move (stage))
{
    setMeasurementView (measurements, [this] { measurements.refresh(); });
}
