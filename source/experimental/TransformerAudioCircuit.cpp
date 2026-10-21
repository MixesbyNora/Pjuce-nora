#include "TransformerAudioCircuit.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace MagneticPrototype
{
namespace
{
bool finite (double x) noexcept { return (std::bit_cast<std::uint64_t> (x) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL; }
bool sameValue (double a, double b) noexcept { return std::bit_cast<std::uint64_t> (a) == std::bit_cast<std::uint64_t> (b); }
double bounded (double x, double lo, double hi, double fallback) noexcept { return finite (x) ? std::clamp (x, lo, hi) : fallback; }
}
void TransformerAudioCircuit::prepare (double rate, Parameters values, bool linearReference, bool exactConstitutive) noexcept
{
    rate = bounded (rate, AudioSettings::minimumRate, AudioSettings::maximumRate, 48000.0);
    preparedRate = rate;
    parameters.cornerHz = bounded (values.cornerHz, AudioSettings::minimumCornerHz, AudioSettings::maximumCornerHz, AudioSettings::cornerHz);
    parameters.driveDb = bounded (values.driveDb, AudioSettings::minimumDriveDb, AudioSettings::maximumDriveDb, AudioSettings::driveDb);
    parameters.magnetizationWeight = bounded (values.magnetizationWeight, 0.0, AudioSettings::maximumMagnetizationWeight, AudioSettings::magnetizationWeight);
    core.prepare (values.core);
    parameters.core = core.getParameters();
    linearSlope = 1.0 + parameters.magnetizationWeight * (1.0 - parameters.core.memory) / Settings::saturationField;
    tau = 1.0 / (2.0 * std::numbers::pi * parameters.cornerHz * linearSlope);
    q = 1.0 / (2.0 * rate * tau);
    drive = AudioSettings::fieldScale * std::pow (10.0, parameters.driveDb / 20.0);
    linear = linearReference;
    exact = exactConstitutive;
    reset();
}
void TransformerAudioCircuit::configureMagnetics (double weight, double memory, double cornerHz) noexcept
{
    parameters.magnetizationWeight = bounded (weight, 0.0, AudioSettings::maximumMagnetizationWeight, AudioSettings::magnetizationWeight);
    parameters.core.memory = bounded (memory, Settings::minimumMemory, Settings::maximumMemory, Settings::defaultMemory);
    parameters.cornerHz = bounded (cornerHz, AudioSettings::minimumCornerHz, AudioSettings::maximumCornerHz, AudioSettings::cornerHz);
    core.parameters.memory = parameters.core.memory;
    linearSlope = 1.0 + parameters.magnetizationWeight * (1.0 - parameters.core.memory) / Settings::saturationField;
    tau = 1.0 / (2.0 * std::numbers::pi * parameters.cornerHz * linearSlope);
    q = 1.0 / (2.0 * preparedRate * tau);
    reset();
}
void TransformerAudioCircuit::reset() noexcept
{
    core.reset();
    field = magnetization = previousVoltage = 0.0;
    committedValues.fill (0.0);
    committedReversible = committedClippedField = committedTrialMagnetization = 0.0;
    diagnostics = {};
    lastSlope = (1.0 - parameters.core.memory) / Settings::saturationField;
}
TransformerAudioCircuit::Trial TransformerAudioCircuit::evaluateTrial (double h) const noexcept
{
    if (! finite (h))
        return { core.getMagnetization(), 0.0 };
    Evaluation evaluation;
    evaluate (h, evaluation);
    return evaluation.trial;
}
void TransformerAudioCircuit::evaluate (double h, Evaluation& evaluation) const noexcept
{
    const auto clipped = std::clamp (h, -Settings::maximumField, Settings::maximumField);
    const auto scale = Settings::saturationField;
    if (sameValue (clipped, committedClippedField))
    {
        // The initial Newton trial normally repeats the last accepted field.
        // Every play coordinate is inside its closed interval at that field,
        // so only the reversible term contributes to the strict branch slope.
        evaluation.positions = core.getPlayStates();
        evaluation.values = committedValues;
        evaluation.reversible = committedReversible;
        evaluation.trial = { committedTrialMagnetization, std::abs (h) > Settings::maximumField ? 0.0
            : (1.0 - parameters.core.memory) * (1.0 - committedReversible * committedReversible) / scale };
        return;
    }
    const auto reversible = exact ? std::tanh (clipped / scale) : density.value (clipped / scale);
    evaluation.reversible = reversible;
    auto& result = evaluation.trial;
    result = { (1.0 - parameters.core.memory) * reversible,
        (1.0 - parameters.core.memory) * (1.0 - reversible * reversible) / scale };
    for (std::size_t i = 0; i < Settings::operatorCount; ++i)
    {
        const auto old = core.getPlayStates()[i];
        const auto radius = core.getRadii()[i];
        const auto position = std::clamp (old, clipped - radius, clipped + radius);
        const auto value = sameValue (position, old) ? committedValues[i]
            : exact ? std::tanh (position / scale) : density.value (position / scale);
        evaluation.positions[i] = position;
        evaluation.values[i] = value;
        result.magnetization += parameters.core.memory * core.getWeights()[i] * value;
        if (old < clipped - radius || old > clipped + radius)
            result.derivative += parameters.core.memory * core.getWeights()[i] * (1.0 - value * value) / scale;
    }
    if (std::abs (h) > Settings::maximumField)
        result.derivative = 0.0;
}
void TransformerAudioCircuit::commit (const Evaluation& evaluation) noexcept
{
    // Preserve the canonical core's arithmetic order: the trial sum deliberately
    // weights memory differently, so its rounded M cannot be used for the commit.
    double irreversible = 0.0;
    for (std::size_t i = 0; i < Settings::operatorCount; ++i)
        irreversible += core.weights[i] * evaluation.values[i];
    core.states = evaluation.positions;
    core.magnetization = std::clamp ((1.0 - parameters.core.memory) * evaluation.reversible
        + parameters.core.memory * irreversible, -1.0, 1.0);
    committedValues = evaluation.values;
    committedReversible = evaluation.reversible;
    committedTrialMagnetization = evaluation.trial.magnetization;
    committedClippedField = std::clamp (field, -Settings::maximumField, Settings::maximumField);
}
double TransformerAudioCircuit::processSample (double voltage) noexcept
{
    if (! finite (voltage))
    {
        voltage = 0.0;
        ++diagnostics.invalidInputs;
    }
    if (std::abs (voltage) > AudioSettings::maximumInput)
    {
        voltage = std::clamp (voltage, -AudioSettings::maximumInput, AudioSettings::maximumInput);
        ++diagnostics.limitedInputs;
    }
    const auto forcing = drive * (voltage + previousVoltage);
    if (linear || parameters.magnetizationWeight <= 0.0)
    {
        const auto slope = linear ? linearSlope : 1.0;
        field = ((slope - q) * field + q * forcing) / (slope + q);
        previousVoltage = voltage;
        return voltage - field / drive;
    }
    const auto estimate = ((1.0 - q) * field + q * forcing) / (1.0 + q);
    const auto radius = 2.0 * parameters.magnetizationWeight / (1.0 + q);
    auto lo = estimate - radius, hi = estimate + radius;
    auto h = std::clamp (field + q * (forcing - 2.0 * field)
        / (1.0 + q + parameters.magnetizationWeight * lastSlope), lo, hi);
    auto residual = 0.0;
    int iterations = 0;
    Evaluation evaluation;
    for (; iterations < AudioSettings::maximumIterations; ++iterations)
    {
        evaluate (h, evaluation);
        const auto& trial = evaluation.trial;
        residual = h - field + parameters.magnetizationWeight * (trial.magnetization - magnetization)
            - q * (forcing - field - h);
        if (std::abs (residual) <= AudioSettings::rootTolerance * std::max (1.0, std::abs (h)) || iterations + 1 == AudioSettings::maximumIterations)
            break;
        if (residual < 0.0) lo = h; else hi = h;
        const auto next = h - residual / (1.0 + q + parameters.magnetizationWeight * trial.derivative);
        h = iterations < AudioSettings::newtonIterations && next > lo && next < hi ? next : 0.5 * (lo + hi);
    }
    const auto scaledResidual = std::abs (residual) / std::max (1.0, std::abs (h));
    diagnostics.maximumEquationResidual = std::max (diagnostics.maximumEquationResidual, scaledResidual);
    diagnostics.maximumIterations = std::max (diagnostics.maximumIterations, iterations + 1);
    if (scaledResidual > AudioSettings::rootTolerance)
        ++diagnostics.solverFailures;
    field = h;
    lastSlope = evaluation.trial.derivative;
    commit (evaluation); // The only history commit, with no repeated tanh calls.
    magnetization = core.getMagnetization();
    previousVoltage = voltage;
    return voltage - h / drive;
}
}
