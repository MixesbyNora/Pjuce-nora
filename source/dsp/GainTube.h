#pragma once

#include "../modules/TriodeModel.h"
#include <vector>

// Vari-mu gain element (docs/GainTube.md): a balanced pair of gain-control triodes
// at a fixed plate voltage. The compressor's gain reduction sets both grids' bias;
// the output is the difference of the two plate currents, normalized so the
// small-signal gain is exactly the requested gain. Distortion then follows the
// tube's real curvature: it rises as the bias moves toward cut-off.
namespace GainTube
{
enum class Tube { ge5670, ge6386 };

// One tube's plate current and transconductance against grid voltage at its
// plate voltage, tabulated once for cubic Hermite interpolation, plus the bias for
// each gain reduction. Built on first use and shared (read-only) by all instances.
class Curve
{
public:
    Curve (Tube, double plateVolts, double quiescentGrid, double minimumGrid);
    double currentAt (double grid) const noexcept;
    double gmAt (double grid) const noexcept;
    double biasFor (double reductionDb) const noexcept;
    double quiescentGrid() const noexcept { return quiescent; }
    double quiescentGm() const noexcept { return gmQuiescent; }
    double plateVolts() const noexcept { return plate; }
    static constexpr double maximumReductionDb = 60.0;

private:
    static constexpr double step = 0.005, top = 20.0, reductionStep = 0.05;
    double plate, quiescent, bottom, gmQuiescent;
    std::vector<double> current, slope, bias;
};
// The class A1 operating points from each sheet: 5670 at 150 V and -1.97 V
// (Rk 240 ohm, 8.2 mA), 6386 at 100 V and -1.92 V (Rk 200 ohm, 9.6 mA).
const Curve& curve (Tube) noexcept;

struct Settings
{
    Tube tube = Tube::ge5670;
    double gridLevelDb = 0.0;  // Relative to the default grid swing (defaultGridVolts).
    double imbalance = 0.0;    // Second section's current ratio minus 1.
};
// Grid peak volts per full-scale unit at 0 dB grid level (docs/GainTube.md).
inline constexpr double defaultGridVolts = 1.0;

// The balanced cell for one channel. Memoryless: the bias follows the compressor.
class Cell
{
public:
    void configure (Settings) noexcept;
    double process (double x, double reductionDb) const noexcept;

private:
    const Curve* tube = &curve (Tube::ge5670);
    double gridVolts = defaultGridVolts, second = 1.0, idleOffset = 0.0, scale = 1.0;
};
}
