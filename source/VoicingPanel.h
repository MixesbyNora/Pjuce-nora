#pragma once

#include "VoicingState.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Workbench panel for any voicing schema: A/B slots, one slider per descriptor,
// Default and Match level, reset, copy/paste. A stage may add a measurement view,
// which then gets Voicing / Measurements tabs.
template <typename Schema>
class VoicingPanel : public juce::Component
{
public:
    using State = VoicingState<Schema>;
    // levelMatch false hides Match level, for stages whose gain is exact by design.
    VoicingPanel (State& stateToUse, std::function<juce::String()> exportSettings, juce::String titleText, juce::String componentId,
        bool levelMatch = true)
        : state (stateToUse), showsMatch (levelMatch)
    {
        setComponentID (componentId);
        title.setText (titleText, juce::dontSendNotification);
        title.setFont (juce::FontOptions (18.0f));
        title.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title);
        addChildComponent (voicingTab);
        addChildComponent (measurementTab);
        voicingTab.setComponentID ("voicingTab");
        measurementTab.setComponentID ("measurementTab");
        voicingTab.onClick = [this] { showMeasurements (false); };
        measurementTab.onClick = [this] { showMeasurements (true); };
        voicingTab.setToggleState (true, juce::dontSendNotification);
        for (std::size_t i = 0; i < sliders.size(); ++i)
        {
            const auto& descriptor = Schema::descriptors[i];
            labels[i].setText (descriptor.label, juce::dontSendNotification);
            addAndMakeVisible (labels[i]);
            auto& slider = sliders[i];
            slider.setComponentID ("voicing_" + juce::String (descriptor.id));
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 24);
            slider.setTextValueSuffix (juce::String (descriptor.unit).isEmpty() ? juce::String() : " " + juce::String (descriptor.unit));
            slider.setRange (descriptor.minimum, descriptor.maximum, descriptor.step);
            slider.setSkewFactor (descriptor.skew);
            slider.setDoubleClickReturnValue (true, descriptor.defaultValue);
            if (descriptor.choices != nullptr)
            {
                const auto names = juce::StringArray::fromTokens (descriptor.choices, "|", "");
                slider.setTextValueSuffix ({});
                slider.textFromValueFunction = [names, minimum = descriptor.minimum] (double value)
                { return names[juce::roundToInt (value - minimum)]; };
                slider.valueFromTextFunction = [names, minimum = descriptor.minimum] (const juce::String& text)
                { return minimum + juce::jmax (0, names.indexOf (text.trim(), true)); };
                slider.updateText();
            }
            slider.onValueChange = [this]
            {
                pasteError.clear();
                typename State::Values values;
                for (std::size_t index = 0; index < sliders.size(); ++index)
                    values[index] = static_cast<float> (sliders[index].getValue());
                state.setCurrent (values);
            };
            addAndMakeVisible (slider);
        }
        for (auto* button : { &a, &b, &copySlot, &reset, &copy, &paste })
            addAndMakeVisible (*button);
        a.setComponentID ("voicingA");
        b.setComponentID ("voicingB");
        a.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff287f9b));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff287f9b));
        copySlot.setComponentID ("voicingCopySlot");
        reset.setComponentID ("voicingReset");
        copy.setComponentID ("voicingCopy");
        paste.setComponentID ("voicingPaste");
        reference.setComponentID ("voicingReference");
        match.setComponentID ("voicingMatch");
        a.onClick = [this] { state.selectB (false); refresh(); };
        b.onClick = [this] { state.selectB (true); refresh(); };
        copySlot.onClick = [this] { state.copyToOther(); refresh(); };
        reset.onClick = [this] { pasteError.clear(); state.resetCurrent(); refresh(); };
        reference.onClick = [this] { state.setReference (reference.getToggleState()); refresh(); };
        match.onClick = [this] { state.setLevelMatch (match.getToggleState()); refresh(); };
        copy.onClick = [this, exporter = std::move (exportSettings)]
        {
            juce::SystemClipboard::copyTextToClipboard (exporter ? exporter() : state.toJson());
        };
        copy.setTooltip ("Copies both voicings, selection and audition flags, plus current plugin parameters and sample rate for feedback.");
        paste.setTooltip ("Restores voicing snapshots and audition flags. Compressor and other host parameters stay as they are.");
        paste.onClick = [this]
        {
            pasteError = state.restoreJson (juce::SystemClipboard::getTextFromClipboard())
                ? juce::String() : "Cannot paste: incompatible or incomplete voicing settings.";
            refresh();
        };
        addAndMakeVisible (reference);
        addChildComponent (match);
        match.setVisible (showsMatch);
        hint.setText ("Adjust one control, compare A/B, then copy settings with listening notes.\n\nLet edits and Match level settle before comparing. Disable matching for measurements.", juce::dontSendNotification);
        hint.setFont (juce::FontOptions (13.0f));
        hint.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hint);
        status.setComponentID ("voicingStatus");
        status.setFont (juce::FontOptions (13.0f));
        status.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (status);
        refresh();
    }

    void refresh()
    {
        if (refreshMeasurements) refreshMeasurements();
        const auto snapshot = state.read();
        const auto values = snapshot.reference ? typename State::Values {} : snapshot.useB ? snapshot.b : snapshot.a;
        for (std::size_t i = 0; i < sliders.size(); ++i)
            sliders[i].setValue (values[i], juce::dontSendNotification);
        for (auto& slider : sliders)
            slider.setEnabled (! snapshot.reference);
        a.setToggleState (! snapshot.useB && ! snapshot.reference, juce::dontSendNotification);
        b.setToggleState (snapshot.useB && ! snapshot.reference, juce::dontSendNotification);
        reference.setToggleState (snapshot.reference, juce::dontSendNotification);
        match.setToggleState (snapshot.levelMatch, juce::dontSendNotification);
        copySlot.setButtonText (snapshot.useB ? "B > A" : "A > B");
        status.setText (pasteError.isNotEmpty() ? pasteError
            : juce::String ("Workbench v") + juce::String (Schema::version) + " | " + (snapshot.reference ? "Default" : snapshot.useB ? "Editing B" : "Editing A")
                + "\nSettings and both snapshots are saved with the project."
                + (snapshot.legacyVoicingIgnored ? "\nVoicing from a retired engine was not applied: defaults loaded." : ""), juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12);
        title.setBounds (area.removeFromTop (32));
        if (measurementView != nullptr)
        {
            auto tabs = area.removeFromTop (26);
            voicingTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2));
            measurementTab.setBounds (tabs);
            area.removeFromTop (8);
            measurementView->setBounds (area);
        }
        auto row = area.removeFromTop (28);
        a.setBounds (row.removeFromLeft (48));
        row.removeFromLeft (6);
        b.setBounds (row.removeFromLeft (48));
        row.removeFromLeft (6);
        copySlot.setBounds (row);
        area.removeFromTop (8);
        for (std::size_t i = 0; i < sliders.size(); ++i)
        {
            labels[i].setBounds (area.removeFromTop (20));
            sliders[i].setBounds (area.removeFromTop (28));
            area.removeFromTop (6);
        }
        reference.setBounds (area.removeFromTop (28));
        if (showsMatch) match.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        reset.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        row = area.removeFromTop (28);
        copy.setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
        row.removeFromLeft (8);
        paste.setBounds (row);
        area.removeFromTop (12);
        hint.setBounds (area.removeFromTop (100));
        status.setBounds (area);
    }

protected:
    // The view stays owned by the caller (normally a member of a subclass).
    void setMeasurementView (juce::Component& view, std::function<void()> refreshView)
    {
        measurementView = &view;
        refreshMeasurements = std::move (refreshView);
        addChildComponent (view);
        voicingTab.setVisible (true);
        measurementTab.setVisible (true);
        refresh();
    }

private:
    void showMeasurements (bool show)
    {
        for (auto& label : labels)
            label.setVisible (! show);
        for (auto& slider : sliders)
            slider.setVisible (! show);
        for (auto* component : std::array<juce::Component*, 9> { &a, &b, &copySlot, &reset, &copy, &paste, &reference, &hint, &status })
            component->setVisible (! show);
        match.setVisible (showsMatch && ! show);
        if (measurementView != nullptr) measurementView->setVisible (show);
        voicingTab.setToggleState (! show, juce::dontSendNotification);
        measurementTab.setToggleState (show, juce::dontSendNotification);
    }
    State& state;
    bool showsMatch = true;
    juce::Component* measurementView = nullptr;
    std::function<void()> refreshMeasurements;
    std::array<juce::Label, Schema::descriptors.size()> labels;
    std::array<juce::Slider, Schema::descriptors.size()> sliders;
    juce::TextButton a { "A" }, b { "B" }, copySlot { "B > A" }, reset { "Reset selected" };
    juce::TextButton copy { "Copy settings" }, paste { "Paste settings" };
    juce::TextButton voicingTab { "Voicing" }, measurementTab { "Measurements" };
    juce::ToggleButton reference { "Default voicing" }, match { "Match level (audition)" };
    juce::Label title, hint, status;
    juce::String pasteError;
};
