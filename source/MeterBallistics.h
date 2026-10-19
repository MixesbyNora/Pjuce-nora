#pragma once

#include <algorithm>

// Pure display state: no JUCE UI, processor, timer, or audio-thread interaction.
class MeterBallistics
{
public:
    static constexpr float floorDb = -60.0f;
    static constexpr float ceilingDb = 6.0f;
    static constexpr float releaseDbPerSecond = 24.0f;
    static constexpr double holdSeconds = 1.5;

    void update (float incomingDb, double elapsedSeconds) noexcept
    {
        const auto elapsed = std::max (0.0, elapsedSeconds);
        const auto target = std::clamp (incomingDb, floorDb, ceilingDb);
        const auto fall = releaseDbPerSecond * static_cast<float> (elapsed);
        levelDb = std::max (target, levelDb - fall);
        if (target >= peakDb)
        {
            peakDb = target;
            holdRemaining = holdSeconds;
        }
        else
        {
            const auto fallingSeconds = std::max (0.0, elapsed - holdRemaining);
            holdRemaining = std::max (0.0, holdRemaining - elapsed);
            peakDb = std::max (levelDb,
                peakDb - releaseDbPerSecond * static_cast<float> (fallingSeconds));
        }
    }

    float getLevelDb() const noexcept { return levelDb; }
    float getPeakDb() const noexcept { return peakDb; }

private:
    float levelDb = floorDb;
    float peakDb = floorDb;
    double holdRemaining = 0.0;
};
