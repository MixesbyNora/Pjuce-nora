#pragma once

#include "../CompressorParameters.h"
#include <juce_audio_basics/juce_audio_basics.h>

class DbGainEnvelope
{
public:
    void prepare (double sampleRate) noexcept
    {
        fillCoefficients (attackCoefficients, CompressorParameters::attack, sampleRate);
        minReleaseMs = 1000.0 / sampleRate;
        gainReductionDb = 0.0;
    }

    void setTimes (float attackMs, float releaseMs) noexcept
    {
        attackCoefficient = attackCoefficients[indexFor (attackMs, CompressorParameters::attack)];
        releaseTimeMs = juce::jmax (minReleaseMs, static_cast<double> (releaseMs));
    }

    float process (float targetReductionDb) noexcept
    {
        auto coefficient = attackCoefficient;
        if (static_cast<double> (targetReductionDb) < gainReductionDb)
        {
            const auto depth = juce::jlimit (0.0, 1.0,
                gainReductionDb / CompressorParameters::fullSpeedGrDb);
            const auto effectiveMs = juce::jlimit (minReleaseMs, releaseTimeMs,
                releaseTimeMs * std::pow (minReleaseMs / releaseTimeMs, depth));
            // Calculated only while releasing. The user's Release is the
            // slowest time constant; deep reduction can reach one sample.
            coefficient = std::exp (-minReleaseMs / effectiveMs);
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
    double minReleaseMs = 1.0;
    double releaseTimeMs = CompressorParameters::release.defaultValue;
    double gainReductionDb = 0.0;
};
