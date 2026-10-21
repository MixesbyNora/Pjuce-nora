// Standalone sampling driver. The paired end-to-end timing lives in Tests.
#include "helpers/TransformerAudioReference.h"
#include <experimental/TransformerAudioCircuit.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace
{
template <typename Circuit>
void run()
{
    Circuit circuit;
    circuit.prepare (48000.0 * 16.0, {});
    double checksum = 0.0;
    const auto start = std::chrono::steady_clock::now();
    for (int n = 0; n < 24000000; ++n)
        checksum += circuit.processSample (0.7 * std::sin (2.0 * 3.141592653589793 * 60.0 * n / (48000.0 * 16.0)));
    std::printf ("checksum %.17g ns/internal-sample %.2f max-iterations %d failures %llu\n", checksum,
        std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count() / 24000000.0,
        circuit.getDiagnostics().maximumIterations, static_cast<unsigned long long> (circuit.getDiagnostics().solverFailures));
}
}
int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::puts ("usage: profile_transformer_audio reference|candidate");
        return 1;
    }
    const std::string_view mode (argv[1]);
    if (mode == "reference") run<MagneticPrototype::TransformerAudioReference>();
    else if (mode == "candidate") run<MagneticPrototype::TransformerAudioCircuit>();
    else return 1;
    return 0;
}
