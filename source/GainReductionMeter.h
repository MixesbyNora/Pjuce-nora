#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Message-thread display component. It receives only a dB reading and owns no
// processor reference, parameter attachments, or audio-thread state.
class GainReductionMeter : public juce::Component
{
public:
    void setGainReductionDb (float reductionDb);
    void paint (juce::Graphics& graphics) override;

private:
    float currentDb = 0.0f;
    float displayedDb = 0.0f;
    float peakDb = 0.0f;
    double lastUpdateMs = juce::Time::getMillisecondCounterHiRes();
    double peakHoldUntilMs = 0.0;
};
