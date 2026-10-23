#pragma once
#include "TransformerModule.h"
#include "TriodeModel.h"
#include <limits>

// A 12BH7 cathode follower capacitor-coupled to a variant C transformer primary,
// solved jointly (docs/TubeStage.md). Owner decisions: a cathode follower, and
// the joint solve for the live output stage (the coupling test's reference).
//
// Circuit, volts and amperes, primary-referred like the C calibration:
//   B+ -- plate; grid = bias + signal; cathode -- Rk -- ground;
//   cathode -- Cc -- primary: winding Rw, then magnetizing branch || load Rl.
// One trapezoidal step per sample; every unknown follows from the magnetizing
// current (or the load current, for a resistor), so each sample is one monotone
// scalar root: bounded Newton, then bisection, as TransformerCircuit.
namespace ModuleSpike
{
struct StageDesign
{
    TriodeParameters tube = rca12bh7a();
    double sections = 2;               // Both 12BH7 sections in parallel (inference: one bottle per channel).
    double supply = 300, bias = 145, cathodeOhms = 7500, couplingFarads = 1000.0e-6;
};
// A follower biased for a quiescent current per section, with the cathode at
// half the supply (the widest symmetric swing). Grid bias solved from the triode law.
inline StageDesign followerDesign (double plateAmps, double couplingFarads, double supply, double sections) noexcept
{
    StageDesign d;
    d.supply = supply; d.sections = sections; d.couplingFarads = couplingFarads;
    const auto cathode = 0.5 * supply;
    d.cathodeOhms = cathode / (sections * plateAmps);
    double lo = -150, hi = 20;
    for (int i = 0; i < 100; ++i)
    {
        const auto mid = 0.5 * (lo + hi);
        (triodeCurrent (d.tube, supply - cathode, mid).ia < plateAmps ? lo : hi) = mid;
    }
    d.bias = cathode + 0.5 * (lo + hi);
    return d;
}
// The live VTB1148 C calibration: 0 dBFS = +18 dBu at the primary, so one
// normalized circuit volt is this many volts.
inline double fullScalePeakVolts() { return std::sqrt (2.0) * 0.7745966692 * std::pow (10.0, 18.0 / 20.0); }
inline double transformerUnitVolts (const TransformerParameters& p) { return fullScalePeakVolts() / std::pow (10.0, p.driveDb / 20.0); }
// Large-signal magnetizing inductance of C: every play operator moving, no saturation.
inline double linearInductance (const TransformerParameters& p)
{
    return TransformerCircuit::impedanceBase * TransformerCircuit::fluxScale * (0.1 + p.saturation / p.knee);
}

struct LoadTrial { double ip = 0, dIp = 0, vp = 0, dVp = 0, output = 0; };

// Primary load with C's magnetizing law (the fully coupled reference).
class MagneticLoad
{
public:
    void prepare (double rate, const TransformerParameters& p) noexcept
    {
        dt = 1 / rate; unit = transformerUnitVolts (p); parameters = p;
        winding = p.loss * TransformerCircuit::impedanceBase; load = p.loadOhms; reset();
    }
    void reset() noexcept { element.reset(); flux = voltage = 0; }
    LoadTrial evaluate (double current) noexcept
    {
        const auto trial = element.evaluate (current * TransformerCircuit::impedanceBase / unit, parameters);
        const auto lambda = unit * TransformerCircuit::fluxScale * trial.flux;
        const auto slope = TransformerCircuit::impedanceBase * TransformerCircuit::fluxScale * trial.derivative;
        trialFlux = lambda;
        const auto v = 2 * (lambda - flux) / dt - voltage, dv = 2 * slope / dt;
        const auto ip = current + v / load, dIp = 1 + dv / load;
        return { ip, dIp, winding * ip + v, winding * dIp + dv, v };
    }
    void commit (double current, const LoadTrial& t) noexcept
    {
        element.commit (current * TransformerCircuit::impedanceBase / unit, parameters);
        flux = trialFlux; voltage = t.output;
    }
private:
    OuterPlayElement element;
    TransformerParameters parameters;
    double dt = 0, unit = 1, winding = 0, load = 1, flux = 0, voltage = 0, trialFlux = 0;
};
// The same primary with a linear magnetizing inductance (the tube's view in candidates a and b).
class InductiveLoad
{
public:
    void prepare (double rate, const TransformerParameters& p) noexcept
    {
        dt = 1 / rate; inductance = linearInductance (p);
        winding = p.loss * TransformerCircuit::impedanceBase; load = p.loadOhms; reset();
    }
    void reset() noexcept { flux = voltage = 0; }
    LoadTrial evaluate (double current) noexcept
    {
        const auto v = 2 * (inductance * current - flux) / dt - voltage, dv = 2 * inductance / dt;
        const auto ip = current + v / load, dIp = 1 + dv / load;
        return { ip, dIp, winding * ip + v, winding * dIp + dv, v };
    }
    void commit (double current, const LoadTrial& t) noexcept { flux = inductance * current; voltage = t.output; }
private:
    double dt = 0, inductance = 1, winding = 0, load = 1, flux = 0, voltage = 0;
};
// A plain resistor (candidate c): the unknown is the load current itself.
class ResistiveLoad
{
public:
    void prepare (double, const TransformerParameters& p) noexcept { resistance = p.loss * TransformerCircuit::impedanceBase + p.loadOhms; }
    void reset() noexcept {}
    LoadTrial evaluate (double current) const noexcept { return { current, 1, resistance * current, resistance, resistance * current }; }
    void commit (double, const LoadTrial&) noexcept {}
private:
    double resistance = 1;
};

struct StageDiagnostics { int maximumIterations = 0; std::uint64_t failures = 0; double minimumPlateMa = 1.0e9, maximumPlateMa = 0; };

template <typename Load>
class CathodeFollower
{
public:
    void prepare (double rate, const StageDesign& d, const TransformerParameters& p) noexcept
    {
        design = d; dt = 1 / rate; load.prepare (rate, p); quiescent = quiescentCathode (design); reset();
    }
    void reset() noexcept
    {
        load.reset();
        cathode = quiescent;
        capacitor = cathode; primaryCurrent = unknown = previousUnknown = lastOutput = 0; diagnostics = {};
    }
    // Quiescent cathode voltage: the coupling capacitor blocks DC.
    static double quiescentCathode (const StageDesign& d) noexcept
    {
        double lo = 0, hi = d.supply;
        for (int i = 0; i < 200; ++i)
        {
            const auto mid = 0.5 * (lo + hi);
            const auto excess = d.sections * triodeCurrent (d.tube, d.supply - mid, d.bias - mid).ia - mid / d.cathodeOhms;
            (excess > 0 ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }
    // Grid signal in volts (added to the bias); returns the load voltage.
    // `drive` runs the tube at that multiple of the load's signal level, as if an
    // ideal level shifter sat between them: the tube sees drive x the grid signal,
    // cathode swing and load current, while the load still sees the follower's own
    // output resistance. 1 is the plain circuit.
    double process (double signal, double drive = 1.0) noexcept
    {
        const auto grid = design.bias + drive * signal;
        const auto step = dt / (2 * design.couplingFarads);
        // Start from a linear extrapolation of the last two solutions (about half
        // the iterations of starting from the last one). Bracket ends are flagged,
        // never infinite: Release builds use -Ofast (finite math only).
        double lo = 0, hi = 0, x = 2 * unknown - previousUnknown, reach = 1.0e-3;
        bool haveLo = false, haveHi = false;
        LoadTrial t;
        double residual = 0, vk = 0, vc = 0, plateAmps = 0;
        int iteration = 0;
        for (; iteration < 200; ++iteration)
        {
            t = load.evaluate (x);
            vc = capacitor + step * (t.ip + primaryCurrent);
            vk = t.vp + vc;
            const auto dVk = t.dVp + step * t.dIp;
            const auto tubeCathode = quiescent + drive * (vk - quiescent); // The tube's side of the level shift.
            const auto tube = triodeCurrent (design.tube, design.supply - tubeCathode, grid - tubeCathode);
            plateAmps = design.sections * tube.ia;
            residual = plateAmps - tubeCathode / design.cathodeOhms - drive * t.ip;
            const auto conductance = design.sections * (tube.dVa + tube.dVg) + 1 / design.cathodeOhms;
            const auto derivative = drive * (-conductance * dVk - t.dIp);
            // Roundoff bound from the term sizes, including the cathode voltage's own rounding.
            constexpr auto epsilon = std::numeric_limits<double>::epsilon();
            const auto goal = 1.0e-18 + 256 * epsilon * (design.sections * tube.ia + std::abs (tubeCathode) / design.cathodeOhms + drive * std::abs (t.ip) + conductance * std::abs (tubeCathode));
            if (std::abs (residual) <= goal) break;
            // Residual falls as the unknown rises.
            if (residual > 0) { lo = x; haveLo = true; } else { hi = x; haveHi = true; }
            const auto bracketed = haveLo && haveHi;
            if (bracketed && hi - lo <= 4 * epsilon * std::max ({ std::abs (lo), std::abs (hi), 1.0e-12 })) break;
            // As TransformerCircuit: bounded Newton effort, then bisection, so the
            // hysteretic kinks cannot make Newton cycle inside the bracket.
            auto next = x - residual / derivative;
            const auto inside = isFinite (next) && (! haveLo || next > lo) && (! haveHi || next < hi);
            if (iteration >= 8 || ! inside)
            {
                if (bracketed) next = 0.5 * (lo + hi);
                else { next = residual > 0 ? x + reach : x - reach; reach *= 2; }
            }
            x = next;
        }
        diagnostics.maximumIterations = std::max (diagnostics.maximumIterations, iteration + 1);
        // On failure keep the state and hold the last output rather than click to 0.
        if (iteration >= 200 || ! isFinite (x)) { ++diagnostics.failures; return lastOutput; }
        load.commit (x, t);
        previousUnknown = unknown; unknown = x; capacitor = vc; cathode = vk; primaryCurrent = t.ip; lastOutput = t.output;
        diagnostics.minimumPlateMa = std::min (diagnostics.minimumPlateMa, plateAmps * 1000);
        diagnostics.maximumPlateMa = std::max (diagnostics.maximumPlateMa, plateAmps * 1000);
        return t.output;
    }
    // Small-signal output resistance at the present operating point.
    double outputOhms (double signal) const noexcept
    {
        const auto tube = triodeCurrent (design.tube, design.supply - cathode, design.bias + signal - cathode);
        return 1 / (design.sections * (tube.dVa + tube.dVg) + 1 / design.cathodeOhms);
    }
    // Small-signal cathode gain at the quiescent point into a resistive load
    // (coupling capacitor shorted, magnetizing branch open).
    static double midbandGain (const StageDesign& d, double loadOhms) noexcept
    {
        const auto vk = quiescentCathode (d);
        const auto tube = triodeCurrent (d.tube, d.supply - vk, d.bias - vk);
        return d.sections * tube.dVg / (d.sections * (tube.dVa + tube.dVg) + 1 / d.cathodeOhms + 1 / loadOhms);
    }
    double primaryVolts() const noexcept { return cathode - capacitor; }
    double primaryAmps() const noexcept { return primaryCurrent; }
    StageDiagnostics diagnostics;
private:
    StageDesign design;
    Load load;
    double dt = 0, quiescent = 0, cathode = 0, capacitor = 0, primaryCurrent = 0, unknown = 0, previousUnknown = 0, lastOutput = 0;
};

}
