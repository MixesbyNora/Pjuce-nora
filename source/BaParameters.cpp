#include "BaParameters.h"

void BaParameters::appendLayout (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    const auto addFloat = [&layout] (const FloatSpecification& spec)
    {
        juce::NormalisableRange<float> range { spec.minimum, spec.maximum, spec.step };
        if (spec.midpoint > spec.minimum)
            range.setSkewForCentre (spec.midpoint);
        const auto hasOffText = spec.offBelow > 0.0f;
        const auto signedGain = std::strcmp (spec.unit, "dB") == 0;
        // Units are in the value text, so the generic editor must not append a second label.
        const auto attributes = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([hasOffText, signedGain, offBelow = spec.offBelow, unit = spec.unit] (float value, int)
            {
                if (hasOffText && value < offBelow)
                    return juce::String ("Off");
                if (std::abs (value) < 0.05f)
                    value = 0.0f; // Display rounding only: never print negative zero.
                return (signedGain && value > 0.0f ? juce::String ("+") : juce::String())
                    + juce::String (value, 1) + " " + unit;
            }).withValueFromStringFunction ([hasOffText] (const juce::String& text)
            {
                return hasOffText && text.trim().equalsIgnoreCase ("Off") ? 0.0f : text.getFloatValue();
            });
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { spec.id, 1 }, spec.name, range, spec.initial, attributes));
    };
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { bypass.id, 1 }, bypass.name, bypass.initial));
    addFloat (inGain);
    addFloat (outGain);
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ioLink.id, 1 }, ioLink.name, ioLink.initial));
    addFloat (lfm);
    addFloat (mix);
    juce::StringArray choices;
    for (const auto* choice : monitorChoices)
        choices.add (choice);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { monitorId, 1 }, monitorName, choices, monitorInitial));
}
