#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace BaParameters
{
struct FloatSpecification
{
    const char* id;
    const char* name;
    const char* unit;
    float minimum;
    float maximum;
    float step;
    float initial;
    float midpoint = 0.0f;
    float offBelow = 0.0f;
};
struct BoolSpecification
{
    const char* id;
    const char* name;
    bool initial;
};
inline constexpr BoolSpecification bypass { "bypass", "Bypass", false };
inline constexpr FloatSpecification inGain { "inGain", "IN", "dB", -24.0f, 24.0f, 0.1f, 0.0f };
inline constexpr FloatSpecification outGain { "outGain", "OUT", "dB", -24.0f, 24.0f, 0.1f, 0.0f };
inline constexpr BoolSpecification ioLink { "ioLink", "IN/OUT Link", false };
inline constexpr BoolSpecification xfrmOut { "xfrmOut", "Xfrm Out", false };
inline constexpr FloatSpecification xfrmAmount { "xfrmAmount", "Xfrm Amount", "%", 0.0f, 100.0f, 0.1f, 0.0f };
inline constexpr FloatSpecification lfm { "lfm", "LFM", "Hz", 0.0f, 250.0f, 0.1f, 0.0f, 70.0f, 20.0f };
inline constexpr FloatSpecification mix { "mix", "Mix", "%", 0.0f, 100.0f, 0.1f, 100.0f };
inline constexpr const char* monitorId = "monitor";
inline constexpr const char* monitorName = "Monitor";
inline constexpr std::array monitorChoices { "LR", "M", "S", "D" };
inline constexpr int monitorInitial = 0;
inline constexpr double gainRampSeconds = 0.02;
inline constexpr double lfmRampSeconds = 0.02;
inline constexpr double switchRampSeconds = 0.01;
inline constexpr double lfmQ = 0.7071067811865476;

struct Values
{
    float inGainDb = inGain.initial;
    float outGainDb = outGain.initial;
    float lfmHz = lfm.initial;
    float mixPercent = mix.initial;
    bool bypassed = bypass.initial;
    int monitor = monitorInitial;
    bool transformerOut = xfrmOut.initial;
    float transformerAmountPercent = xfrmAmount.initial;
};

void appendLayout (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
}
