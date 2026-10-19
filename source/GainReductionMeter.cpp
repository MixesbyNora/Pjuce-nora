#include "GainReductionMeter.h"

void GainReductionMeter::setGainReductionDb (float reductionDb)
{
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto elapsedSeconds = juce::jlimit (0.0, 0.2, (nowMs - lastUpdateMs) * 0.001);
    lastUpdateMs = nowMs;
    currentDb = juce::jmax (0.0f, reductionDb);

    const auto timeConstant = currentDb > displayedDb ? 0.025 : 0.25;
    const auto coefficient = static_cast<float> (std::exp (-elapsedSeconds / timeConstant));
    displayedDb = currentDb + coefficient * (displayedDb - currentDb);

    if (currentDb >= peakDb)
    {
        peakDb = currentDb;
        peakHoldUntilMs = nowMs + 1000.0;
    }
    else if (nowMs > peakHoldUntilMs)
    {
        const auto peakCoefficient = static_cast<float> (std::exp (-elapsedSeconds / 0.5));
        peakDb = juce::jmax (displayedDb,
            displayedDb + peakCoefficient * (peakDb - displayedDb));
    }

    repaint();
}

void GainReductionMeter::paint (juce::Graphics& graphics)
{
    graphics.setColour (juce::Colour (0xff20242a));
    graphics.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    graphics.setColour (juce::Colour (0xffdce2e8));
    graphics.setFont (juce::FontOptions (13.0f));
    graphics.drawText ("Gain reduction", 8, 12, getWidth() - 16, 20, juce::Justification::centred);

    const juce::Rectangle<float> track (32.0f, 48.0f, 38.0f, static_cast<float> (getHeight() - 108));
    const auto positionForDb = [&track] (float db)
    {
        return track.getY() + track.getHeight() * juce::jlimit (0.0f, 20.0f, db) / 20.0f;
    };

    graphics.setColour (juce::Colour (0xff11151a));
    graphics.fillRect (track);
    graphics.setColour (juce::Colour (0xff68c6ad));
    graphics.fillRect (track.withHeight (positionForDb (displayedDb) - track.getY()));
    graphics.setColour (juce::Colour (0xff53606c));
    graphics.drawRect (track, 1.0f);

    graphics.setColour (juce::Colour (0xffdce2e8));
    graphics.setFont (juce::FontOptions (12.0f));
    for (int db = 0; db <= 20; db += 5)
    {
        const auto y = positionForDb (static_cast<float> (db));
        graphics.drawLine (track.getRight() + 5.0f, y, track.getRight() + 13.0f, y, 1.0f);
        graphics.drawText (juce::String (db), 90, juce::roundToInt (y) - 9, 38, 18,
            juce::Justification::centredLeft);
    }

    // Keep the hold line within the border at the 0 and 20 dB endpoints.
    const auto peakY = juce::jlimit (track.getY() + 1.0f, track.getBottom() - 1.0f, positionForDb (peakDb));
    graphics.setColour (juce::Colour (0xffffd184));
    graphics.drawLine (track.getX() + 1.0f, peakY, track.getRight() - 1.0f, peakY, 1.0f);

    graphics.setColour (juce::Colour (0xfff2f5f8));
    graphics.setFont (juce::FontOptions (20.0f));
    graphics.drawText (juce::String (currentDb, 1) + " dB", 8, getHeight() - 42,
        getWidth() - 16, 28, juce::Justification::centred);
}
