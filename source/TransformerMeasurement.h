#pragma once

#include "TransformerVoicing.h"
#include <juce_core/juce_core.h>
#include <array>
#include <functional>
#include <optional>

// Offline transformer-only measurements. Never called by the audio callback.
namespace TransformerMeasurement
{
inline constexpr double sampleRate = 48000.0;
inline constexpr int warmupSamples = 12000;
inline constexpr int analysisSamples = 48000;
inline constexpr std::array frequencies { 20.0, 50.0, 60.0, 200.0, 1000.0, 5000.0, 8000.0 };
inline constexpr std::array levels { -60.0f, -24.0f, -12.0f, -6.0f };
inline constexpr std::array amounts { 25.0f, 50.0f, 100.0f };
inline constexpr std::array harmonics { 2, 3, 5, 7 };
inline constexpr std::size_t rowCount = frequencies.size() * levels.size() * amounts.size();
using Cancel = std::function<bool()>;

struct Metrics
{
    double fundamentalGainDb = 0.0, rmsGainDb = 0.0;
    std::array<double, 4> harmonicDbc {};
    std::array<bool, 4> available {};
};
struct Request
{
    TransformerVoicing::Values a, b;
    juce::String contextJson;
};
struct Row
{
    double frequencyHz = 0.0;
    float inputDbfs = 0.0f, amountPercent = 0.0f;
    std::array<Metrics, 2> voices;
};
struct Result
{
    Request request;
    std::array<Row, rowCount> rows;
    const Row& at (int frequency, int level, int amount) const noexcept;
};

std::optional<Metrics> measurePoint (TransformerVoicing::Values, double frequencyHz,
    float inputDbfs, float amountPercent, const Cancel& cancel = {});
std::optional<Result> measure (const Request&, const Cancel& cancel = {},
    const std::function<void (float)>& progress = {});
juce::String toCsv (const Result&);
}
