#include "StereoLevelMeter.h"

StereoLevelMeter::StereoLevelMeter (juce::String titleToUse) : title (std::move (titleToUse))
{
    setName (title);
}

void StereoLevelMeter::setLevelsDb (float leftDb, float rightDb)
{
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = juce::jmax (0.0, (nowMs - lastUpdateMs) * 0.001);
    lastUpdateMs = nowMs;
    ballistics[0].update (leftDb, elapsed);
    ballistics[1].update (rightDb, elapsed);
    repaint();
}

void StereoLevelMeter::setChannelLabels (juce::String leftLabel, juce::String rightLabel)
{
    labels = { std::move (leftLabel), std::move (rightLabel) };
    repaint();
}

void StereoLevelMeter::paint (juce::Graphics& graphics)
{
    graphics.setColour (juce::Colour (0xff20242a));
    graphics.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    graphics.setColour (juce::Colour (0xfff2f5f8));
    graphics.setFont (juce::FontOptions (13.0f));
    graphics.drawText (title, 10, 26, 34, 36, juce::Justification::centredLeft);

    const auto trackX = 82.0f;
    const auto trackWidth = static_cast<float> (getWidth()) - trackX - 16.0f;
    const auto xForDb = [trackX, trackWidth] (float db)
    {
        return trackX + trackWidth * (juce::jlimit (-60.0f, 6.0f, db) + 60.0f) / 66.0f;
    };
    constexpr std::array ticks { -60, -48, -36, -24, -12, -6, 0, 6 };
    graphics.setFont (juce::FontOptions (10.0f));
    for (int db : ticks)
    {
        const auto text = db > 0 ? "+" + juce::String (db) : juce::String (db);
        graphics.drawText (text, juce::roundToInt (xForDb (static_cast<float> (db))) - 16, 4,
            32, 16, juce::Justification::centred);
    }

    constexpr std::array ends { -12.0f, 0.0f, 6.0f };
    const std::array colours { juce::Colour (0xff68c6ad), juce::Colour (0xffe9c85f), juce::Colour (0xffec6a69) };
    for (std::size_t channel = 0; channel < ballistics.size(); ++channel)
    {
        const auto y = 28.0f + 24.0f * static_cast<float> (channel);
        const juce::Rectangle<float> track (trackX, y, trackWidth, 12.0f);
        graphics.setColour (juce::Colour (0xffdce2e8));
        graphics.setFont (juce::FontOptions (11.0f));
        graphics.drawText (labels[channel], 50, juce::roundToInt (y) - 3, 24, 18, juce::Justification::centred);
        graphics.setColour (juce::Colour (0xff11151a));
        graphics.fillRect (track);
        const auto levelX = xForDb (ballistics[channel].getLevelDb());
        auto beginX = trackX;
        for (std::size_t segment = 0; segment < ends.size(); ++segment)
        {
            const auto endX = xForDb (ends[segment]);
            const auto width = juce::jmax (0.0f, juce::jmin (levelX, endX) - beginX);
            graphics.setColour (colours[segment]);
            graphics.fillRect (juce::Rectangle<float> (beginX, y, width, track.getHeight()));
            beginX = endX;
        }
        graphics.setColour (juce::Colour (0xff53606c).withAlpha (0.4f));
        for (int db : ticks)
        {
            const auto x = xForDb (static_cast<float> (db));
            graphics.drawLine (x, y, x, track.getBottom(), 1.0f);
        }
        graphics.setColour (juce::Colour (0xfff2f5f8));
        const auto peakX = juce::jlimit (track.getX() + 0.5f, track.getRight() - 0.5f,
            xForDb (ballistics[channel].getPeakDb()));
        graphics.drawLine (peakX, y - 1.0f, peakX, track.getBottom() + 1.0f, 1.0f);
    }
}
