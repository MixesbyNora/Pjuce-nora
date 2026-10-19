#pragma once

#include "../CompressorParameters.h"
#include <juce_audio_basics/juce_audio_basics.h>

class DbGainEnvelope
{
public:
    void prepare (double sampleRate) noexcept
    {
        fillCoefficients (attackCoefficients, CompressorParameters::attack, sampleRate);
        fillCoefficients (releaseCoefficients, CompressorParameters::release, sampleRate);
        gainReductionDb = 0.0;
    }

    void setTimes (float attackMs, float releaseMs) noexcept
    {
        attackCoefficient = attackCoefficients[indexFor (attackMs, CompressorParameters::attack)];
        releaseCoefficient = releaseCoefficients[indexFor (releaseMs, CompressorParameters::release)];
    }

    float process (float targetReductionDb) noexcept
    {
        const auto coefficient = static_cast<double> (targetReductionDb) > gainReductionDb
            ? attackCoefficient : releaseCoefficient;
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
            // One time constant reaches 1 - exp(-1) of an attack, or exp(-1) of a release.
            table[i] = std::exp (-1.0 / (sampleRate * milliseconds * 0.001));
        }
    }

    std::array<double, CompressorParameters::stepCount (CompressorParameters::attack)> attackCoefficients {};
    std::array<double, CompressorParameters::stepCount (CompressorParameters::release)> releaseCoefficients {};
    double attackCoefficient = 0.0;
    double releaseCoefficient = 0.0;
    double gainReductionDb = 0.0;
};
