#include "TransformerMeasurementView.h"
#include <mutex>

class TransformerMeasurementView::Worker : private juce::Thread
{
public:
    Worker() : Thread ("Lumiere transformer measurement") {}
    ~Worker() override
    {
        signalThreadShouldExit();
        stopThread (-1); // Cancellation is checked every 256 offline samples.
    }
    bool start (TransformerMeasurement::Request request)
    {
        jassert (! isThreadRunning());
        job = std::move (request);
        progress.store (0.0f, std::memory_order_relaxed);
        return startThread (juce::Thread::Priority::low);
    }
    void cancel() { signalThreadShouldExit(); }
    bool running() const { return isThreadRunning(); }
    std::optional<TransformerMeasurement::Result> takeResult()
    {
        const std::lock_guard lock (resultMutex);
        auto completed = std::move (result);
        result.reset();
        return completed;
    }
    std::atomic<float> progress { 0.0f };

private:
    void run() override
    {
        auto completed = TransformerMeasurement::measure (job, [this] { return threadShouldExit(); },
            [this] (float value) { progress.store (value, std::memory_order_relaxed); });
        const std::lock_guard lock (resultMutex);
        result = std::move (completed);
    }
    TransformerMeasurement::Request job;
    std::mutex resultMutex; // Only the worker and message thread use this mutex.
    std::optional<TransformerMeasurement::Result> result;
};

namespace
{
juce::Colour voiceColour (int voice) { return juce::Colour (voice == 0 ? 0xff64c6dc : 0xfff2b85b); }
juce::String harmonicText (const TransformerMeasurement::Metrics& metrics, std::size_t index)
{
    return ! metrics.available[index] ? juce::String ("NA") : metrics.harmonicDbc[index] < -140.0
        ? juce::String ("< -140") : juce::String (metrics.harmonicDbc[index], 1);
}
}

TransformerMeasurementView::TransformerMeasurementView (TransformerVoicingState& stateToUse,
    std::function<juce::String()> exportSettings)
    : state (stateToUse), exporter (std::move (exportSettings)), worker (std::make_unique<Worker>())
{
    setComponentID ("transformerMeasurementView");
    intro.setText ("48 kHz | Transformer only | Match off\nStored A/B snapshots; internal test tones.", juce::dontSendNotification);
    intro.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (intro);
    const std::array names { "Tone", "Level", "Amount" };
    for (std::size_t i = 0; i < selectors.size(); ++i)
    {
        labels[i].setText (names[i], juce::dontSendNotification);
        labels[i].setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (labels[i]);
        selectors[i].setComponentID ("measurementSelector" + juce::String (static_cast<int> (i)));
        selectors[i].onChange = [this] { repaint(); };
        addAndMakeVisible (selectors[i]);
    }
    for (std::size_t i = 0; i < TransformerMeasurement::frequencies.size(); ++i)
        selectors[0].addItem (juce::String (TransformerMeasurement::frequencies[i], 0) + " Hz", static_cast<int> (i + 1));
    for (std::size_t i = 0; i < TransformerMeasurement::levels.size(); ++i)
        selectors[1].addItem (juce::String (TransformerMeasurement::levels[i], 0) + " dBFS", static_cast<int> (i + 1));
    for (std::size_t i = 0; i < TransformerMeasurement::amounts.size(); ++i)
        selectors[2].addItem (juce::String (TransformerMeasurement::amounts[i], 0) + "%", static_cast<int> (i + 1));
    selectors[0].setSelectedItemIndex (4, juce::dontSendNotification);
    selectors[1].setSelectedItemIndex (1, juce::dontSendNotification);
    selectors[2].setSelectedItemIndex (2, juce::dontSendNotification);
    measure.setComponentID ("measurementRun");
    copy.setComponentID ("measurementCopyCsv");
    measure.onClick = [this] { startOrCancel(); };
    copy.onClick = [this]
    {
        if (result)
            juce::SystemClipboard::copyTextToClipboard (TransformerMeasurement::toCsv (*result));
    };
    copy.setEnabled (false);
    measure.setTooltip ("Captures both voicing slots. Measures 7 frequencies, 4 input levels and 3 Amount settings without sending tones to the DAW.");
    copy.setTooltip ("Copies the full captured A/B report and settings, including readings not currently shown.");
    addAndMakeVisible (measure);
    addAndMakeVisible (copy);
    status.setText ("Press Measure A/B to capture both slots.", juce::dontSendNotification);
    status.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (status);
}

TransformerMeasurementView::~TransformerMeasurementView() = default;
bool TransformerMeasurementView::isMeasuring() const { return worker->running(); }

void TransformerMeasurementView::startOrCancel()
{
    if (worker->running())
    {
        worker->cancel();
        status.setText ("Cancelling...", juce::dontSendNotification);
        return;
    }
    const auto snapshot = state.read();
    intro.setText ("48 kHz | Transformer only | Match off\nA: " + TransformerVoicingState::describe (snapshot.a, false)
        + "\nB: " + TransformerVoicingState::describe (snapshot.b, false), juce::dontSendNotification);
    if (! worker->start ({ snapshot.a, snapshot.b, exporter ? exporter() : state.toJson() }))
    {
        status.setText ("Could not start measurement worker. Retry.", juce::dontSendNotification);
        return;
    }
    awaiting = true;
    measure.setButtonText ("Cancel");
    copy.setEnabled (false);
}

void TransformerMeasurementView::refresh()
{
    if (worker->running())
    {
        status.setText ("Measuring captured A/B: " + juce::String (worker->progress.load (std::memory_order_relaxed) * 100.0f, 0)
            + "%\nAudio and live controls stay independent.", juce::dontSendNotification);
        return;
    }
    if (awaiting)
    {
        if (auto completed = worker->takeResult())
            result = std::move (completed);
        awaiting = false;
        measure.setButtonText ("Measure A/B");
        copy.setEnabled (result.has_value());
        if (! result)
            status.setText ("Cancelled. Press Measure A/B to retry.", juce::dontSendNotification);
        repaint();
    }
    if (result)
    {
        const auto now = state.read();
        const auto changed = ! TransformerVoicing::equal (now.a, result->request.a)
            || ! TransformerVoicing::equal (now.b, result->request.b);
        status.setText (changed ? "Voicing changed: press Measure A/B again.\nShowing the previous captured settings."
            : "168 points captured | A blue / B amber\nHarmonics in dBc; NA = at/above Nyquist.", juce::dontSendNotification);
    }
}

void TransformerMeasurementView::resized()
{
    auto area = getLocalBounds();
    intro.setBounds (area.removeFromTop (60));
    for (std::size_t i = 0; i < selectors.size(); ++i)
    {
        auto row = area.removeFromTop (24);
        labels[i].setBounds (row.removeFromLeft (64));
        selectors[i].setBounds (row);
        area.removeFromTop (4);
    }
    auto buttons = area.removeFromTop (26);
    measure.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
    buttons.removeFromLeft (6);
    copy.setBounds (buttons);
    area.removeFromTop (4);
    status.setBounds (area.removeFromTop (34));
    gainPlot = area.removeFromTop (108);
    area.removeFromTop (6);
    harmonicPlot = area.removeFromTop (108);
    area.removeFromTop (6);
    readout = area;
}

void TransformerMeasurementView::paint (juce::Graphics& g)
{
    g.setFont (juce::FontOptions (12.0f));
    if (! result)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawFittedText ("Gain versus input level\nand H2 / H3 / H5 / H7\nwill appear here.", gainPlot.getUnion (harmonicPlot), juce::Justification::centred, 3);
        return;
    }
    const auto frequency = selectors[0].getSelectedItemIndex();
    const auto level = selectors[1].getSelectedItemIndex();
    const auto amount = selectors[2].getSelectedItemIndex();
    const auto& selected = result->at (frequency, level, amount);
    g.setColour (juce::Colours::white);
    g.drawText ("Gain (dB) vs input (dBFS)", gainPlot.withHeight (18), juce::Justification::centredLeft);
    auto graph = gainPlot.withTrimmedTop (20).withTrimmedBottom (18).withTrimmedLeft (32).withTrimmedRight (8).toFloat();
    double minimum = -6.0;
    for (int l = 0; l < static_cast<int> (TransformerMeasurement::levels.size()); ++l)
        for (const auto& metrics : result->at (frequency, l, amount).voices)
            minimum = juce::jmin (minimum, std::floor (metrics.fundamentalGainDb / 6.0) * 6.0);
    const auto yForGain = [&] (double db) { return juce::jmap (static_cast<float> (db), static_cast<float> (minimum), 3.0f, graph.getBottom(), graph.getY()); };
    for (double tick : { minimum, 0.0 })
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine (juce::roundToInt (yForGain (tick)), graph.getX(), graph.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String (tick, 0), gainPlot.getX(), juce::roundToInt (yForGain (tick)) - 7, 28, 14, juce::Justification::centredRight);
    }
    for (int voice = 0; voice < 2; ++voice)
    {
        juce::Path path;
        g.setColour (voiceColour (voice));
        for (int l = 0; l < static_cast<int> (TransformerMeasurement::levels.size()); ++l)
        {
            const auto x = juce::jmap (TransformerMeasurement::levels[static_cast<std::size_t> (l)], -60.0f, -6.0f, graph.getX(), graph.getRight());
            const auto y = yForGain (result->at (frequency, l, amount).voices[static_cast<std::size_t> (voice)].fundamentalGainDb);
            if (l == 0) path.startNewSubPath (x, y);
            else path.lineTo (x, y);
            g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
            if (voice == 0)
                g.drawText (juce::String (TransformerMeasurement::levels[static_cast<std::size_t> (l)], 0), juce::roundToInt (x) - 15, gainPlot.getBottom() - 16, 30, 16, juce::Justification::centred);
        }
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }
    g.setColour (juce::Colours::white);
    g.drawText ("Harmonics (dBc) | floor -140", harmonicPlot.withHeight (18), juce::Justification::centredLeft);
    auto bars = harmonicPlot.withTrimmedTop (22).withTrimmedBottom (18).withTrimmedLeft (32).withTrimmedRight (8).toFloat();
    for (float tick : { -140.0f, -70.0f, 0.0f })
    {
        const auto y = juce::jmap (tick, -140.0f, 0.0f, bars.getBottom(), bars.getY());
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine (juce::roundToInt (y), bars.getX(), bars.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.drawText (juce::String (tick, 0), harmonicPlot.getX(), juce::roundToInt (y) - 7, 28, 14, juce::Justification::centredRight);
    }
    for (std::size_t h = 0; h < TransformerMeasurement::harmonics.size(); ++h)
    {
        const auto x = bars.getX() + (static_cast<float> (h) + 0.5f) * bars.getWidth() / 4.0f;
        g.setColour (juce::Colours::white);
        g.drawText ("H" + juce::String (TransformerMeasurement::harmonics[h]), juce::roundToInt (x) - 18, harmonicPlot.getBottom() - 16, 36, 16, juce::Justification::centred);
        for (std::size_t voice = 0; voice < 2; ++voice)
        {
            const auto& metrics = selected.voices[voice];
            if (! metrics.available[h])
                continue;
            const auto y = juce::jmap (juce::jlimit (-140.0f, 0.0f, static_cast<float> (metrics.harmonicDbc[h])), -140.0f, 0.0f, bars.getBottom(), bars.getY());
            g.setColour (voiceColour (static_cast<int> (voice)));
            g.fillRect (x - 9.0f + static_cast<float> (voice) * 10.0f, y, 8.0f, bars.getBottom() - y);
        }
    }
    auto table = readout;
    auto row = table.removeFromTop (18);
    const auto columnWidth = table.getWidth() / 3;
    for (int voice = 0; voice < 2; ++voice)
    {
        g.setColour (voiceColour (voice));
        g.drawText (voice == 0 ? "A" : "B", row.withTrimmedLeft (columnWidth * (voice + 1)).withWidth (columnWidth), juce::Justification::centredRight);
    }
    for (int metric = 0; metric < 6; ++metric)
    {
        row = table.removeFromTop (16);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawText (metric == 0 ? "Gain dB" : metric == 1 ? "RMS dB" : "H" + juce::String (TransformerMeasurement::harmonics[static_cast<std::size_t> (metric - 2)]) + " dBc",
            row.withWidth (columnWidth), juce::Justification::centredLeft);
        for (std::size_t voice = 0; voice < 2; ++voice)
        {
            const auto& metrics = selected.voices[voice];
            const auto text = metric == 0 ? juce::String (metrics.fundamentalGainDb, 2) : metric == 1
                ? juce::String (metrics.rmsGainDb, 2) : harmonicText (metrics, static_cast<std::size_t> (metric - 2));
            g.setColour (voiceColour (static_cast<int> (voice)));
            g.drawText (text, row.withTrimmedLeft (columnWidth * static_cast<int> (voice + 1)).withWidth (columnWidth), juce::Justification::centredRight);
        }
    }
}
