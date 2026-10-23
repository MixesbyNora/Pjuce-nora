#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "TransformerVoicing.h"
#include "TubeVoicing.h"
#include "GainTubeVoicing.h"

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
// IDs stay stable for automation; names are what hosts show.
inline constexpr BoolSpecification xfrmOut { "xfrmOut", "Output XFRM", false };
inline constexpr BoolSpecification inputXfrmOut { "inputXfrmOut", "Input XFRM", false };
// The 12BH7 follower driving the output transformer (docs/TubeStage.md).
inline constexpr BoolSpecification outputTubeOut { "outputTubeOut", "Output Tube", false };
// The tube's own drive, mapped like Xfrm Amount.
inline constexpr FloatSpecification tubeAmount { "tubeAmount", "Tube Amount", "%", 0.0f, 100.0f, 0.1f, 400.0f / 7.0f };
// The compressor's vari-mu gain element; Out is the exact digital gain (docs/GainTube.md).
inline constexpr BoolSpecification gainTubeOut { "gainTubeOut", "Gain Tube", false };
// Amounts are drive only (-24 to +18 dB; 400/7 % is the calibrated 0 dB, the
// default). Owner decision: the In/Out switches are the only bypass.
inline constexpr FloatSpecification xfrmAmount { "xfrmAmount", "Xfrm Amount", "%", 0.0f, 100.0f, 0.1f, 400.0f / 7.0f };
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
    TransformerVoicing::Values transformerVoicing;
    bool voicingLevelMatch = false;
    // Input transformer (VTB9046) workbench voicing; the stage is always in circuit.
    TransformerVoicing::Values inputTransformerVoicing;
    bool inputVoicingLevelMatch = false;
    bool inputTransformerOut = inputXfrmOut.initial;
    // Output tube workbench voicing, and its bypass.
    TubeVoicing::Values tubeVoicing;
    bool tubeVoicingLevelMatch = false;
    bool outputTubeOut = BaParameters::outputTubeOut.initial;
    float tubeAmountPercent = tubeAmount.initial;
    GainTubeVoicing::Values gainTubeVoicing;
    bool gainTubeOut = BaParameters::gainTubeOut.initial;
};

void appendLayout (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
}
