#pragma once

#include "../modules/TriodeModel.h"
#include <vector>

// The 5751 voltage amplifier after the gain tubes (docs/TubeStage.md, "5751
// amplifier"). A resistance-coupled common-cathode stage, Manley-style position:
// it lifts the gain tubes' output to line level for the 12BH7 follower.
//   B+ -- Ra -- plate -- (coupling capacitor) -- Rg (next grid's leak) -- ground
//   cathode -- Rk || bypass -- ground; grid = signal.
// Coupling and bypass capacitors are treated as ideal at audio rates, so the
// stage is memoryless: plate voltage against grid signal, tabulated once.
namespace AmpTube
{
struct Design
{
    double supply = 300, plateOhms = 100.0e3, nextGridOhms = 1.0e6, plateAmps = 1.0e-3;
};
class Stage
{
public:
    explicit Stage (Design = {});
    // x in full-scale units at the amplifier's input (after Makeup); returns the
    // output in full-scale units with unity small-signal gain (polarity kept).
    double process (double x) const noexcept;
    double gainDb() const noexcept { return 20.0 * std::log10 (std::abs (smallSignalGain)); }
    double gridVoltsPerUnit() const noexcept { return gridScale; }
    double cathodeOhms() const noexcept { return rk; }
    double quiescentPlate() const noexcept { return plateQuiescent; }

private:
    double plateFor (double grid) const noexcept;
    static constexpr double step = 0.001, range = 30.0;
    Design design;
    double rk = 0, cathode = 0, plateQuiescent = 0, smallSignalGain = -1, gridScale = 1;
    std::vector<double> plate, slope;
};
// The live stage, built on first use (at prepare, never on the audio thread).
const Stage& stage() noexcept;
}
