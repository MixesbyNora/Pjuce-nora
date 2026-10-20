#pragma once

#include "MagneticAnalysis.h"
#include "MagneticIntervalCore.h"
#include "MagneticTurningPointCore.h"
#include "MagneticQuarticCore.h"
#include <juce_dsp/juce_dsp.h>

// Test/offline harness only. No live plugin target references this file.
namespace MagneticOversampling
{
inline constexpr std::array factors { 1, 2, 4, 8, 16 };
inline constexpr float firstTransition = 0.04f;
inline constexpr float laterTransition = 0.08f;
inline constexpr float stopbandDb = -120.0f;
inline constexpr int blockSize = 256;
enum class Evaluation { point, interval, turningPoint, quartic };
enum class TimingInput { slowSine, quietHighSine, noise };

class Processor
{
public:
    Processor (int factor, int channels = 1, int capacity = blockSize,
        MagneticPrototype::PlayPreisachCore::Parameters parameters = {}, Evaluation = Evaluation::point);
    void reset();
    void resetCores();
    void process (juce::AudioBuffer<double>&, bool identity = false) noexcept;
    double latency() const noexcept { return filters.getLatencyInSamples() + (evaluation == Evaluation::interval ? 0.5 / static_cast<double> (factor()) : evaluation == Evaluation::quartic ? 2.0 / static_cast<double> (factor()) : 0.0); }
    std::size_t factor() const noexcept { return filters.getOversamplingFactor(); }

private:
    juce::dsp::Oversampling<double> filters;
    std::array<MagneticPrototype::PlayPreisachCore, 2> cores;
    std::array<IntervalCore, 2> intervalCores;
    std::array<TurningPointCore, 2> turningPointCores;
    std::array<QuarticCore, 2> quarticCores;
    std::array<std::array<double, 2>, 2> identityDelay {};
    std::array<double, 2> linearPrevious {};
    Evaluation evaluation;
};

struct Render
{
    MagneticAnalysis::Spectrum spectrum;
    double latencySamples = 0.0;
};
struct Comparison
{
    int factor = 1;
    double aliasDbr = 0.0, alignedResidualDbr = 0.0, passbandResidualDbr = 0.0;
    double gainErrorDb = 0.0, phaseErrorDegrees = 0.0, latencySamples = 0.0;
    double audioBandAliasDbr = 0.0, peakAliasHz = 0.0;
};
struct Case
{
    double sampleRate = 0.0, requestedFrequency = 0.0, frequency = 0.0, fieldPeak = 0.0;
    double referenceInitialDbr = 0.0, referenceConvergenceDbr = 0.0;
    int referenceFactor = 64;
    std::array<Comparison, factors.size()> comparisons;
};
struct Filter
{
    double latencySamples = 0.0;
    std::vector<double> impulse;
};
struct Response
{
    double gainDb = 0.0, delayRemovedPhaseDegrees = 0.0;
};

Render renderTone (int factor, int bin, double peak, MagneticPrototype::PlayPreisachCore::Parameters = {}, Evaluation = Evaluation::point);
Case compare (double sampleRate, double requestedFrequency, double peak, Evaluation = Evaluation::point);
Filter measureFilter (int factor, Evaluation = Evaluation::point);
Response responseAt (const Filter&, double sampleRate, double frequency);
std::array<double, 2> timing (int factor, Evaluation = Evaluation::point, TimingInput = TimingInput::slowSine); // Median ns/base sample and checksum; includes filters/core/copies.
}
