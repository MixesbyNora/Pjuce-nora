#include "AnalogDetector.h"
#include <algorithm>
#include <cmath>

namespace
{
// The 12AL5 law tabulated (cubic Hermite with its slope), shared and built once
// at prepare. Below -1.5 V a unit passes under 1e-8 A: nothing next to the
// release resistor's current. Past the table, the law itself.
constexpr double lowestVolts = -1.5;
const ModuleSpike::HermiteTable& diodeTable() noexcept
{
    static const ModuleSpike::HermiteTable table (lowestVolts, 80.0, 0.005, [] (double v)
    {
        const auto d = ModuleSpike::diodeCurrent (ModuleSpike::tungSol12al5(), v);
        return ModuleSpike::HermiteTable::Knot { d.i, d.slope };
    });
    return table;
}
ModuleSpike::HermiteTable::Knot conduction (double v) noexcept
{
    const auto& table = diodeTable();
    if (table.contains (v)) return table (v);
    const auto d = ModuleSpike::diodeCurrent (ModuleSpike::tungSol12al5(), v);
    return { d.i, d.slope };
}
}

void AnalogDetector::prepare (double sampleRate) noexcept
{
    dt = 1.0 / sampleRate;
    diodeTable();
    reset();
}

void AnalogDetector::configure (const Settings& s) noexcept
{
    scale = sidechainVolts * std::pow (10.0, s.sidechainGainDb / 20.0);
    delay = scale * std::pow (10.0, s.thresholdDb / 20.0);
    attackOhms = attackScale * s.attackMs * 1.0e-3 / timingFarads;
    const auto m = std::clamp (s.releaseMod, 0.0, 0.95);
    const auto release = s.releaseMs * 1.0e-3;
    const auto fastSeconds = std::min (release, std::max (fastestReleaseMs * 1.0e-3, 0.1 * release));
    // dt / C for each capacitor (1 / Cf = m / C, 1 / Cs = (1 - m) / C), and dt / (R C).
    fastCharge = dt * m / timingFarads;
    slowCharge = dt * (1 - m) / timingFarads;
    fastLeak = m > 0 ? dt / fastSeconds : 0.0;
    slowLeak = dt / release;
}

void AnalogDetector::reset (double controlVolts) noexcept
{
    fast = 0;
    slow = controlVolts;
    diode.fill (0.0);
}

double AnalogDetector::process (const double* sidechain, int channels, double calibrationGain) noexcept
{
    const auto control = fast + slow;
    // Diode currents at the present control voltage, and their slope against it, so
    // the step below is implicit in the diodes too (linearized).
    double current = 0, conductance = 0;
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto w = std::abs (sidechain[ch] * scale * calibrationGain) - delay - control; // Volts across Ra and the diode.
        auto& u = diode[static_cast<std::size_t> (ch)];
        if (! (w > lowestVolts)) { u = w; continue; }
        // u + Ra I(u) = w rises monotonically in u; the root lies in [w - Ra I(w), w].
        double hi = w, lo = w - attackOhms * conduction (w).f;
        u = std::clamp (u, lo, hi);
        ModuleSpike::HermiteTable::Knot d {};
        for (int i = 0; i < 30; ++i)
        {
            d = conduction (u);
            const auto residual = u + attackOhms * d.f - w;
            (residual > 0 ? hi : lo) = u;
            auto next = u - residual / (1 + attackOhms * d.d);
            if (! (next >= lo && next <= hi)) next = 0.5 * (lo + hi);
            const auto done = std::abs (next - u) < 1.0e-9;
            u = next;
            if (done) break;
        }
        d = conduction (u);
        current += d.f;
        conductance += d.d / (1 + attackOhms * d.d);
    }
    // Backward Euler on both capacitors, the diodes linearized about the control
    // voltage (x, y: their steps):
    //   x = dt / Cf (I - G (x + y)) - dt / (Rf Cf) (Vf + x)
    //   y = dt / Cs (I - G (x + y)) - dt / (Rs Cs) (Vs + y)
    const auto a11 = 1 + fastCharge * conductance + fastLeak, a12 = fastCharge * conductance;
    const auto a21 = slowCharge * conductance, a22 = 1 + slowCharge * conductance + slowLeak;
    const auto b1 = fastCharge * current - fastLeak * fast, b2 = slowCharge * current - slowLeak * slow;
    const auto determinant = a11 * a22 - a12 * a21;
    fast = std::max (0.0, fast + (b1 * a22 - a12 * b2) / determinant);
    slow = std::max (0.0, slow + (a11 * b2 - a21 * b1) / determinant);
    return fast + slow;
}
