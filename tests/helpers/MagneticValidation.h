#pragma once

#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <vector>

// Offline validation stimuli, alignment and spectral metrics. The Magnetic
// engine's reference renders that used to live here are in archive/magnetic-engine.
namespace MagneticValidation
{
inline constexpr int fftOrder = 14;
inline constexpr int windowSize = 1 << fftOrder;
inline constexpr int alignmentRadius = 128;
inline constexpr double referenceGoalDb = -100.0;
inline constexpr double residualScreenDb = -80.0;

enum class Signal { lowHigh, closeHigh, sixTone, impulse, lowBurst, highBurst, gaussian, bipolar, fastDecay };
inline constexpr std::array signals { Signal::lowHigh, Signal::closeHigh, Signal::sixTone, Signal::impulse,
    Signal::lowBurst, Signal::highBurst, Signal::gaussian, Signal::bipolar, Signal::fastDecay };
const char* name (Signal) noexcept;

struct Stimulus
{
    Stimulus (Signal, double sampleRate, double peak);
    double at (double baseSample) const noexcept;
    bool periodic() const noexcept { return signal <= Signal::sixTone; }
    bool silentTail() const noexcept { return ! periodic() && signal != Signal::impulse; }
    Signal signal;
    double rate = 48000.0, peak = 1.0, decayScale = 1.0;
    std::array<int, 6> bins {};
    std::array<double, 6> weights {}, phases {};
    int toneCount = 0;
};

struct Trace
{
    std::vector<double> samples;
    double latencySamples = 0.0;
};
struct Metrics
{
    double bandResidualDb = 0.0, fullResidualDb = 0.0, offGridDb = 0.0;
    double peakErrorDb = 0.0, maximumError = 0.0, tailErrorDb = 0.0, tailDriftDb = 0.0;
    double bandPeakErrorDb = 0.0, bandMaximumError = 0.0;
    double identityResidualDb = 0.0;
};
// Only offline/test allocations. No new live processing, parameters or UI.
std::vector<std::complex<double>> spectrum (const std::vector<double>&);
std::vector<double> audioBand (const std::vector<double>&, double sampleRate);
double alignmentErrorDb (double fractionalDelay, double cyclesPerSample);
Metrics measure (const Trace&, const Trace&, const Stimulus&);
Trace alignTrace (const std::vector<double>& raw, int start, double latency, int length = windowSize);
}
