#pragma once
#include <cmath>

namespace ModuleSpike
{
// Koren triode plate current (N. Koren, "Improved vacuum tube models for SPICE
// simulations", Glass Audio 8(5), 1996; revised 2003):
//   E1 = Va/kp * ln(1 + exp(kp*(1/mu + Vgk/sqrt(kvb + Va^2))))
//   Ia = E1^ex / kg1 * (1 + sgn(E1))
// Volts and amperes, one triode section. No grid current: the fitted sheets
// publish no positive-grid curves (docs/TubeStage.md).
struct TriodeParameters
{
    double mu = 100, ex = 1.4, kg1 = 1060, kp = 600, kvb = 300;
};
struct TriodeCurrent
{
    double ia = 0, dVa = 0, dVg = 0; // Plate current and its partial derivatives (A, S).
};
inline TriodeCurrent triodeCurrent (const TriodeParameters& p, double va, double vg) noexcept
{
    if (! (va > 0)) return {};
    const auto s = std::sqrt (p.kvb + va * va);
    const auto z = p.kp * (1 / p.mu + vg / s);
    const auto softplus = z > 30 ? z : std::log1p (std::exp (z));
    const auto sigmoid = 1 / (1 + std::exp (-z));
    const auto e1 = va / p.kp * softplus;
    if (! (e1 > 0)) return {};
    const auto power = std::pow (e1, p.ex);
    const auto slope = 2 * p.ex * power / e1 / p.kg1;
    return { 2 * power / p.kg1, slope * (softplus / p.kp - sigmoid * vg * va * va / (s * s * s)), slope * va * sigmoid / s };
}
// Fits to the digitized average plate families (task tube-fit, docs/TubeStage.md).
// RCA 12BH7-A tentative data, Mar 1955, PDF p4; each triode unit.
inline TriodeParameters rca12bh7a() noexcept { return { 23.43635165, 1.102590463, 389.1440358, 107.4446752, 5309.767147 }; }
// GE GL-5751, ETX-245A, Jun 1952, p2; each triode unit.
inline TriodeParameters ge5751() noexcept { return { 87.40702906, 1.0690516, 749.109422, 409.6063436, 4508.165482 }; }
}
