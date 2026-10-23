#include "TransformerVoicingPanel.h"

TransformerVoicingPanel::TransformerVoicingPanel (TransformerVoicingState& stateToUse, std::function<juce::String()> exportSettings)
    : state (stateToUse), measurements (stateToUse, exportSettings)
{
    setComponentID ("transformerVoicingPanel");
    title.setText ("Transformer Voicing", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f));
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);
    addAndMakeVisible (voicingTab);
    addAndMakeVisible (measurementTab);
    addChildComponent (measurements);
    voicingTab.setComponentID ("voicingTab");
    measurementTab.setComponentID ("measurementTab");
    voicingTab.onClick = [this] { showMeasurements (false); };
    measurementTab.onClick = [this] { showMeasurements (true); };
    voicingTab.setToggleState (true, juce::dontSendNotification);
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto& descriptor = TransformerVoicing::descriptors[i];
        labels[i].setText (descriptor.label, juce::dontSendNotification);
        addAndMakeVisible (labels[i]);
        auto& slider = sliders[i];
        slider.setComponentID ("voicing_" + juce::String (descriptor.id));
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 24);
        slider.setTextValueSuffix (" " + juce::String (descriptor.unit));
        slider.setRange (descriptor.minimum, descriptor.maximum, descriptor.step);
        slider.setSkewFactor (descriptor.skew);
        slider.setDoubleClickReturnValue (true, descriptor.defaultValue);
        slider.onValueChange = [this]
        {
            pasteError.clear();
            TransformerVoicing::Values values;
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
    addAndMakeVisible (match);
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

void TransformerVoicingPanel::refresh()
{
    measurements.refresh();
    const auto snapshot = state.read();
    const auto values = snapshot.reference ? TransformerVoicing::Values {} : snapshot.useB ? snapshot.b : snapshot.a;
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
        : juce::String ("Workbench v") + juce::String (TransformerVoicing::version) + " | " + (snapshot.reference ? "Default" : snapshot.useB ? "Editing B" : "Editing A")
            + "\nSettings and both snapshots are saved with the project."
            + (snapshot.legacyVoicingIgnored ? "\nMagnetic voicing from an older session was not applied: VTB1148 defaults." : ""), juce::dontSendNotification);
}

void TransformerVoicingPanel::showMeasurements (bool show)
{
    for (auto& label : labels)
        label.setVisible (! show);
    for (auto& slider : sliders)
        slider.setVisible (! show);
    for (auto* component : std::array<juce::Component*, 10> { &a, &b, &copySlot, &reset, &copy, &paste, &reference, &match, &hint, &status })
        component->setVisible (! show);
    measurements.setVisible (show);
    voicingTab.setToggleState (! show, juce::dontSendNotification);
    measurementTab.setToggleState (show, juce::dontSendNotification);
}

void TransformerVoicingPanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void TransformerVoicingPanel::resized()
{
    auto area = getLocalBounds().reduced (12);
    title.setBounds (area.removeFromTop (32));
    auto tabs = area.removeFromTop (26);
    voicingTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2));
    measurementTab.setBounds (tabs);
    area.removeFromTop (8);
    measurements.setBounds (area);
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
    match.setBounds (area.removeFromTop (28));
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
