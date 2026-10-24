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
    // Inverting stage; divide by the (negative) small-signal gain to keep polarity.
    return (plateFor (x * gridScale) - plateQuiescent) / (smallSignalGain * gridScale);
}

const Stage& stage() noexcept
{
    static const Stage live;
    return live;
}
}
