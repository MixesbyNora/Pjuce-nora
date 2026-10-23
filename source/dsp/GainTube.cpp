#include "GainTube.h"

namespace GainTube
{
namespace
{
ModuleSpike::TriodeCurrent evaluate (Tube tube, double plate, double grid) noexcept
{
    return tube == Tube::ge6386 ? ModuleSpike::variableMuCurrent (ModuleSpike::ge6386(), plate, grid)
                                : ModuleSpike::triodeCurrent (ModuleSpike::ge5670(), plate, grid);
}
}

Curve::Curve (Tube tubeType, double plateVolts, double quiescentGrid, double minimumGrid)
    : plate (plateVolts), quiescent (quiescentGrid), bottom (minimumGrid)
{
    const auto count = static_cast<std::size_t> ((top - bottom) / step) + 1;
    current.resize (count);
    slope.resize (count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto c = evaluate (tubeType, plate, bottom + static_cast<double> (i) * step);
        current[i] = c.ia;
        slope[i] = c.dVg;
    }
    gmQuiescent = evaluate (tubeType, plate, quiescent).dVg;
    // Bias for each gain reduction: gm(bias) / gm(quiescent) = 10^(-GR/20).
    // gm rises monotonically with grid voltage below the quiescent point.
    bias.resize (static_cast<std::size_t> (maximumReductionDb / reductionStep) + 1);
    for (std::size_t i = 0; i < bias.size(); ++i)
    {
        const auto target = gmQuiescent * std::pow (10.0, -static_cast<double> (i) * reductionStep / 20.0);
        double lo = bottom, hi = quiescent;
        for (int n = 0; n < 100; ++n)
        {
            const auto mid = 0.5 * (lo + hi);
            (evaluate (tubeType, plate, mid).dVg < target ? lo : hi) = mid;
        }
        bias[i] = 0.5 * (lo + hi);
    }
}

double Curve::currentAt (double grid) const noexcept
{
    const auto position = (grid - bottom) / step;
    const auto last = current.size() - 1;
    // Linear beyond the table (grid far past cut-off or far positive).
    if (! (position > 0)) return std::max (0.0, current.front() + (grid - bottom) * slope.front());
    if (position >= static_cast<double> (last)) return current[last] + (grid - (bottom + static_cast<double> (last) * step)) * slope[last];
    const auto i = static_cast<std::size_t> (position);
    const auto t = position - static_cast<double> (i);
    const auto t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * current[i] + (t3 - 2 * t2 + t) * step * slope[i]
         + (-2 * t3 + 3 * t2) * current[i + 1] + (t3 - t2) * step * slope[i + 1];
}

double Curve::gmAt (double grid) const noexcept
{
    const auto position = std::clamp ((grid - bottom) / step, 0.0, static_cast<double> (slope.size() - 1));
    const auto i = std::min (static_cast<std::size_t> (position), slope.size() - 2);
    const auto t = position - static_cast<double> (i);
    return slope[i] + t * (slope[i + 1] - slope[i]);
}

double Curve::biasFor (double reductionDb) const noexcept
{
    const auto position = std::clamp (reductionDb / reductionStep, 0.0, static_cast<double> (bias.size() - 1));
    const auto i = std::min (static_cast<std::size_t> (position), bias.size() - 2);
    const auto t = position - static_cast<double> (i);
    return bias[i] + t * (bias[i + 1] - bias[i]);
}

const Curve& curve (Tube tube) noexcept
{
    static const Curve ge5670 (Tube::ge5670, 150.0, -1.968, -30.0);
    static const Curve ge6386 (Tube::ge6386, 100.0, -1.92, -80.0);
    return tube == Tube::ge6386 ? ge6386 : ge5670;
}

void Cell::configure (Settings settings) noexcept
{
    tube = &curve (settings.tube);
    gridVolts = defaultGridVolts * std::pow (10.0, settings.gridLevelDb / 20.0);
    second = 1.0 + settings.imbalance;
    // The pair is balanced (trimmed) at idle, as a technician would: imbalance then
    // shows as even harmonics and as thump when the bias moves.
    const auto idle = tube->currentAt (tube->quiescentGrid());
    idleOffset = idle - second * idle;
    // Small-signal gain is gm(bias) / gm(quiescent): (1 + second) sections share it.
    scale = 1.0 / (gridVolts * tube->quiescentGm() * (1.0 + second));
}

double Cell::process (double x, double reductionDb) const noexcept
{
    const auto bias = tube->biasFor (reductionDb);
    const auto swing = gridVolts * x;
    return (tube->currentAt (bias + swing) - second * tube->currentAt (bias - swing) - idleOffset) * scale;
}
}
