#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

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
// A uniform cubic Hermite table of a function and its slope, exact at the knots.
// The slope it returns is the interpolant's own, so Newton on it stays consistent.
// Built once, never on the audio thread.
class HermiteTable
{
public:
    struct Knot { double f = 0, d = 0; };
    HermiteTable() = default;
    template <typename F>
    HermiteTable (double lowest, double highest, double spacing, F law) : low (lowest), high (highest), step (spacing)
    {
        knots.resize (static_cast<std::size_t> ((high - low) / step + 0.5) + 1);
        for (std::size_t i = 0; i < knots.size(); ++i) knots[i] = law (low + static_cast<double> (i) * step);
    }
    bool contains (double x) const noexcept { return x > low && x < high; }
    // Value and slope at x, which must be in range.
    Knot operator() (double x) const noexcept
    {
        const auto position = (x - low) / step;
        const auto i = std::min (static_cast<std::size_t> (position), knots.size() - 2);
        const auto t = position - static_cast<double> (i), t2 = t * t, t3 = t2 * t;
        const auto& a = knots[i];
        const auto& b = knots[i + 1];
        const auto da = a.d * step, db = b.d * step;
        return { (2 * t3 - 3 * t2 + 1) * a.f + (t3 - 2 * t2 + t) * da + (3 * t2 - 2 * t3) * b.f + (t3 - t2) * db,
                 ((6 * t2 - 6 * t) * (a.f - b.f) + (3 * t2 - 4 * t + 1) * da + (3 * t2 - 2 * t) * db) / step };
    }

private:
    double low = 0, high = 0, step = 1;
    std::vector<Knot> knots;
};
// Koren's current is separable: Ia = 2/kg1 * (Va/kp)^ex * softplus(z)^ex, with
// z = kp (1/mu + Vg / sqrt(kvb + Va^2)). Tabulating the two one-dimensional
// factors replaces exp, log1p and pow: about 3x faster, within 3e-9 of the law
// (docs/Overdrive.md, "CPU"). Outside the tables, the exact law.
class TriodeTable
{
public:
    explicit TriodeTable (const TriodeParameters& q)
        : p (q), kpOverMu (q.kp / q.mu), scale (2 / q.kg1),
          grid (-40, 40, 0.005, [q] (double z)
          {
              const auto e = std::exp (z);
              const auto softplus = std::log1p (e), power = std::pow (softplus, q.ex);
              return HermiteTable::Knot { power, q.ex * power / softplus * e / (1 + e) };
          }),
          plate (1, 800, 0.05, [q] (double va)
          {
              const auto power = std::pow (va / q.kp, q.ex);
              return HermiteTable::Knot { power, q.ex * power / va };
          })
    {
    }
    TriodeCurrent operator() (double va, double vg) const noexcept
    {
        if (! plate.contains (va)) return triodeCurrent (p, va, vg);
        const auto inverse = 1 / std::sqrt (p.kvb + va * va);
        const auto z = kpOverMu + p.kp * vg * inverse;
        if (! grid.contains (z)) return triodeCurrent (p, va, vg);
        const auto g = grid (z), h = plate (va);
        const auto dz = p.kp * inverse;
        return { scale * h.f * g.f, scale * (h.d * g.f - h.f * g.d * dz * vg * va * inverse * inverse), scale * h.f * g.d * dz };
    }
    const TriodeParameters& parameters() const noexcept { return p; }

private:
    TriodeParameters p;
    double kpOverMu, scale;
    HermiteTable grid, plate;
};
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
// A grid driven from a source resistance into its own grid current (one law per
// grid; `grids` in parallel). For an open-circuit drive d relative to the cathode,
// the grid-to-cathode voltage w solves (d - w) / R = Ig(w): monotone, tabulated
// with its slope dw/dd. Far below the cathode it is d itself; past the tables,
// Newton on the law (docs/Overdrive.md, "Output grid current").
class ResistiveGrid
{
public:
    struct Point { double vgk = 0, slope = 1, ig = 0; };
    ResistiveGrid (double sourceOhms, double grids, double perveance = ge5670GridPerveance)
        : r (sourceOhms), scale (grids * perveance),
          fine (cutoff, 60, 0.005, [this] (double d) { return knot (d); }),
          coarse (60 - 1, 3000, 0.5, [this] (double d) { return knot (d); })
    {
    }
    Point operator() (double d) const noexcept
    {
        if (! (d > cutoff)) return { d, 1, 0 };
        const auto k = fine.contains (d) ? fine (d) : coarse.contains (d) ? coarse (d) : knot (d);
        return { k.f, k.d, (d - k.f) / r };
    }
    double sourceOhms() const noexcept { return r; }

private:
    static constexpr double cutoff = -10; // Ig there is below 1e-24 A.
    HermiteTable::Knot knot (double d) const noexcept
    {
        // w + R Ig(w) rises monotonically and convexly with w, and is at least d
        // at w = d: Newton from there descends onto the root without overshoot.
        auto w = d;
        GridCurrent g;
        for (int n = 0; n < 100; ++n)
        {
            g = gridCurrent (w, scale);
            const auto residual = w + r * g.ig - d;
            if (std::abs (residual) < 1.0e-13 * (1 + std::abs (d))) break;
            w = std::min (w - residual / (1 + r * g.slope), d);
        }
        return { w, 1 / (1 + r * gridCurrent (w, scale).slope) };
    }
    double r, scale;
    HermiteTable fine, coarse;
};
// A diode's plate current against plate-to-cathode voltage (docs/Detector.md):
//   I = P * (softplus(s V) / s)^n
// The power law above about a volt (space charge), and below it the cathode's
// initial-velocity current, exponential with slope n s = q / kT, which lets a
// detector develop a small negative bias with no signal (contact potential).
struct DiodeParameters
{
    double perveance = 1.0e-3, exponent = 1.5, sharpness = 10.0;
};
struct DiodeCurrent { double i = 0, slope = 0; };
inline DiodeCurrent diodeCurrent (const DiodeParameters& p, double v) noexcept
{
    const auto z = p.sharpness * v;
    const auto e = std::exp (std::min (z, 30.0));
    const auto softplus = z > 30 ? z : std::log1p (e);
    if (! (softplus > 0)) return {};
    const auto x = softplus / p.sharpness;
    const auto power = p.perveance * std::pow (x, p.exponent);
    return { power, p.exponent * power / x * (z > 30 ? 1.0 : e / (1 + e)) };
}
// Tung-Sol 12AL5, Mar 1953, p2: the forward curve, each unit (task detector-fit):
// 0.47 % RMS over 0.75-8.25 V. The sharpness sets n s = 1 / 0.0905 V, kT/q at an
// oxide cathode's 1050 K; it gives 0.47-0.57 V of self-bias into 0.5-2 MOhm, as
// the sheet's detector curves show about 0.6 V. Those curves are not fitted: they
// imply about 20x less current than the forward curve, so they include a test
// circuit the sheet does not give.
inline DiodeParameters tungSol12al5() noexcept { return { 2.750585398e-3, 1.346428335, 8.206692822 }; }
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
// The 12BH7 table, shared and built on first use (the output stage's design, at prepare).
inline const TriodeTable& rca12bh7aTable() noexcept
{
    static const TriodeTable table (rca12bh7a());
    return table;
}
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
