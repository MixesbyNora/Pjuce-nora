#pragma once
#include <modules/TransformerModule.h>
#include <modules/TriodeModel.h>

// TEST FIXTURE ONLY. A 12BH7 cathode follower capacitor-coupled to the VTB1148
// output transformer (variant C, live calibration), for the hybrid plan's
// tube-to-transformer coupling test (docs/TubeStage.md). Owner decision: a
// cathode follower, because a common-cathode 12BH7 (about 3.5 kOhm out) into the
// VTB1148's ~3 H primary loses bass below roughly 200 Hz.
//
// Circuit, volts and amperes, primary-referred like the C calibration:
//   B+ -- plate; grid = bias + signal; cathode -- Rk -- ground;
//   cathode -- Cc -- primary: winding Rw, then magnetizing branch || load Rl.
// One trapezoidal step per sample; every unknown follows from the magnetizing
// current, so each sample is one monotone scalar root.
namespace TubeCoupling
{
using namespace ModuleSpike;

struct StageDesign
{
    TriodeParameters tube = rca12bh7a();
    double sections = 2;               // Both 12BH7 sections in parallel (inference: one bottle per channel).
    double supply = 300, bias = 145, cathodeOhms = 7500, couplingFarads = 1000.0e-6;
};
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
        design = d; dt = 1 / rate; load.prepare (rate, p); reset();
    }
    void reset() noexcept
    {
        load.reset();
        cathode = quiescentCathode (design);
        capacitor = cathode; primaryCurrent = unknown = 0; diagnostics = {};
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
    double process (double signal) noexcept
    {
        const auto grid = design.bias + signal;
        const auto step = dt / (2 * design.couplingFarads);
        double lo = -std::numeric_limits<double>::infinity(), hi = std::numeric_limits<double>::infinity(), x = unknown, reach = 1.0e-3;
        LoadTrial t;
        double residual = 0, vk = 0, vc = 0;
        int iteration = 0;
        for (; iteration < 200; ++iteration)
        {
            t = load.evaluate (x);
            vc = capacitor + step * (t.ip + primaryCurrent);
            vk = t.vp + vc;
            const auto dVk = t.dVp + step * t.dIp;
            const auto tube = triodeCurrent (design.tube, design.supply - vk, grid - vk);
            residual = design.sections * tube.ia - vk / design.cathodeOhms - t.ip;
            const auto conductance = design.sections * (tube.dVa + tube.dVg) + 1 / design.cathodeOhms;
            const auto derivative = -conductance * dVk - t.dIp;
            // Roundoff bound from the term sizes, including the cathode voltage's own rounding.
            constexpr auto epsilon = std::numeric_limits<double>::epsilon();
            const auto goal = 1.0e-18 + 256 * epsilon * (design.sections * tube.ia + std::abs (vk) / design.cathodeOhms + std::abs (t.ip) + conductance * std::abs (vk));
            if (std::abs (residual) <= goal) break;
            (residual > 0 ? lo : hi) = x; // Residual falls as the unknown rises.
            if (std::isfinite (hi - lo) && hi - lo <= 4 * epsilon * std::max ({ std::abs (lo), std::abs (hi), 1.0e-12 })) break;
            // As TransformerCircuit: bounded Newton effort, then bisection, so the
            // hysteretic kinks cannot make Newton cycle inside the bracket.
            auto next = iteration < 8 ? x - residual / derivative : 0.5 * (lo + hi);
            if (! (next > lo && next < hi))
            {
                if (std::isfinite (lo) && std::isfinite (hi)) next = 0.5 * (lo + hi);
                else { next = residual > 0 ? x + reach : x - reach; reach *= 2; }
            }
            x = next;
        }
        diagnostics.maximumIterations = std::max (diagnostics.maximumIterations, iteration + 1);
        if (iteration >= 200 || ! std::isfinite (x)) { ++diagnostics.failures; return 0; }
        load.commit (x, t);
        unknown = x; capacitor = vc; cathode = vk; primaryCurrent = t.ip;
        const auto plate = design.sections * triodeCurrent (design.tube, design.supply - vk, grid - vk).ia * 1000;
        diagnostics.minimumPlateMa = std::min (diagnostics.minimumPlateMa, plate);
        diagnostics.maximumPlateMa = std::max (diagnostics.maximumPlateMa, plate);
        return t.output;
    }
    // Small-signal output resistance at the present operating point.
    double outputOhms (double signal) const noexcept
    {
        const auto tube = triodeCurrent (design.tube, design.supply - cathode, design.bias + signal - cathode);
        return 1 / (design.sections * (tube.dVa + tube.dVg) + 1 / design.cathodeOhms);
    }
    double primaryVolts() const noexcept { return cathode - capacitor; }
    double primaryAmps() const noexcept { return primaryCurrent; }
    StageDiagnostics diagnostics;
private:
    StageDesign design;
    Load load;
    double dt = 0, cathode = 0, capacitor = 0, primaryCurrent = 0, unknown = 0;
};

enum class Path { reference, linearInterface, perBlockInterface, uncoupled };
inline const char* name (Path p)
{
    return p == Path::reference ? "reference" : p == Path::linearInterface ? "a-linear" : p == Path::perBlockInterface ? "b-per-block" : "c-uncoupled";
}
// One complete path from grid signal (volts) to load voltage (volts).
//  reference: joint solve of tube, capacitor and C.
//  a: tube into the linear primary; C fed its EMF through the quiescent output resistance.
//  b: as a, with the output resistance re-read every block from the tube's operating point.
//  c: tube into a resistor; C fed from the live calibration's ~0 ohm source.
class CouplingPath
{
public:
    void prepare (Path p, double rate, int blockSamples, const StageDesign& d, const TransformerParameters& calibration)
    {
        path = p; block = blockSamples; unit = transformerUnitVolts (calibration);
        reference.prepare (rate, d, calibration); linear.prepare (rate, d, calibration); resistive.prepare (rate, d, calibration);
        quiescentOhms = linear.outputOhms (0);
        auto c = calibration;
        if (p == Path::linearInterface || p == Path::perBlockInterface) c.sourceOhms = quiescentOhms;
        circuit.prepare (rate, Variant::play3Outer, Topology::shunt, c);
        sourceOhms = c.sourceOhms; blockSum = 0; counter = 0; minimumOhms = maximumOhms = sourceOhms;
    }
    double process (double signal) noexcept
    {
        if (path == Path::reference) return reference.process (signal);
        if (path == Path::uncoupled) return circuit.process (resistive.process (signal) / unit) * unit;
        linear.process (signal);
        const auto emf = linear.primaryVolts() + sourceOhms * linear.primaryAmps();
        const auto out = circuit.process (emf / unit) * unit;
        if (path == Path::perBlockInterface)
        {
            blockSum += linear.outputOhms (signal);
            if (++counter == block)
            {
                sourceOhms = blockSum / block; blockSum = 0; counter = 0;
                circuit.setSourceOhms (sourceOhms);
                minimumOhms = std::min (minimumOhms, sourceOhms); maximumOhms = std::max (maximumOhms, sourceOhms);
            }
        }
        return out;
    }
    StageDiagnostics stageDiagnostics() const noexcept
    {
        return path == Path::reference ? reference.diagnostics : path == Path::uncoupled ? resistive.diagnostics : linear.diagnostics;
    }
    std::uint64_t transformerFailures() const noexcept { return path == Path::reference ? 0 : circuit.diagnostics.failures; }
    double quiescentOhms = 0, minimumOhms = 0, maximumOhms = 0;
private:
    Path path = Path::reference;
    CathodeFollower<MagneticLoad> reference;
    CathodeFollower<InductiveLoad> linear;
    CathodeFollower<ResistiveLoad> resistive;
    TransformerCircuit circuit;
    int block = 1, counter = 0;
    double unit = 1, sourceOhms = 0, blockSum = 0;
};
}
