#pragma once
#include "ModuleContract.h"
#include <limits>

namespace ModuleSpike
{
enum class Variant { smooth, play3 };
enum class Topology { shunt, series };
inline constexpr std::array transformerDescriptors {
    Descriptor { "saturation", "Saturation point", "normalized flux", 0.1, 4.0, 0.01, 1.0, 1.0 },
    Descriptor { "knee", "Knee", "normalized current", 0.02, 2.0, 0.01, 0.2, 0.5 },
    Descriptor { "memory", "Memory", "", 0.0, 1.0, 0.01, 0.35, 1.0 },
    Descriptor { "loss", "Winding loss", "normalized resistance", 0.01, 1.0, 0.01, 0.1, 1.0 },
    // Upper bound widened from 24 to 48 dB in shunt Step 2: the VTB1148 fit plus
    // the +18 dB render setting exceeds 24 dB.
    Descriptor { "driveCalibrationDb", "Drive calibration", "dB", -24, 48, 0.1, 0, 1.0 }
};
struct TransformerParameters
{
    double saturation = transformerDescriptors[0].defaultValue, knee = transformerDescriptors[1].defaultValue;
    double memory = transformerDescriptors[2].defaultValue, loss = transformerDescriptors[3].defaultValue, driveDb = transformerDescriptors[4].defaultValue;
    double sourceOhms = 1000.0, loadOhms = 10000.0;
};
// Separate input protection, before drive and before the circuit. Not magnetics.
struct InputSafety
{
    bool enabled = true;
    double limit = 16.0;
    std::uint64_t limited = 0, invalid = 0;
    double process (double x) noexcept
    {
        if (! isFinite (x)) { ++invalid; return 0.0; }
        if (enabled && std::abs (x) > limit) { ++limited; return std::clamp (x, -limit, limit); }
        return x;
    }
    void reset() noexcept { limited = invalid = 0; }
};
struct MagneticTrial { double flux = 0, derivative = 0, magnitude = 0; };
// Flux linkage versus normalized magnetizing current. Trials never mutate history.
class MagneticElement
{
public:
    virtual ~MagneticElement() = default;
    virtual MagneticTrial evaluate (double current, const TransformerParameters&) const noexcept = 0;
    virtual void commit (double current) noexcept = 0;
    virtual void reset() noexcept = 0;
};
class SmoothElement final : public MagneticElement
{
public:
    MagneticTrial evaluate (double i, const TransformerParameters& p) const noexcept override
    {
        const auto m = std::tanh (i / p.knee);
        return { 0.1 * i + p.saturation * m, 0.1 + p.saturation / p.knee * (1.0 - m * m), 0.1 * std::abs (i) + p.saturation * std::abs (m) };
    }
    void commit (double) noexcept override {}
    void reset() noexcept override {}
};
class PlayElement final : public MagneticElement
{
public:
    MagneticTrial evaluate (double i, const TransformerParameters& p) const noexcept override
    {
        auto m = 0.0, derivative = 0.0, magnitude = 0.0;
        for (std::size_t n = 0; n < positions.size(); ++n)
        {
            const auto position = std::clamp (positions[n], i - radii[n], i + radii[n]);
            const auto value = std::tanh (position / p.knee);
            m += value / 3.0;
            magnitude += std::abs (value) / 3.0;
            if (i - radii[n] > positions[n] || i + radii[n] < positions[n])
                derivative += (1.0 - value * value) / (3.0 * p.knee);
        }
        const auto reversible = std::tanh (i / p.knee);
        return { 0.1 * i + p.saturation * ((1.0 - p.memory) * reversible + p.memory * m),
            0.1 + p.saturation * ((1.0 - p.memory) / p.knee * (1.0 - reversible * reversible) + p.memory * derivative),
            0.1 * std::abs (i) + p.saturation * ((1.0 - p.memory) * std::abs (reversible) + p.memory * magnitude) };
    }
    void commit (double i) noexcept override
    {
        for (std::size_t n = 0; n < positions.size(); ++n) positions[n] = std::clamp (positions[n], i - radii[n], i + radii[n]);
    }
    void reset() noexcept override { positions = {}; }
private:
    // Reduced play construction, not an identified material or a fit of the live core.
    inline static constexpr std::array radii { 0.02, 0.15, 0.6 };
    std::array<double, 3> positions {};
};
struct SolverDiagnostics
{
    int maximumIterations = 0;
    std::uint64_t failures = 0;
    double maximumResidual = 0;
};
// Normalized 1:1 ideal secondary. Zbase=1000 ohms; flux scale is seconds.
// Shunt: v=(u-Rs*i)/(1+Rs/Rl), d(lambda)/dt=v.
// Series: vL=u-(Rs+Rl)*i, output=Rl*i, d(lambda)/dt=vL.
// lambda=fluxScale*element(i). Trapezoidal integration, one scalar root.
class TransformerCircuit
{
public:
    static constexpr double fluxScale = 0.005, impedanceBase = 1000.0, tolerance = 1.0e-13;
    void prepare (double rate, Variant v, Topology t, TransformerParameters p) noexcept
    { dt = 0.5 / rate; variant = v; topology = t; parameters = p; reset(); }
    void reset() noexcept { smooth.reset(); play.reset(); current = flux = previousVoltage = 0; diagnostics = {}; }
    double process (double u) noexcept
    {
        const auto rs = parameters.sourceOhms / impedanceBase + parameters.loss, rl = parameters.loadOhms / impedanceBase;
        const auto a = topology == Topology::shunt ? 1.0 + rs / rl : 1.0;
        const auto b = topology == Topology::shunt ? rs : rs + rl;
        const auto constant = flux + dt * (u / a + previousVoltage);
        // The equation is flux balance. A voltage-equivalent goal scales with
        // dt; a fixed flux cutoff otherwise freezes quiet tails at high rates.
        // Roundoff must use individual term magnitudes: hysteretic contributions
        // can cancel even with a near-zero *net* flux. Use an operation-count
        // error bound, not a fixed absolute floor that would freeze quiet tails.
        constexpr auto epsilon = std::numeric_limits<double>::epsilon();
        constexpr auto gamma = 32 * epsilon / (1 - 32 * epsilon);
        auto& element = variant == Variant::smooth ? static_cast<MagneticElement&> (smooth) : static_cast<MagneticElement&> (play);
        auto lo = -std::max (1.0, (std::abs (constant) + 2 * fluxScale * parameters.saturation) / (fluxScale * 0.1 + dt * b / a));
        auto hi = -lo;
        auto i = std::clamp (current, lo, hi);
        MagneticTrial trial;
        double residual = 0;
        int iterations = 0;
        for (; iterations < 96; ++iterations)
        {
            trial = element.evaluate (i, parameters);
            residual = fluxScale * trial.flux + dt * b / a * i - constant;
            const auto roundoff = gamma * (fluxScale * trial.magnitude + std::abs (dt * b / a * i) + std::abs (constant));
            const auto rootGoal = std::max (tolerance * dt, roundoff);
            if (std::abs (residual) <= rootGoal) break;
            if (residual > 0) hi = i; else lo = i;
            const auto candidate = i - residual / (fluxScale * trial.derivative + dt * b / a);
            // Bound Newton effort, then bisect the monotone bracket. A sequence
            // of accepted near-endpoint Newton steps must not stall convergence.
            i = iterations < 8 && candidate > lo && candidate < hi ? candidate : 0.5 * (lo + hi);
        }
        diagnostics.maximumIterations = std::max (diagnostics.maximumIterations, std::min (iterations + 1, 96));
        diagnostics.maximumResidual = std::max (diagnostics.maximumResidual, std::abs (residual) / (1 + std::abs (constant)));
        if (iterations >= 96 || ! isFinite (i)) { ++diagnostics.failures; return 0; }
        current = i; flux = fluxScale * trial.flux; element.commit (i);
        previousVoltage = (u - b * i) / a;
        return topology == Topology::shunt ? previousVoltage : rl * i;
    }
    SolverDiagnostics diagnostics;
private:
    SmoothElement smooth;
    PlayElement play;
    TransformerParameters parameters;
    Variant variant = Variant::smooth;
    Topology topology = Topology::shunt;
    double dt = 0, current = 0, flux = 0, previousVoltage = 0;
};
class TransformerModule final : public Module
{
public:
    bool prepare (Spec value) noexcept override
    {
        if (! value.valid()) return false;
        spec = value; configure (variant, topology, parameters); return true;
    }
    void configure (Variant v, Topology t, TransformerParameters p) noexcept
    {
        const auto bound = [] (double value, std::size_t index) noexcept
        {
            const auto& d = transformerDescriptors[index];
            return bounded (value, d.minimum, d.maximum, d.defaultValue);
        };
        p.saturation = bound (p.saturation, 0); p.knee = bound (p.knee, 1);
        p.memory = bound (p.memory, 2); p.loss = bound (p.loss, 3); p.driveDb = bound (p.driveDb, 4);
        p.sourceOhms = bounded (p.sourceOhms, 0.01, 1.0e8, 1000);
        p.loadOhms = bounded (p.loadOhms, 0.01, 1.0e8, 10000);
        parameters = p; variant = v; topology = t;
        gain = std::pow (10.0, p.driveDb / 20.0);
        for (auto& circuit : circuits) circuit.prepare (spec.sampleRate, v, t, p);
        safety.reset();
    }
    void reset() noexcept override { for (auto& circuit : circuits) circuit.reset(); safety.reset(); }
    double processSample (double x, int channel) noexcept
    { return circuits[static_cast<std::size_t> (channel)].process (safety.process (x) * gain) / gain; }
    void process (Block block, Modulation = {}) noexcept override
    {
        for (int c = 0; c < block.channels; ++c)
            for (std::size_t n = 0; n < block.samples; ++n)
                block.data[static_cast<std::size_t> (c)][n] = processSample (block.data[static_cast<std::size_t> (c)][n], c);
    }
    Info info() const noexcept override
    {
        Info result;
        result.partId = variant == Variant::smooth ? "transformer.smooth" : "transformer.play3";
        result.version = "2-spike"; result.tailSamples = -1; // Asymptotic, not a finite truncation promise.
        result.nonlinear = true; result.preferredFactor = variant == Variant::smooth ? 4 : 8;
        result.parameters = transformerDescriptors;
        result.input = Impedance::scalar (1000); // Nominal metadata; not the nonlinear dynamic driving-point impedance.
        result.output = Impedance::scalar (parameters.sourceOhms);
        return result;
    }
    std::array<TransformerCircuit, 2> circuits;
    InputSafety safety;
private:
    Spec spec;
    TransformerParameters parameters;
    Variant variant = Variant::smooth;
    Topology topology = Topology::shunt;
    double gain = 1;
};
}
