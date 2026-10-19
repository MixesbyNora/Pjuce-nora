#include "LowFrequencyMono.h"
#include <juce_dsp/juce_dsp.h>

void LowFrequencyMono::prepare (double sampleRate, float initialHz) noexcept
{
    rate = sampleRate;
    state = {};
    enabled = initialHz >= BaParameters::lfm.offBelow;
    cutoff.reset (rate, BaParameters::lfmRampSeconds);
    blend.reset (rate, BaParameters::lfmRampSeconds);
    cutoff.setCurrentAndTargetValue (juce::jmax (static_cast<double> (BaParameters::lfm.offBelow), static_cast<double> (initialHz)));
    blend.setCurrentAndTargetValue (enabled ? 1.0 : 0.0);
    updateCoefficients (cutoff.getCurrentValue());
    coefficientsNeedUpdate = false;
}

void LowFrequencyMono::setCutoff (float hertz) noexcept
{
    const auto newEnabled = hertz >= BaParameters::lfm.offBelow;
    if (newEnabled && ! enabled)
        coefficientsNeedUpdate = true;
    if (newEnabled && ! enabled && blend.getCurrentValue() <= 0.0)
        state = {};
    enabled = newEnabled;
    cutoff.setTargetValue (juce::jmax (static_cast<double> (BaParameters::lfm.offBelow), static_cast<double> (hertz)));
    blend.setTargetValue (enabled ? 1.0 : 0.0);
}

void LowFrequencyMono::updateCoefficients (double hertz) noexcept
{
    const auto c = juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass (rate, hertz, BaParameters::lfmQ);
    coefficients = { c[0], c[1], c[2], c[4], c[5] }; // high-pass factory already normalizes a0 to 1
}

void LowFrequencyMono::process (float& left, float& right) noexcept
{
    const auto updatingCutoff = cutoff.isSmoothing();
    const auto hertz = cutoff.getNextValue();
    const auto amount = blend.getNextValue();
    if (amount <= 0.0)
        return; // Exact Off path: no M/S round-trip.
    if (updatingCutoff || coefficientsNeedUpdate)
    {
        updateCoefficients (hertz);
        coefficientsNeedUpdate = false;
    }
    const auto mid = (static_cast<double> (left) + right) * 0.5;
    const auto side = (static_cast<double> (left) - right) * 0.5;
    const auto filteredSide = DoubleBiquad::processSample (side, coefficients, state);
    const auto blendedSide = amount >= 1.0 ? filteredSide : side + amount * (filteredSide - side);
    left = static_cast<float> (mid + blendedSide);
    right = static_cast<float> (mid - blendedSide);
}
