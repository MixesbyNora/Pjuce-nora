#pragma once
#include <modules/SourceFixture.h>

namespace ModuleSpike
{
// OFFLINE MEASUREMENT TOOL ONLY, not a production module/configuration.
// Same source family as the fixture; load current feeds back *inside* tanh:
// terminal = tanh(4*(excitation - Rsource*Iterminal))/4.
// Winding R=100 ohms + loss*1000 ohms is separate from source impedance. Trial flux gives
// winding voltage by the trapezoidal equation; KCL/KVL leaves one scalar root.
class OfflineCoupledReference
{
public:
    static constexpr double tolerance = 1.0e-18; // Flux-balance units, not volts.
    void prepare (double rate, Variant v, Topology t, TransformerParameters p, double sourceOhms) noexcept
    {
        dt = 0.5 / rate; variant = v; topology = t; parameters = p;
        sourceR = sourceOhms / TransformerCircuit::impedanceBase;
        reset();
    }
    void reset() noexcept { smooth.reset(); play.reset(); outer.reset(); oldFlux = oldVoltage = current = 0; diagnostics = {}; }
    double process (double excitation) noexcept
    {
        auto& element = variant == Variant::smooth ? static_cast<MagneticElement&> (smooth)
            : variant == Variant::play3 ? static_cast<MagneticElement&> (play) : static_cast<MagneticElement&> (outer);
        const auto rl = parameters.loadOhms / TransformerCircuit::impedanceBase;
        const auto winding = 0.1 + parameters.loss;
        auto lo = -1000000.0, hi = 1000000.0, i = current;
        double voltage = 0, flux = 0, residual = 0;
        int iteration = 0;
        for (; iteration < 96; ++iteration)
        {
            const auto law = element.evaluate (i, parameters);
            flux = TransformerCircuit::fluxScale * law.flux;
            voltage = (flux - oldFlux) / dt - oldVoltage;
            const auto dv = TransformerCircuit::fluxScale * law.derivative / dt;
            const auto loadCurrent = topology == Topology::shunt ? i + voltage / rl : i;
            const auto di = topology == Topology::shunt ? 1.0 + dv / rl : 1.0;
            const auto terminal = topology == Topology::shunt ? voltage + winding * loadCurrent : voltage + (winding + rl) * i;
            const auto terminalDerivative = topology == Topology::shunt ? dv + winding * di : dv + winding + rl;
            const auto s = std::tanh (4.0 * (excitation - sourceR * loadCurrent));
            residual = dt * (terminal - s / 4.0);
            if (std::abs (residual) <= tolerance) break;
            if (residual > 0) hi = i; else lo = i;
            const auto next = i - residual / (dt * (terminalDerivative + sourceR * di * (1.0 - s * s)));
            i = iteration < 8 && next > lo && next < hi ? next : 0.5 * (lo + hi);
        }
        diagnostics.maximumIterations = std::max (diagnostics.maximumIterations, iteration + 1);
        diagnostics.maximumResidual = std::max (diagnostics.maximumResidual, std::abs (residual));
        if (iteration >= 96 || ! isFinite (i)) { ++diagnostics.failures; return 0; }
        current = i; oldFlux = flux; oldVoltage = voltage; element.commit (i, parameters);
        return topology == Topology::shunt ? voltage : rl * i;
    }
    SolverDiagnostics diagnostics;
private:
    SmoothElement smooth; PlayElement play; OuterPlayElement outer;
    TransformerParameters parameters;
    Variant variant = Variant::smooth;
    Topology topology = Topology::shunt;
    double dt = 0, sourceR = 1, oldFlux = 0, oldVoltage = 0, current = 0;
};
}
