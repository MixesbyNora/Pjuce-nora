#pragma once

#include "../CompressorParameters.h"
#include <juce_audio_basics/juce_audio_basics.h>

class DbGainEnvelope
{
public:
    void prepare (double sampleRate) noexcept
    {
        fillCoefficients (attackCoefficients, CompressorParameters::attack, sampleRate);
        sampleMs = 1000.0 / sampleRate;
        gainReductionDb = 0.0;
    }

    void setTimes (float attackMs, float releaseMs) noexcept
    {
        attackCoefficient = attackCoefficients[indexFor (attackMs, CompressorParameters::attack)];
        releaseTimeMs = juce::jmax (CompressorParameters::fastestReleaseMs, static_cast<double> (releaseMs));
    }
    // 0 = the Release time always; 1 = full program dependence.
    void setReleaseMod (float amount) noexcept { releaseMod = juce::jlimit (0.0, 1.0, static_cast<double> (amount)); }
    // Continue from a gain reduction reached elsewhere (a detector switch).
    void setGainReductionDb (float reductionDb) noexcept { gainReductionDb = static_cast<double> (reductionDb); }

    float process (float targetReductionDb) noexcept
    {
        auto coefficient = attackCoefficient;
        if (static_cast<double> (targetReductionDb) < gainReductionDb)
        {
            const auto depth = juce::jlimit (0.0, 1.0,
                gainReductionDb / CompressorParameters::fullSpeedGrDb) * releaseMod;
            const auto fastest = CompressorParameters::fastestReleaseMs;
            const auto effectiveMs = juce::jlimit (fastest, releaseTimeMs,
                releaseTimeMs * std::pow (fastest / releaseTimeMs, depth));
            // Calculated only while releasing. The user's Release is the slowest
            // time constant; deep reduction with Release Mod reaches 20 ms at most.
            coefficient = std::exp (-sampleMs / effectiveMs);
        }
        gainReductionDb = targetReductionDb + coefficient * (gainReductionDb - targetReductionDb);
        return getGainReductionDb();
    }

    float getGainReductionDb() const noexcept
    {
        return static_cast<float> (gainReductionDb);
    }

private:
    static std::size_t indexFor (float milliseconds, const CompressorParameters::Specification& spec) noexcept
    {
        const auto bounded = juce::jlimit (spec.minimum, spec.maximum, milliseconds);
        return static_cast<std::size_t> (juce::roundToInt ((bounded - spec.minimum) / spec.step));
    }

    template <std::size_t Size>
    static void fillCoefficients (std::array<double, Size>& table,
                                 const CompressorParameters::Specification& spec,
                                 double sampleRate) noexcept
    {
        for (std::size_t i = 0; i < Size; ++i)
        {
            const auto milliseconds = static_cast<double> (spec.minimum) + static_cast<double> (i) * spec.step;
            // One attack time constant reaches 1 - exp(-1) of the target.
            table[i] = std::exp (-1.0 / (sampleRate * milliseconds * 0.001));
        }
    }

    std::array<double, CompressorParameters::stepCount (CompressorParameters::attack)> attackCoefficients {};
    double attackCoefficient = 0.0;
    double sampleMs = 1.0;
    double releaseMod = CompressorParameters::releaseMod.defaultValue * 0.01;
    double releaseTimeMs = CompressorParameters::release.defaultValue;
    double gainReductionDb = 0.0;
};
