#include "TransformerAudioReference.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace MagneticPrototype
{
namespace
{
bool finite (double x) noexcept { return (std::bit_cast<std::uint64_t> (x) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL; }
double bounded (double x, double lo, double hi, double fallback) noexcept { return finite (x) ? std::clamp (x, lo, hi) : fallback; }
}
void TransformerAudioReference::prepare (double rate, Parameters values, bool linearReference) noexcept
{
    rate = bounded (rate, FrozenAudioSettings::minimumRate, FrozenAudioSettings::maximumRate, 48000.0);
    parameters.cornerHz = bounded (values.cornerHz, FrozenAudioSettings::minimumCornerHz, FrozenAudioSettings::maximumCornerHz, FrozenAudioSettings::cornerHz);
    parameters.driveDb = bounded (values.driveDb, FrozenAudioSettings::minimumDriveDb, FrozenAudioSettings::maximumDriveDb, FrozenAudioSettings::driveDb);
    parameters.magnetizationWeight = bounded (values.magnetizationWeight, 0.0, FrozenAudioSettings::maximumMagnetizationWeight, FrozenAudioSettings::magnetizationWeight);
    core.prepare (values.core);
    parameters.core = core.getParameters();
    linearSlope = 1.0 + parameters.magnetizationWeight * (1.0 - parameters.core.memory) / Settings::saturationField;
    tau = 1.0 / (2.0 * std::numbers::pi * parameters.cornerHz * linearSlope);
    q = 1.0 / (2.0 * rate * tau);
    drive = FrozenAudioSettings::fieldScale * std::pow (10.0, parameters.driveDb / 20.0);
    linear = linearReference;
    reset();
}
void TransformerAudioReference::reset() noexcept
{
    core.reset();
    field = magnetization = previousVoltage = 0.0;
    diagnostics = {};
}
TransformerAudioReference::Trial TransformerAudioReference::evaluateTrial (double h) const noexcept
{
    if (! finite (h))
        return { core.getMagnetization(), 0.0 };
    const auto clipped = std::clamp (h, -Settings::maximumField, Settings::maximumField);
    const auto scale = Settings::saturationField;
    const auto reversible = std::tanh (clipped / scale);
    Trial result { (1.0 - parameters.core.memory) * reversible,
        (1.0 - parameters.core.memory) * (1.0 - reversible * reversible) / scale };
    for (std::size_t i = 0; i < Settings::operatorCount; ++i)
    {
        const auto old = core.getPlayStates()[i];
        const auto radius = core.getRadii()[i];
        const auto position = std::clamp (old, clipped - radius, clipped + radius);
        const auto value = std::tanh (position / scale);
        result.magnetization += parameters.core.memory * core.getWeights()[i] * value;
        if (old < clipped - radius || old > clipped + radius)
            result.derivative += parameters.core.memory * core.getWeights()[i] * (1.0 - value * value) / scale;
    }
    if (std::abs (h) > Settings::maximumField)
        result.derivative = 0.0;
    return result;
}
double TransformerAudioReference::processSample (double voltage) noexcept
{
    if (! finite (voltage))
    {
        voltage = 0.0;
        ++diagnostics.invalidInputs;
    }
    if (std::abs (voltage) > FrozenAudioSettings::maximumInput)
    {
        voltage = std::clamp (voltage, -FrozenAudioSettings::maximumInput, FrozenAudioSettings::maximumInput);
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
    auto h = std::clamp (field, lo, hi);
    auto residual = 0.0;
    int iterations = 0;
    for (; iterations < FrozenAudioSettings::maximumIterations; ++iterations)
    {
        const auto trial = evaluateTrial (h);
        residual = h - field + parameters.magnetizationWeight * (trial.magnetization - magnetization)
            - q * (forcing - field - h);
        if (std::abs (residual) <= FrozenAudioSettings::rootTolerance * std::max (1.0, std::abs (h)) || iterations + 1 == FrozenAudioSettings::maximumIterations)
            break;
        if (residual < 0.0) lo = h; else hi = h;
        const auto next = h - residual / (1.0 + q + parameters.magnetizationWeight * trial.derivative);
        h = iterations < FrozenAudioSettings::newtonIterations && next > lo && next < hi ? next : 0.5 * (lo + hi);
    }
    const auto scaledResidual = std::abs (residual) / std::max (1.0, std::abs (h));
    diagnostics.maximumEquationResidual = std::max (diagnostics.maximumEquationResidual, scaledResidual);
    diagnostics.maximumIterations = std::max (diagnostics.maximumIterations, iterations + 1);
    if (scaledResidual > FrozenAudioSettings::rootTolerance)
        ++diagnostics.solverFailures;
    field = h;
    magnetization = core.processField (h); // The only history commit in the solve.
    previousVoltage = voltage;
    return voltage - h / drive;
}
}
