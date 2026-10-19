#pragma once

#include "MeterBallistics.h"
#include <juce_gui_basics/juce_gui_basics.h>

class StereoLevelMeter : public juce::Component
{
public:
    explicit StereoLevelMeter (juce::String titleToUse);
    void setLevelsDb (float leftDb, float rightDb);
    void setChannelLabels (juce::String leftLabel, juce::String rightLabel);
    void paint (juce::Graphics& graphics) override;

private:
    juce::String title;
    std::array<juce::String, 2> labels { "L", "R" };
    std::array<MeterBallistics, 2> ballistics;
    double lastUpdateMs = juce::Time::getMillisecondCounterHiRes();
};
