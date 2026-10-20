#include "TransformerSaturation.h"
#include <juce_dsp/juce_dsp.h>

double TransformerSaturation::driveDbFor (float amountPercent) noexcept
{
    return TransformerSettings::minimumDriveDb
        + (TransformerSettings::maximumDriveDb - TransformerSettings::minimumDriveDb)
            * static_cast<double> (amountPercent) / 100.0;
}

void TransformerSaturation::prepare (double sampleRate, float amountPercent, bool out) noexcept
{
    const auto raw = juce::dsp::IIR::ArrayCoefficients<double>::makeLowShelf (sampleRate,
        TransformerSettings::shelfFrequencyHz, TransformerSettings::shelfQ,
        juce::Decibels::decibelsToGain (TransformerSettings::shelfGainDb));
    preEmphasis = { raw[0] / raw[3], raw[1] / raw[3], raw[2] / raw[3], raw[4] / raw[3], raw[5] / raw[3] };
    // Algebraic reciprocal: exactly the -gain shelf at the same frequency/Q,
    // without independent coefficient rounding between the inverse pair.
    const auto inverseB0 = 1.0 / preEmphasis.b0;
    deEmphasis = { inverseB0, preEmphasis.a1 * inverseB0, preEmphasis.a2 * inverseB0,
        preEmphasis.b1 * inverseB0, preEmphasis.b2 * inverseB0 };
    state = {};
    wasProcessing = false;
    driveDb.reset (sampleRate, TransformerSettings::amountRampSeconds);
    active.reset (sampleRate, TransformerSettings::bypassRampSeconds);
    driveDb.setCurrentAndTargetValue (driveDbFor (amountPercent));
    active.setCurrentAndTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}

void TransformerSaturation::setParameters (float amountPercent, bool out) noexcept
{
    driveDb.setTargetValue (driveDbFor (amountPercent));
    active.setTargetValue (amountPercent > 0.0f && ! out ? 1.0f : 0.0f);
}

void TransformerSaturation::process (juce::AudioBuffer<float>& buffer, int channels) noexcept
{
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto currentDriveDb = driveDb.getNextValue();
        const auto amount = active.getNextValue();
        if (amount <= 0.0f)
        {
            if (wasProcessing)
                state = {};
            wasProcessing = false;
            continue; // Exact untouched samples: no filter or nonlinearity work.
        }
        wasProcessing = true;
        const auto drive = juce::Decibels::decibelsToGain (currentDriveDb);
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& s = state[static_cast<std::size_t> (channel)];
            const auto original = buffer.getSample (channel, sample);
            const auto emphasized = DoubleBiquad::processSample (original, preEmphasis, s.pre);
            const auto driven = drive * emphasized;
            if (! s.primed)
            {
                s.previousDriven = driven;
                s.primed = true;
            }
            // ADAA of the nonlinear residual tanh(u)-u, plus an undelayed
            // linear path. This avoids ADAA's small-signal two-sample low-pass.
            const auto average = driven * 0.5 + s.previousDriven * 0.5;
            const auto saturated = emphasized + (TanhADAA::evaluate (driven, s.previousDriven) - average) / drive;
            s.previousDriven = driven;
            const auto processed = static_cast<float> (DoubleBiquad::processSample (saturated, deEmphasis, s.post));
            buffer.setSample (channel, sample, amount >= 1.0f ? processed : original + amount * (processed - original));
        }
    }
}
