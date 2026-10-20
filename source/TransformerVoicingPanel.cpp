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
    const std::array names { "Drive calibration", "LF emphasis", "Shelf frequency" };
    const std::array ids { "voicingDrive", "voicingEmphasis", "voicingFrequency" };
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        labels[i].setText (names[i], juce::dontSendNotification);
        addAndMakeVisible (labels[i]);
        auto& slider = sliders[i];
        slider.setComponentID (ids[i]);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 24);
        slider.setTextValueSuffix (i == 2 ? " Hz" : " dB");
        slider.onValueChange = [this]
        {
            pasteError.clear();
            state.setCurrent ({ static_cast<float> (sliders[0].getValue()),
                static_cast<float> (sliders[1].getValue()), static_cast<float> (sliders[2].getValue()) });
        };
        addAndMakeVisible (slider);
    }
    using namespace TransformerSettings;
    sliders[0].setRange (calibrationMinimumDb, calibrationMaximumDb, 0.1);
    sliders[1].setRange (emphasisMinimumDb, emphasisMaximumDb, 0.1);
    sliders[2].setRange (cornerMinimumHz, cornerMaximumHz, 1.0);
    sliders[2].setSkewFactorFromMidPoint (shelfFrequencyHz);
    sliders[0].setTooltip ("Offsets the existing Amount-to-drive mapping. Zero retains the original drive calibration.");
    sliders[1].setTooltip ("Low-frequency excitation before saturation. De-emphasis remains its exact inverse.");
    sliders[2].setTooltip ("Corner frequency of the inverse shelf pair.");
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
    hint.setText ("Current model: tanh + residual ADAA\n\nUse Xfrm Amount above 0% and keep Xfrm Out unchecked.\n\nFor comparisons, let Match level settle for about a second. Disable it for exact reference and harmonic measurements.", juce::dontSendNotification);
    hint.setFont (juce::FontOptions (13.0f));
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);
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
    sliders[0].setValue (values.driveCalibrationDb, juce::dontSendNotification);
    sliders[1].setValue (values.emphasisDb, juce::dontSendNotification);
    sliders[2].setValue (values.cornerHz, juce::dontSendNotification);
    for (auto& slider : sliders)
        slider.setEnabled (! snapshot.reference);
    a.setToggleState (! snapshot.useB && ! snapshot.reference, juce::dontSendNotification);
    b.setToggleState (snapshot.useB && ! snapshot.reference, juce::dontSendNotification);
    reference.setToggleState (snapshot.reference, juce::dontSendNotification);
    match.setToggleState (snapshot.levelMatch, juce::dontSendNotification);
    copySlot.setButtonText (snapshot.useB ? "B > A" : "A > B");
    status.setText (pasteError.isNotEmpty() ? pasteError
        : juce::String ("Model v1 | ") + (snapshot.reference ? "Original" : snapshot.useB ? "Editing B" : "Editing A")
            + "\nSettings and both snapshots are saved with the project.", juce::dontSendNotification);
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
    area.removeFromTop (16);
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        labels[i].setBounds (area.removeFromTop (24));
        sliders[i].setBounds (area.removeFromTop (32));
        area.removeFromTop (12);
    }
    reference.setBounds (area.removeFromTop (28));
    match.setBounds (area.removeFromTop (28));
    area.removeFromTop (12);
    reset.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);
    row = area.removeFromTop (28);
    copy.setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
    row.removeFromLeft (8);
    paste.setBounds (row);
    area.removeFromTop (12);
    hint.setBounds (area.removeFromTop (164));
    status.setBounds (area);
}
