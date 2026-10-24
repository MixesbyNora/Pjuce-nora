#pragma once
#include <algorithm>
#include <array>
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
    // One exponential serves both the softplus and its derivative (the sigmoid).
    const auto e = std::exp (std::min (z, 30.0));
    const auto softplus = z > 30 ? z : std::log1p (e);
    const auto sigmoid = z > 30 ? 1.0 : e / (1 + e);
    const auto e1 = va / p.kp * softplus;
    if (! (e1 > 0)) return {};
    const auto power = std::pow (e1, p.ex);
    const auto slope = 2 * p.ex * power / e1 / p.kg1;
    return { 2 * power / p.kg1, slope * (softplus / p.kp - sigmoid * vg * va * va / (s * s * s)), slope * va * sigmoid / s };
}
// Grid current: 3/2-power in grid-to-cathode voltage above about 0 V, its onset
// smoothed over a few tenths of a volt (contact potential). Fitted to the GE 5670
// sheet's dashed grid-current lines (p5: 2.0 / 5.75 / 10 mA at +2 / +4 / +6 V):
// 0.70 mA/V^1.5 (docs/Overdrive.md). No other sheet here publishes it.
inline constexpr double ge5670GridPerveance = 0.70e-3;
struct GridCurrent { double ig = 0, slope = 0; };
inline GridCurrent gridCurrent (double vgk, double perveance = ge5670GridPerveance) noexcept
{
    constexpr double sharpness = 4.0; // per volt
    const auto z = sharpness * vgk;
    const auto e = std::exp (std::min (z, 30.0));
    const auto x = z > 30 ? vgk : std::log1p (e) / sharpness;
    const auto dx = z > 30 ? 1.0 : e / (1 + e);
    const auto root = std::sqrt (std::max (x, 0.0));
    return { perveance * x * root, 1.5 * perveance * root * dx };
}
// Remote-cutoff (variable-mu) triode, such as the 6386: its variable-pitch grid
// behaves like sections in parallel with different cutoffs. Each section is a
// Koren triode and the currents add (docs/GainTube.md).
struct VariableMuParameters
{
    std::array<TriodeParameters, 3> sections {};
    int count = 1;
};
inline TriodeCurrent variableMuCurrent (const VariableMuParameters& p, double va, double vg) noexcept
{
    TriodeCurrent sum;
    for (int i = 0; i < p.count; ++i)
    {
        const auto c = triodeCurrent (p.sections[static_cast<std::size_t> (i)], va, vg);
        sum.ia += c.ia; sum.dVa += c.dVa; sum.dVg += c.dVg;
    }
    return sum;
}
// Fits to the digitized average plate families (task tube-fit, docs/TubeStage.md).
// RCA 12BH7-A tentative data, Mar 1955, PDF p4; each triode unit.
inline TriodeParameters rca12bh7a() noexcept { return { 23.43635165, 1.102590463, 389.1440358, 107.4446752, 5309.767147 }; }
// GE GL-5751, ETX-245A, Jun 1952, p2; each triode unit.
inline TriodeParameters ge5751() noexcept { return { 87.40702906, 1.0690516, 749.109422, 409.6063436, 4508.165482 }; }
// Gain-control tubes (task gain-tube-fit, docs/GainTube.md).
// GE 5670, ET-T1083A (Aug 1956), p5 plate family; each section.
inline TriodeParameters ge5670() noexcept { return { 40.21453522, 1.659496752, 833.5216629, 118.6584674, 203.6156978 }; }
// GE 6386, ET-T1113 (Aug 1954), p4 transfer family plus the p2 anchors; each
// section. Two sections; the sharp one sits at the fit's mu bound of 60.
inline VariableMuParameters ge6386() noexcept
{
    VariableMuParameters p;
    p.count = 2;
    p.sections[0] = { 60.0, 1.627518423, 484.1414682, 47.05430135, 941.993712 };
    p.sections[1] = { 4.310000073, 1.627518423, 111976.2989, 47.05430135, 941.993712 };
    return p;
}
}
