#include "SidechainFilter.h"
#include <juce_dsp/juce_dsp.h>

void SidechainFilter::prepare (double sampleRate) noexcept
{
    using Factory = juce::dsp::IIR::ArrayCoefficients<double>;
    using Type = SidechainFilterSettings::Type;
    for (std::size_t mode = 0; mode < coefficients.size(); ++mode)
    {
        for (std::size_t stage = 0; stage < coefficients[mode].size(); ++stage)
        {
            const auto& settings = SidechainFilterSettings::modes[mode].stages[stage];
            std::array<double, 6> raw { 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };
            const auto gain = juce::Decibels::decibelsToGain (settings.gainDb);
            switch (settings.type)
            {
                case Type::bypass: break;
                case Type::highPass: raw = Factory::makeHighPass (sampleRate, settings.frequencyHz, settings.q); break;
                case Type::bandPass: raw = Factory::makeBandPass (sampleRate, settings.frequencyHz, settings.q); break;
                case Type::lowShelf: raw = Factory::makeLowShelf (sampleRate, settings.frequencyHz, settings.q, gain); break;
                case Type::highShelf: raw = Factory::makeHighShelf (sampleRate, settings.frequencyHz, settings.q, gain); break;
                case Type::peak: raw = Factory::makePeakFilter (sampleRate, settings.frequencyHz, settings.q, gain); break;
            }
            const auto inverseA0 = 1.0 / raw[3];
            coefficients[mode][stage] = { raw[0] * inverseA0, raw[1] * inverseA0,
                raw[2] * inverseA0, raw[4] * inverseA0, raw[5] * inverseA0 };
        }
    }
    state = {};
    activeMode = SidechainFilterSettings::defaultMode;
}

int SidechainFilter::boundedMode (int mode) noexcept
{
    return juce::jlimit (0, static_cast<int> (SidechainFilterSettings::modes.size()) - 1, mode);
}

void SidechainFilter::setMode (int mode) noexcept
{
    const auto newMode = boundedMode (mode);
    if (newMode != activeMode)
    {
        activeMode = newMode;
        state = {};
    }
}

double SidechainFilter::processSample (int channel, double sample) noexcept
{
    if (activeMode == SidechainFilterSettings::defaultMode)
        return sample;

    jassert (channel >= 0 && channel < static_cast<int> (state.size()));
    auto& channelState = state[static_cast<std::size_t> (channel)];
    const auto& mode = coefficients[static_cast<std::size_t> (activeMode)];
    for (std::size_t stage = 0; stage < mode.size(); ++stage)
    {
        sample = DoubleBiquad::processSample (sample, mode[stage], channelState[stage]);
    }
    return sample;
}

const SidechainFilter::ModeCoefficients& SidechainFilter::getCoefficients (int mode) const noexcept
{
    return coefficients[static_cast<std::size_t> (boundedMode (mode))];
}
