#pragma once

#include "../experimental/TransformerAudioSettings.h"
#include <juce_dsp/juce_dsp.h>

namespace MagneticAudio
{
inline constexpr int capacity = 256;
inline constexpr int maximumLatency = 144;
inline constexpr double stopbandDb = -120.0;
inline constexpr std::array upWidths { 0.04, 0.08, 0.08, 0.08 };
// Slightly longer down filters make each stage's round-trip group delay an
// integer base-rate sample. No fractional-delay allpass is needed for dry mix.
// Up/down orders: 198/198, 98/102, 98/110, 98/126.
inline constexpr std::array downWidths { 0.04, 0.07731219348552776, 0.07167688671527778, 0.06255435552210459 };
inline void prepareFilters (juce::dsp::Oversampling<double>& filters, int factor)
{
    filters.clearOversamplingStages();
    if (factor == 1)
        filters.addDummyOversamplingStage();
    std::size_t index = 0;
    for (int stage = 1; stage < factor; stage *= 2, ++index)
        filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
            static_cast<float> (upWidths[index]), static_cast<float> (stopbandDb),
            static_cast<float> (downWidths[index]), static_cast<float> (stopbandDb));
    filters.setUsingIntegerLatency (false);
    filters.initProcessing (capacity);
}

// Exact integer delay, including signed-zero samples; fixed storage, no locks.
class StereoDelay
{
public:
    void prepare (int samples) noexcept { length = samples; position = 0; data = {}; }
    std::array<float, 2> process (float left, float right) noexcept
    {
        if (length == 0) return { left, right };
        const std::array<float, 2> result { data[0][static_cast<std::size_t> (position)], data[1][static_cast<std::size_t> (position)] };
        data[0][static_cast<std::size_t> (position)] = left;
        data[1][static_cast<std::size_t> (position)] = right;
        if (++position == length) position = 0;
        return result;
    }
private:
    std::array<std::array<float, maximumLatency>, 2> data {};
    int length = 0, position = 0;
};
}
