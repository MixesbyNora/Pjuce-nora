#pragma once

#include <experimental/PlayPreisachCore.h>
#include <array>
#include <complex>
#include <vector>

namespace MagneticAnalysis
{
inline constexpr int fftOrder = 14;
inline constexpr int fftSize = 1 << fftOrder;
struct Spectrum
{
    std::vector<std::complex<double>> bins;
};
struct SamplingResult
{
    double sampleRate = 0.0, requestedFrequency = 0.0, frequency = 0.0, fieldPeak = 0.0;
    double referenceRms = 0.0;
    double nonHarmonicAliasDbr = 0.0;
    double inBandResidualDbr = 0.0;
    double referenceConvergenceDbr = 0.0;
};
Spectrum toneSpectrum (int multiplier, int carrierBin, double fieldPeak, MagneticPrototype::PlayPreisachCore::Parameters);
SamplingResult sampling (double sampleRate, double requestedFrequency, double fieldPeak,
    MagneticPrototype::PlayPreisachCore::Parameters = {});
std::array<double, 3> timing(); // Median ns / mono sample, checksum, core storage bytes.
}
