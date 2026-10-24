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
    // Memoryless: no grid current (see Grid for the live input network).
    double process (double x) const noexcept;
    // Output in full-scale units for an absolute grid voltage.
    double outputForGrid (double grid) const noexcept;
    double cathodeVolts() const noexcept { return cathode; }
    double gainDb() const noexcept { return 20.0 * std::log10 (std::abs (smallSignalGain)); }
    double gridVoltsPerUnit() const noexcept { return gridScale; }
    double cathodeOhms() const noexcept { return rk; }
    double quiescentPlate() const noexcept { return plateQuiescent; }
    // Small-signal plate output resistance, Ra || rp: the source the next grid sees.
    double outputOhms() const noexcept { return plateOutputOhms; }

private:
    double plateFor (double grid) const noexcept;
    static constexpr double step = 0.001, range = 30.0;
    Design design;
    double rk = 0, cathode = 0, plateQuiescent = 0, smallSignalGain = -1, gridScale = 1, plateOutputOhms = 0;
    std::vector<double> plate, slope;
};
// The live stage, built on first use (at prepare, never on the audio thread).
const Stage& stage() noexcept;

// The 5751's input network, one per channel (docs/Overdrive.md): the driving
// source (the gain tubes' plates, inferred 22 kOhm), a 0.1 uF coupling capacitor
// and a 1 MOhm grid leak. Driven past its cathode voltage the grid draws current
// (5670 law), which rounds the peaks and charges the capacitor: the stage biases
// itself colder after big hits and recovers through the leak (blocking).
class Grid
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept { capacitor = previousCurrent = 0; }
    // x in full-scale units, driven `drive` times harder (Headroom; the caller
    // divides the stage's output back). Returns the absolute grid voltage. The
    // capacitor's charge is kept per unit of drive, so a change of drive scales it
    // with the signal instead of leaving an offset.
    double process (double x, double drive = 1.0) noexcept;
    static constexpr double sourceOhms = 22.0e3, couplingFarads = 0.1e-6, leakOhms = 1.0e6;

private:
    double step = 0, capacitor = 0, previousCurrent = 0, scale = 1, cathode = 0;
};
}
