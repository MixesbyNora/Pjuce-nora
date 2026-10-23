#pragma once

#include "TransformerMeasurement.h"
#include "TransformerVoicingState.h"
#include <juce_gui_basics/juce_gui_basics.h>

class TransformerMeasurementView : public juce::Component
{
public:
    explicit TransformerMeasurementView (TransformerVoicingState&, std::function<juce::String()> exportSettings = {},
        ModuleSpike::TransformerParameters calibration = ModuleSpike::vtb1148OutputC(), juce::String stage = "VTB1148 output");
    ~TransformerMeasurementView() override;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isMeasuring() const;
    const TransformerMeasurement::Result* getResult() const noexcept { return result ? &*result : nullptr; }

private:
    class Worker;
    void startOrCancel();
    TransformerVoicingState& state;
    std::function<juce::String()> exporter;
    ModuleSpike::TransformerParameters calibration;
    juce::String stage;
    std::unique_ptr<Worker> worker;
    std::optional<TransformerMeasurement::Result> result;
    juce::Label intro, status;
    std::array<juce::Label, 3> labels;
    std::array<juce::ComboBox, 3> selectors;
    juce::TextButton measure { "Measure A/B" }, copy { "Copy CSV" };
    juce::Rectangle<int> gainPlot, harmonicPlot, readout;
    bool awaiting = false;
};
