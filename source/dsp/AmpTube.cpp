#include "AmpTube.h"
#include "../modules/CathodeFollower.h"

namespace AmpTube
{
Stage::Stage (Design d) : design (d)
{
    const auto tube = ModuleSpike::ge5751();
    // DC point: plate current set by the cathode resistor (bypassed for audio).
    plateQuiescent = d.supply - d.plateOhms * d.plateAmps;
    double lo = -20, hi = 0;
    for (int i = 0; i < 100; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (ModuleSpike::triodeCurrent (tube, plateQuiescent, mid).ia < d.plateAmps ? lo : hi) = mid;
    }
    cathode = -0.5 * (lo + hi);
    rk = cathode / d.plateAmps;
    plateOutputOhms = 1 / (1 / d.plateOhms + ModuleSpike::triodeCurrent (tube, plateQuiescent, -cathode).dVa);
    // Audio: cathode fixed by its bypass, the coupling capacitor holding the
    // quiescent plate voltage, so the plate sees Ra to B+ and Rg to that voltage.
    const auto count = static_cast<std::size_t> (2 * range / step) + 1;
    plate.resize (count);
    slope.resize (count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto grid = -range + static_cast<double> (i) * step;
        // KCL at the plate falls monotonically with plate voltage: bisect.
        double low = 0, high = d.supply;
        for (int n = 0; n < 80; ++n)
        {
            const auto va = 0.5 * (low + high);
            const auto residual = (d.supply - va) / d.plateOhms - (va - plateQuiescent) / d.nextGridOhms
                - ModuleSpike::triodeCurrent (tube, va, grid - cathode).ia;
            (residual > 0 ? low : high) = va;
        }
        plate[i] = 0.5 * (low + high);
    }
    // Slopes from the implicit derivative: dVa/dVg = -gm / (1/Ra + 1/Rg + ga).
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto c = ModuleSpike::triodeCurrent (tube, plate[i], -range + static_cast<double> (i) * step - cathode);
        slope[i] = -c.dVg / (1 / d.plateOhms + 1 / d.nextGridOhms + c.dVa);
    }
    smallSignalGain = slope[static_cast<std::size_t> (range / step)];
    // At 0 dBFS the plate swings the follower's line level (0 dBFS = +18 dBu).
    gridScale = ModuleSpike::fullScalePeakVolts() / std::abs (smallSignalGain);
}

double Stage::plateFor (double grid) const noexcept
{
    const auto position = (grid + range) / step;
    const auto last = plate.size() - 1;
    if (! (position > 0)) return plate.front() + (grid + range) * slope.front();
    if (position >= static_cast<double> (last)) return plate[last] + (grid - range) * slope[last];
    const auto i = static_cast<std::size_t> (position);
    const auto t = position - static_cast<double> (i);
    const auto t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * plate[i] + (t3 - 2 * t2 + t) * step * slope[i]
         + (-2 * t3 + 3 * t2) * plate[i + 1] + (t3 - t2) * step * slope[i + 1];
}

double Stage::process (double x) const noexcept
{
    return outputForGrid (x * gridScale);
}

double Stage::outputForGrid (double grid) const noexcept
{
    // Inverting stage; divide by the (negative) small-signal gain to keep polarity.
    return (plateFor (grid) - plateQuiescent) / (smallSignalGain * gridScale);
}

void Grid::prepare (double sampleRate) noexcept
{
    step = 1.0 / (2.0 * sampleRate * couplingFarads); // Trapezoidal capacitor: T / 2C.
    // Midband the leak divides the drive by Rleak / (Rsource + Rleak): undo it so
    // the small-signal gain stays unity.
    scale = stage().gridVoltsPerUnit() * (sourceOhms + leakOhms) / leakOhms;
    cathode = stage().cathodeVolts();
    reset();
}

double Grid::process (double x, double drive) noexcept
{
    // Source -- Rs -- C -- grid -- Rleak to ground, grid current to the cathode.
    // Coupling current i = (a - grid) / r with the capacitor's trapezoidal step folded in.
    const auto a = drive * (x * scale - capacitor - step * previousCurrent);
    const auto r = sourceOhms + step;
    // KCL at the grid rises monotonically with the grid voltage.
    auto grid = a / (1.0 + r / leakOhms);
    if (grid - cathode > -2.0)
    {
        double lo = grid - 1.0e3, hi = a;
        for (int n = 0; n < 60; ++n)
        {
            const auto g = ModuleSpike::gridCurrent (grid - cathode);
            const auto residual = grid / leakOhms + g.ig - (a - grid) / r;
            if (std::abs (residual) < 1.0e-15) break;
            (residual > 0 ? hi : lo) = grid;
            auto next = grid - residual / (1.0 / leakOhms + g.slope + 1.0 / r);
            if (! (next > lo && next < hi) || n >= 8) next = 0.5 * (lo + hi);
            grid = next;
        }
    }
    const auto current = (a - grid) / r / drive;
    capacitor += step * (current + previousCurrent);
    previousCurrent = current;
    return grid;
}

const Stage& stage() noexcept
{
    static const Stage live;
    return live;
}
}
