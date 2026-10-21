// Extended numerical probe for the full live drive+calibration range.
// Output is offline CSV; the circuit itself does not allocate, lock or log.
#include <experimental/TransformerAudioCircuit.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>

int main (int argc, char** argv)
{
    if (argc != 2) return 1;
    std::ofstream output (argv[1]);
    output << std::setprecision (17) << "host_rate_hz,corner_hz,memory,width,drive_db,samples,failures,maximum_iterations,solver_residual,committed_equation_residual,finite\n";
    std::uint64_t failures = 0;
    int cases = 0, maximumIterations = 0;
    double worst = 0.0;
    bool valid = true;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double corner : { 0.5, 30.0 })
            for (double memory : { 0.0, 0.35, 1.0 })
                for (double width : { 0.05, 1.0, 4.0 })
                    for (double drive : { -36.0, -24.0, -12.0, 0.0, 12.0, 24.0 })
                    {
                        MagneticPrototype::TransformerAudioCircuit circuit;
                        const auto internalRate = rate * MagneticPrototype::AudioSettings::factorForRate (rate);
                        circuit.prepare (internalRate, { corner, 0.0, 16.0, { memory, width } });
                        circuit.setDriveGain (std::pow (10.0, drive / 20.0));
                        double previous = 0.0, equationError = 0.0;
                        bool finite = true;
                        std::uint32_t random = 12345;
                        for (int n = 0; n < 8192; ++n)
                        {
                            // An instantaneous full-span drive reversal is
                            // deliberately harder than the live 20 ms ramp.
                            if (n == 4096) circuit.setDriveGain (std::pow (10.0, (-12.0 - drive) / 20.0));
                            random = random * 1664525U + 1013904223U;
                            const auto noise = static_cast<double> (random) / 4294967295.0 * 32.0 - 16.0;
                            const auto input = n < 1024 ? 16.0 : n < 2048 ? -16.0 : n < 4096 ? noise
                                : n < 6144 ? 16.0 * std::sin (n * 0.017) : 0.0;
                            const auto oldField = circuit.getField(), oldFlux = circuit.getFlux();
                            const auto audio = circuit.processSample (input);
                            finite &= std::isfinite (audio) && std::isfinite (circuit.getFlux());
                            const auto q = 1.0 / (2.0 * internalRate * circuit.getTimeConstant());
                            const auto equation = circuit.getFlux() - oldFlux - q * (circuit.getDrive() * (input + previous) - oldField - circuit.getField());
                            equationError = std::max (equationError, std::abs (equation) / std::max (1.0, std::abs (circuit.getField())));
                            previous = input;
                        }
                        const auto& d = circuit.getDiagnostics();
                        output << rate << ',' << corner << ',' << memory << ',' << width << ',' << drive << ",8192," << d.solverFailures
                               << ',' << d.maximumIterations << ',' << d.maximumEquationResidual << ',' << equationError << ',' << finite << '\n';
                        failures += d.solverFailures;
                        maximumIterations = std::max (maximumIterations, d.maximumIterations);
                        worst = std::max (worst, d.maximumEquationResidual);
                        valid &= finite && d.solverFailures == 0 && d.maximumEquationResidual <= MagneticPrototype::AudioSettings::rootTolerance && equationError < 2.0e-13;
                        ++cases;
                    }
    std::cout << "cases=" << cases << " failures=" << failures << " maximum_iterations=" << maximumIterations << " worst_residual=" << worst << '\n';
    return valid && output.good() ? 0 : 1;
}
