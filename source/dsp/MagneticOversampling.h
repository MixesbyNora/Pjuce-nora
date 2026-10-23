#pragma once

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
// Live design (owner decision 2026-10-23, docs/OversamplingOptions.md): relaxed
// linear-phase half-band FIRs, -100 dB stopband, flat to 20 kHz at every
// supported rate. Anything that folds back on decimation lands above 20 kHz.
// Down widths are narrowed slightly so each round trip is a whole base-rate
// sample, keeping dry, Mix and Delta sample-exact. Factors follow
// vtb1148OutputFactor: 4x / 2x / 1x.
inline constexpr float relaxedStopbandDb = -100.0f;
struct RelaxedStage { float upWidth, downWidth; };
struct RelaxedDesign
{
    double rate;
    int latency;
    std::array<RelaxedStage, 2> stages; // Unused stages have zero width.
};
inline constexpr std::array relaxedDesigns {
    RelaxedDesign { 44100.0, 78, { RelaxedStage { 0.046485260770975034f, 0.046485260770975034f }, RelaxedStage { 0.2f, 0.181f } } },
    RelaxedDesign { 48000.0, 48, { RelaxedStage { 0.083333333333333315f, 0.083333333333333315f }, RelaxedStage { 0.2f, 0.181f } } },
    RelaxedDesign { 88200.0, 11, { RelaxedStage { 0.27324263038548752f, 0.27324263038548752f }, RelaxedStage { 0.0f, 0.0f } } },
    RelaxedDesign { 96000.0, 11, { RelaxedStage { 0.29166666666666663f, 0.29166666666666663f }, RelaxedStage { 0.0f, 0.0f } } },
};
inline const RelaxedDesign* relaxedDesignFor (double sampleRate) noexcept
{
    for (const auto& design : relaxedDesigns)
        if (std::abs (design.rate - sampleRate) < 0.5)
            return &design;
    return nullptr;
}
// The original steep design (passband to about 22 kHz, -120 dB); kept for
// non-standard rates and the research harness.
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
// Live filters for a sample rate: the relaxed design at 44.1/48/88.2/96 kHz,
// the original design at other rates (176.4 kHz and up run at 1x either way).
inline void prepareLiveFilters (juce::dsp::Oversampling<double>& filters, int factor, double sampleRate)
{
    const auto* design = relaxedDesignFor (sampleRate);
    if (design == nullptr || factor == 1)
    {
        prepareFilters (filters, factor);
        return;
    }
    filters.clearOversamplingStages();
    std::size_t index = 0;
    for (int stage = 1; stage < factor; stage *= 2, ++index)
    {
        const auto& s = design->stages[index];
        jassert (s.upWidth > 0.0f);
        filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
            s.upWidth, relaxedStopbandDb, s.downWidth, relaxedStopbandDb);
    }
    filters.setUsingIntegerLatency (false);
    filters.initProcessing (capacity);
    jassert (juce::roundToInt (filters.getLatencyInSamples()) == design->latency);
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
