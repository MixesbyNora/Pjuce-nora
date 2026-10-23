#pragma once
#include <modules/CathodeFollower.h>

// TEST FIXTURE ONLY. The hybrid plan's tube-to-transformer coupling test
// (docs/TubeStage.md): the joint solve against linear, per-block and uncoupled
// interfaces. The stage itself is source/modules/CathodeFollower.h.
namespace TubeCoupling
{
using namespace ModuleSpike;

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
