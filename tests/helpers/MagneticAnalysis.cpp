#include "MagneticAnalysis.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace MagneticAnalysis
{
namespace
{
double dbRatio (double power, double reference)
{
    return 10.0 * std::log10 (std::max (power / reference, 1.0e-30));
}
double differencePower (const Spectrum& a, const Spectrum& b)
{
    // Both spectra use the base-rate frequency bins. Reference bins at or above
    // base Nyquist are deliberately excluded (ideal offline band limiting).
    double power = std::norm (a.bins[0] - b.bins[0]);
    for (int i = 1; i < fftSize / 2; ++i)
        power += 2.0 * std::norm (a.bins[static_cast<std::size_t> (i)] - b.bins[static_cast<std::size_t> (i)]);
    return power;
}
}

Spectrum toneSpectrum (int multiplier, int carrierBin, double fieldPeak, MagneticPrototype::PlayPreisachCore::Parameters parameters)
{
    juce::ScopedNoDenormals noDenormals;
    const auto count = fftSize * multiplier;
    const auto order = fftOrder + static_cast<int> (std::log2 (multiplier));
    juce::dsp::FFT fft (order);
    std::vector<float> data (static_cast<std::size_t> (count * 2), 0.0f);
    MagneticPrototype::PlayPreisachCore core (parameters);
    const auto omega = 2.0 * juce::MathConstants<double>::pi * carrierBin / count;
    for (int pass = 0; pass < 2; ++pass)
        for (int sample = 0; sample < count; ++sample)
        {
            const auto field = fieldPeak * std::sin (omega * sample);
            const auto value = core.processField (field);
            if (pass == 1)
                data[static_cast<std::size_t> (sample)] = static_cast<float> (value);
        }
    fft.performRealOnlyForwardTransform (data.data());
    Spectrum spectrum;
    spectrum.bins.resize (static_cast<std::size_t> (fftSize / 2 + 1));
    for (std::size_t i = 0; i < spectrum.bins.size(); ++i)
        spectrum.bins[i] = { data[i * 2] / static_cast<double> (count), data[i * 2 + 1] / static_cast<double> (count) };
    return spectrum;
}

SamplingResult sampling (double rate, double requestedFrequency, double peak, MagneticPrototype::PlayPreisachCore::Parameters parameters)
{
    // Odd carrier bin and power-of-two FFT size prevent low-order folded
    // harmonics from coinciding with true in-band harmonics.
    const auto bin = std::clamp (2 * static_cast<int> (std::round ((requestedFrequency * fftSize / rate - 1.0) / 2.0)) + 1, 1, fftSize / 2 - 1);
    const auto base = toneSpectrum (1, bin, peak, parameters);
    const auto four = toneSpectrum (4, bin, peak, parameters);
    const auto eight = toneSpectrum (8, bin, peak, parameters);
    double referencePower = std::norm (eight.bins[0]), aliasPower = 0.0;
    for (int i = 1; i < fftSize / 2; ++i)
    {
        referencePower += 2.0 * std::norm (eight.bins[static_cast<std::size_t> (i)]);
        if (i % bin != 0)
            aliasPower += 2.0 * std::norm (base.bins[static_cast<std::size_t> (i)]);
    }
    // Base Nyquist has one real coefficient; the reference rejects that boundary.
    const auto residual = differencePower (base, eight) + std::norm (base.bins.back());
    return { rate, requestedFrequency, bin * rate / fftSize, peak, std::sqrt (referencePower),
        dbRatio (aliasPower, referencePower), dbRatio (residual, referencePower),
        dbRatio (differencePower (four, eight), referencePower) };
}

std::array<double, 3> timing()
{
    juce::ScopedNoDenormals noDenormals;
    std::array<double, 1024> input {};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 4.0 * std::sin (2.0 * juce::MathConstants<double>::pi * static_cast<double> (i) / static_cast<double> (input.size()));
    std::array<double, 5> nanoseconds {};
    double checksum = 0.0;
    constexpr int samples = 32768;
    for (auto& elapsed : nanoseconds)
    {
        MagneticPrototype::PlayPreisachCore core;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < samples; ++i)
            checksum += core.processField (input[static_cast<std::size_t> (i) % input.size()]);
        elapsed = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count() / samples;
    }
    std::sort (nanoseconds.begin(), nanoseconds.end());
    return { nanoseconds[2], checksum, static_cast<double> (sizeof (MagneticPrototype::PlayPreisachCore)) };
}
}
