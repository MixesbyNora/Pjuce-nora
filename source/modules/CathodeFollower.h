#pragma once
#include "TransformerModule.h"
#include "TriodeModel.h"
#include <bit>
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
    // White cathode follower (owner decision 2026-10-24, docs/Overdrive.md): the
    // upper section's plate resistor drives the lower section's grid through an
    // (ideal) coupling capacitor, so the lower section sinks current when the load
    // pulls back. One section in each position; `bias` is the upper grid,
    // `cathodeOhms` the lower section's bypassed cathode resistor.
    bool pushPull = false;
    double plateOhms = 0, quiescentPlate = 0, quiescentOutput = 0, lowerCathode = 0;
    // Tabulated `tube` for the per-sample solve, or null for the exact law.
    const TriodeTable* table = nullptr;
};
// A follower biased for a quiescent current per section, with the cathode at
// half the supply (the widest symmetric swing). Grid bias solved from the triode law.
inline StageDesign followerDesign (double plateAmps, double couplingFarads, double supply, double sections) noexcept
{
    StageDesign d;
    d.supply = supply; d.sections = sections; d.couplingFarads = couplingFarads; d.table = &rca12bh7aTable();
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
// A White cathode follower at a quiescent current per section: both sections get
// the same plate-to-cathode voltage, the lower grid sits at 0 V DC (grid leak) and
// its bypassed cathode resistor sets the current. 1.5 kOhm upper plate resistor.
inline StageDesign pushPullDesign (double plateAmps, double couplingFarads, double supply) noexcept
{
    StageDesign d;
    d.pushPull = true; d.sections = 1; d.supply = supply; d.couplingFarads = couplingFarads; d.table = &rca12bh7aTable();
    d.plateOhms = 1500.0;
    d.quiescentPlate = supply - d.plateOhms * plateAmps;
    const auto gridFor = [&d, plateAmps] (double plateToCathode)
    {
        double lo = -150, hi = 20;
        for (int i = 0; i < 100; ++i)
        {
            const auto mid = 0.5 * (lo + hi);
            (triodeCurrent (d.tube, plateToCathode, mid).ia < plateAmps ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    };
    auto lowerCathode = 5.0;
    for (int i = 0; i < 60; ++i)
    {
        d.quiescentOutput = 0.5 * (d.quiescentPlate + lowerCathode);
        lowerCathode = -gridFor (d.quiescentOutput - lowerCathode);
    }
    d.lowerCathode = lowerCathode;
    d.quiescentOutput = 0.5 * (d.quiescentPlate + lowerCathode);
    d.cathodeOhms = lowerCathode / plateAmps;
    d.bias = d.quiescentOutput + gridFor (d.quiescentPlate - d.quiescentOutput);
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

struct StageDiagnostics
{
    int maximumIterations = 0, maximumInnerIterations = 0;
    std::uint64_t failures = 0, samples = 0, evaluations = 0, innerIterations = 0, fallbacks = 0; // Totals since reset: per-sample cost.
    double minimumPlateMa = 1.0e9, maximumPlateMa = 0;
};

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
        upperPlate = design.quiescentPlate; upperSlope = 0; upperAt = quiescent;
        capacitor = cathode; primaryCurrent = unknown = previousUnknown = lastOutput = 0; diagnostics = {};
    }
    // Quiescent cathode voltage: the coupling capacitor blocks DC.
    static double quiescentCathode (const StageDesign& d) noexcept
    {
        if (d.pushPull) return d.quiescentOutput;
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
        constexpr auto epsilon = std::numeric_limits<double>::epsilon();
        // State of the most recent evaluation; the accepted root is always the
        // last one evaluated, so these are what gets committed.
        LoadTrial t;
        double vk = 0, vc = 0, plateAmps = 0, plate = upperPlate, plateSlope = upperSlope, plateAt = upperAt, goal = 0, derivative = -1, evaluatedAt = 0;
        int evaluations = 0, inner = 0;
        const auto evaluate = [&] (double x) noexcept
        {
            ++evaluations;
            evaluatedAt = x;
            t = load.evaluate (x);
            vc = capacitor + step * (t.ip + primaryCurrent);
            vk = t.vp + vc;
            const auto dVk = t.dVp + step * t.dIp;
            const auto tubeCathode = quiescent + drive * (vk - quiescent); // The tube's side of the level shift.
            double conductance = 0, scale = 0, residual = 0;
            if (design.pushPull)
            {
                // Upper plate for this output voltage (inner monotone root), then KCL at
                // the output: upper current = lower current + load current.
                // Start from the linear prediction of the last solve: usually one correction.
                const auto solved = solveUpperPlate (tubeCathode, grid, plate + plateSlope * (tubeCathode - plateAt));
                plate = solved.plate;
                inner += solved.iterations;
                diagnostics.maximumInnerIterations = std::max (diagnostics.maximumInnerIterations, solved.iterations);
                plateAt = tubeCathode;
                const auto& upper = solved.current;
                const auto lower = tubeCurrent (tubeCathode - design.lowerCathode, plate - design.quiescentPlate - design.lowerCathode);
                plateAmps = upper.ia;
                residual = upper.ia - lower.ia - drive * t.ip;
                plateSlope = (upper.dVa + upper.dVg) / (1 / design.plateOhms + upper.dVa);
                conductance = -(upper.dVa * (plateSlope - 1) - upper.dVg) + lower.dVa + lower.dVg * plateSlope;
                scale = upper.ia + lower.ia;
            }
            else
            {
                const auto tube = tubeCurrent (design.supply - tubeCathode, grid - tubeCathode);
                plateAmps = design.sections * tube.ia;
                residual = plateAmps - tubeCathode / design.cathodeOhms - drive * t.ip;
                conductance = design.sections * (tube.dVa + tube.dVg) + 1 / design.cathodeOhms;
                scale = plateAmps + std::abs (tubeCathode) / design.cathodeOhms;
            }
            derivative = drive * (-conductance * dVk - t.dIp);
            // 1e-11 of the term sizes (including the cathode voltage's own scale): far
            // below audibility, and reachable without bisecting into roundoff.
            goal = 1.0e-18 + 1.0e-11 * (scale + drive * std::abs (t.ip) + conductance * std::abs (tubeCathode));
            return residual;
        };
        // Start from a linear extrapolation of the last two solutions.
        double x = 2 * unknown - previousUnknown, residual = 0;
        bool converged = false;
        if (design.pushPull)
            converged = solveJointly (x, grid, drive, step, t, vc, vk, plateAmps, plate, plateSlope, plateAt, evaluations);
        if (design.pushPull && ! converged) ++diagnostics.fallbacks;
        if (! converged)
        {
            // Fall back from where the joint steps got to (usually close), with the
            // inner plate solve's own prediction.
            plate = upperPlate; plateSlope = upperSlope; plateAt = upperAt;
            if (! isFinite (x)) x = 2 * unknown - previousUnknown;
        }
        // The residual falls as the unknown rises. Phase 1: Newton, tracking a
        // bracket; it converges in 3-4 evaluations at ordinary levels. Bracket ends
        // are flagged, never infinite: Release builds use -Ofast (finite math only).
        double lo = 0, hi = 0, fLo = 0, fHi = 0, reach = 1.0e-3;
        bool haveLo = false, haveHi = false;
        if (! converged) residual = evaluate (x);
        for (int i = 0; i < 6 && ! converged; ++i)
        {
            if (std::abs (residual) <= goal) { converged = true; break; }
            if (residual > 0) { lo = x; fLo = residual; haveLo = true; } else { hi = x; fHi = residual; haveHi = true; }
            const auto next = x - residual / derivative;
            const auto inside = isFinite (next) && (! haveLo || next > lo) && (! haveHi || next < hi);
            if (! inside && haveLo && haveHi) break; // Leave it to the bracketed phase.
            x = inside ? next : (residual > 0 ? x + reach : x - reach);
            if (! inside) reach *= 2;
            residual = evaluate (x);
        }
        if (! converged && std::abs (residual) <= goal) converged = true;
        if (! converged)
        {
            if (residual > 0) { lo = x; fLo = residual; haveLo = true; } else { hi = x; fHi = residual; haveHi = true; }
            // Find a bracket if Newton did not (rare: a very large step in one sample).
            while (! (haveLo && haveHi) && evaluations < maxEvaluations)
            {
                x = haveLo ? lo + reach : hi - reach;
                reach *= 2;
                residual = evaluate (x);
                if (residual > 0) { lo = x; fLo = residual; haveLo = true; } else { hi = x; fHi = residual; haveHi = true; }
            }
            // Phase 2: Brent's method on the bracket. Guaranteed and superlinear even
            // across the tubes' cut-off and positive-grid kinks, so hard transients
            // cost a bounded handful of evaluations instead of dozens of bisections
            // (owner report 2026-10-24: CPU spikes on loud hits; docs/Overdrive.md).
            double a = lo, fa = fLo, b = hi, fb = fHi;
            double c = a, fc = fa, d = b - a, e = d;
            while (evaluations < maxEvaluations)
            {
                bool secant = false;
                if ((fb > 0) == (fc > 0)) { c = a; fc = fa; d = e = b - a; secant = true; }
                if (std::abs (fc) < std::abs (fb)) { a = b; b = c; c = a; fa = fb; fb = fc; fc = fa; secant = true; }
                const auto tolerance = 2 * epsilon * std::abs (b) + 1.0e-15;
                const auto half = 0.5 * (c - b);
                if (std::abs (half) <= tolerance || std::abs (fb) <= goal) break;
                if (std::abs (e) >= tolerance && std::abs (fa) > std::abs (fb))
                {
                    // Secant (two points) or inverse quadratic interpolation (three).
                    double p, q;
                    const auto ratio = fb / fa;
                    if (secant) { p = 2 * half * ratio; q = 1 - ratio; }
                    else
                    {
                        const auto qa = fa / fc, rb = fb / fc;
                        p = ratio * (2 * half * qa * (qa - rb) - (b - a) * (rb - 1));
                        q = (qa - 1) * (rb - 1) * (ratio - 1);
                    }
                    if (p > 0) q = -q; else p = -p;
                    if (2 * p < std::min (3 * half * q - std::abs (tolerance * q), std::abs (e * q))) { e = d; d = p / q; }
                    else { d = half; e = d; }
                }
                else { d = half; e = d; }
                a = b; fa = fb;
                b += std::abs (d) > tolerance ? d : (half > 0 ? tolerance : -tolerance);
                fb = evaluate (b);
            }
            x = b;
            residual = fb;
            // The state must belong to x: Brent's swaps can leave the best point
            // other than the last one evaluated.
            if (std::bit_cast<std::uint64_t> (evaluatedAt) != std::bit_cast<std::uint64_t> (x)) residual = evaluate (x);
        }
        diagnostics.maximumIterations = std::max (diagnostics.maximumIterations, evaluations);
        ++diagnostics.samples;
        diagnostics.evaluations += static_cast<std::uint64_t> (evaluations);
        diagnostics.innerIterations += static_cast<std::uint64_t> (inner);
        // On failure keep the state and hold the last output rather than click to 0.
        if (evaluations >= maxEvaluations || ! isFinite (x) || ! isFinite (residual)) { ++diagnostics.failures; return lastOutput; }
        load.commit (x, t);
        upperPlate = plate; upperSlope = plateSlope; upperAt = plateAt;
        previousUnknown = unknown; unknown = x; capacitor = vc; cathode = vk; primaryCurrent = t.ip; lastOutput = t.output;
        diagnostics.minimumPlateMa = std::min (diagnostics.minimumPlateMa, plateAmps * 1000);
        diagnostics.maximumPlateMa = std::max (diagnostics.maximumPlateMa, plateAmps * 1000);
        return t.output;
    }
    // Worst case per sample: Newton, bracket search and Brent together.
    static constexpr int maxEvaluations = 64;
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
        if (d.pushPull)
        {
            // Linearized White follower: output per upper-grid volt into the load.
            const auto upper = triodeCurrent (d.tube, d.quiescentPlate - d.quiescentOutput, d.bias - d.quiescentOutput);
            const auto lower = triodeCurrent (d.tube, d.quiescentOutput - d.lowerCathode, -d.lowerCathode);
            const auto plateTerm = 1 / d.plateOhms + upper.dVa, lowerTerm = 1 / d.plateOhms + lower.dVg;
            return (upper.dVg / plateTerm) / ((upper.dVg + upper.dVa) / plateTerm + (lower.dVa + 1 / loadOhms) / lowerTerm);
        }
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
    // Upper plate voltage of the White follower for an output (cathode) voltage,
    // with the upper section's current there: KCL at the plate falls
    // monotonically with plate voltage. Newton from a predicted start, then bisection.
    // Push-pull: Newton on the unknown and the upper plate together, one
    // evaluation of each section per step, where the nested solve costs about 2.3
    // upper-section evaluations per outer step (owner report 2026-10-24: CPU
    // spikes on loud hits; docs/Overdrive.md, "CPU"). Converged, it leaves the
    // same state the nested solve would; otherwise the caller falls back to that
    // guaranteed, bracketed solve. KCL at the output (f1) and at the upper plate (f2).
    bool solveJointly (double& x, double grid, double drive, double step, LoadTrial& t, double& vc, double& vk,
        double& plateAmps, double& plate, double& plateSlope, double& plateAt, int& evaluations) noexcept
    {
        const auto plateGoal = 1.0e-15 + 1.0e-13 * design.supply / design.plateOhms;
        for (int i = 0; i < jointSteps; ++i)
        {
            ++evaluations;
            t = load.evaluate (x);
            vc = capacitor + step * (t.ip + primaryCurrent);
            vk = t.vp + vc;
            const auto u = quiescent + drive * (vk - quiescent); // The tube's side of the level shift.
            if (! (u < design.supply)) return false;
            if (i == 0) plate = plate + plateSlope * (u - plateAt);
            plate = std::clamp (plate, u, design.supply);
            const auto upper = tubeCurrent (plate - u, grid - u);
            const auto lower = tubeCurrent (u - design.lowerCathode, plate - design.quiescentPlate - design.lowerCathode);
            const auto f1 = upper.ia - lower.ia - drive * t.ip;
            const auto f2 = (design.supply - plate) / design.plateOhms - upper.ia;
            const auto slope = (upper.dVa + upper.dVg) / (1 / design.plateOhms + upper.dVa);
            const auto conductance = -(upper.dVa * (slope - 1) - upper.dVg) + lower.dVa + lower.dVg * slope;
            const auto goal = 1.0e-18 + 1.0e-11 * (upper.ia + lower.ia + drive * std::abs (t.ip) + conductance * std::abs (u));
            if (std::abs (f1) <= goal && std::abs (f2) <= plateGoal)
            {
                plateAmps = upper.ia; plateSlope = slope; plateAt = u;
                return true;
            }
            const auto du = drive * (t.dVp + step * t.dIp);
            const auto a11 = -(upper.dVa + upper.dVg + lower.dVa) * du - drive * t.dIp, a12 = upper.dVa - lower.dVg;
            const auto a21 = (upper.dVa + upper.dVg) * du, a22 = -1 / design.plateOhms - upper.dVa;
            const auto determinant = a11 * a22 - a12 * a21;
            const auto dx = (f2 * a12 - f1 * a22) / determinant, dPlate = (f1 * a21 - f2 * a11) / determinant;
            if (! isFinite (dx) || ! isFinite (dPlate)) return false;
            x += dx;
            plate += dPlate;
        }
        return false;
    }
    static constexpr int jointSteps = 10; // Hot transients take 5-10; past that, the bracketed solve.
    TriodeCurrent tubeCurrent (double va, double vg) const noexcept
    {
        return design.table != nullptr ? (*design.table) (va, vg) : triodeCurrent (design.tube, va, vg);
    }
    struct UpperPlate { double plate; TriodeCurrent current; int iterations; };
    UpperPlate solveUpperPlate (double output, double grid, double guess) const noexcept
    {
        // At absurd drive the output can sit at or above B+: the upper section is off.
        if (! (output < design.supply)) return { design.supply, {}, 0 };
        double lo = output, hi = design.supply, vp = std::clamp (guess, lo, hi);
        const auto goal = 1.0e-15 + 1.0e-13 * design.supply / design.plateOhms;
        TriodeCurrent c;
        int i = 0;
        for (; i < 60; ++i)
        {
            c = tubeCurrent (vp - output, grid - output);
            const auto g = (design.supply - vp) / design.plateOhms - c.ia;
            if (std::abs (g) <= goal || hi - lo <= 1.0e-12 * design.supply) break;
            (g > 0 ? lo : hi) = vp;
            const auto newton = g / (1 / design.plateOhms + c.dVa);
            if (std::abs (newton) < 1.0e-6 && i < 8)
            {
                // A tiny final step: take it with the current carried to first order
                // (the error is second order, far below the outer tolerance).
                c.ia += c.dVa * newton;
                return { vp + newton, c, i + 1 };
            }
            auto next = vp + newton;
            if (! (next > lo && next < hi) || i >= 8) next = 0.5 * (lo + hi);
            vp = next;
        }
        return { vp, c, std::min (i + 1, 60) };
    }
    double dt = 0, quiescent = 0, cathode = 0, capacitor = 0, primaryCurrent = 0, unknown = 0, previousUnknown = 0, lastOutput = 0, upperPlate = 0, upperSlope = 0, upperAt = 0;
};

}
