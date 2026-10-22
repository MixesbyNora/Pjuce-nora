#pragma once

#include "ResearchHarness.h"
#include <complex>
#include <numbers>
#include <numeric>

// Offline analysis only. Deliberately independent of the frozen float-FFT
// helpers, which are still called unchanged for the original acceptance screen.
namespace ShuntResearch
{
inline constexpr double rate = 48000, pi = std::numbers::pi;
inline constexpr std::size_t sineSize = 65536;
inline constexpr std::array bandEdges { 20.0, 250.0, 2000.0, 8000.0, 20000.0 };
inline double powerDb (double x) { return 10 * std::log10 (std::max (x, 1.0e-30)); }
inline double db (double x) { return 20 * std::log10 (std::max (std::abs (x), 1.0e-15)); }
inline std::vector<std::complex<double>> fft (std::span<const double> input)
{
    const auto size = input.size();
    jassert (size > 0 && (size & (size - 1)) == 0);
    std::vector<std::complex<double>> bins (input.begin(), input.end());
    for (std::size_t i = 1, j = 0; i < size; ++i)
    {
        auto bit = size >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (bins[i], bins[j]);
    }
    for (std::size_t width = 2; width <= size; width <<= 1)
    {
        const auto root = std::polar (1.0, -2 * pi / static_cast<double> (width));
        for (std::size_t start = 0; start < size; start += width)
        {
            std::complex<double> phase { 1, 0 };
            for (std::size_t k = 0; k < width / 2; ++k)
            {
                const auto a = bins[start + k], b = phase * bins[start + k + width / 2];
                bins[start + k] = a + b; bins[start + k + width / 2] = a - b; phase *= root;
            }
        }
    }
    for (auto& value : bins) value /= static_cast<double> (size);
    return bins;
}
inline double relativeDifference (std::span<const double> a, std::span<const double> b)
{
    double error = 0, power = 0;
    for (std::size_t i = 0; i < a.size(); ++i) { error += std::pow (a[i] - b[i], 2); power += b[i] * b[i]; }
    return powerDb (error / std::max (power, 1.0e-30));
}
inline double bandDifference (std::span<const double> a, std::span<const double> b)
{
    std::size_t size = 1;
    while (size < a.size()) size <<= 1;
    std::vector<double> error (size, 0), reference (size, 0);
    for (std::size_t n = 0; n < a.size(); ++n) { error[n] = a[n] - b[n]; reference[n] = b[n]; }
    const auto bins = fft (error), ref = fft (reference);
    double errorPower = std::norm (bins[0]), referencePower = std::norm (ref[0]);
    for (std::size_t k = 1; k < size / 2 && static_cast<double> (k) * rate / static_cast<double> (size) <= 20000; ++k)
    {
        errorPower += 2 * std::norm (bins[k]); referencePower += 2 * std::norm (ref[k]);
    }
    return powerDb (errorPower / std::max (referencePower, 1.0e-30));
}
struct Split
{
    double fundamental = 0, harmonic = 0, nonHarmonic = 0, residual = 0;
};
inline std::array<Split, 4> split (std::span<const double> output, std::span<const double> reference, int fundamentalBin)
{
    const auto bins = fft (output), ref = fft (reference);
    std::array<Split, 4> result {};
    for (std::size_t k = 1; k < output.size() / 2; ++k)
    {
        const auto frequency = static_cast<double> (k) * rate / static_cast<double> (output.size());
        for (std::size_t band = 0; band < result.size(); ++band)
            if (frequency >= bandEdges[band] && frequency < bandEdges[band + 1])
            {
                auto& r = result[band];
                const auto energy = 2 * std::norm (bins[k]);
                if (static_cast<int> (k) == fundamentalBin) r.fundamental += energy;
                else if (static_cast<int> (k) % fundamentalBin == 0) r.harmonic += energy;
                else r.nonHarmonic += energy;
                r.residual += 2 * std::norm (bins[k] - ref[k]);
            }
    }
    return result;
}
// Exact stimulus equations copied from the previous spike's ListeningRenders.cpp.
// These are synthetic fixtures, not recordings. The bass note boundaries and
// drums' noise are intentionally preserved, including their broadband content.
inline double material (int type, int channel, double sample)
{
    const auto time = sample / rate;
    if (time < 0 || time >= 1) return 0;
    const auto fade = std::min ({ 1.0, time / 0.01, (1 - time) / 0.025 });
    const auto beat = std::fmod (time, 0.25);
    if (type == 0)
    {
        constexpr std::array notes { 55.0, 65.4064, 82.4069, 73.4162 };
        const auto hz = notes[static_cast<std::size_t> (static_cast<int> (time * 4) % 4)] * (channel == 0 ? 1.0 : 1.002);
        return fade * 0.8 * (1 - std::exp (-beat / 0.003)) * std::exp (-beat / 0.2)
            * (std::sin (2 * pi * hz * beat) + 0.2 * std::sin (4 * pi * hz * beat));
    }
    if (type == 1)
    {
        auto state = static_cast<std::uint32_t> (std::max (0.0, sample)) * 1664525U + 1013904223U + static_cast<std::uint32_t> (channel) * 1234567U;
        state ^= state >> 16; state *= 2246822519U;
        const auto noise = static_cast<double> (state) / 4294967295.0 * 2 - 1;
        return fade * (0.75 * std::exp (-beat * 30) * std::sin (2 * pi * (55 * beat + 1.5 * (1 - std::exp (-beat * 50))))
            + 0.2 * noise * std::exp (-beat * 70));
    }
    const auto hz = 120 * (1 + 0.015 * std::sin (2 * pi * 5 * time));
    double value = 0;
    for (int harmonic = 1; harmonic <= 40; ++harmonic)
    {
        const auto f = harmonic * 120.0;
        const auto weight = std::exp (-std::pow ((f - 700) / 250, 2)) + 0.7 * std::exp (-std::pow ((f - 1200) / 300, 2)) + 0.3 * std::exp (-std::pow ((f - 2600) / 500, 2));
        value += weight / harmonic * std::sin (2 * pi * harmonic * hz * time + channel * 0.015);
    }
    return fade * 1.5 * (0.6 + 0.4 * std::sin (2 * pi * 3 * time)) * value;
}
inline constexpr double sweepSeconds = 8;
inline double sweepFrequency (double time) { return 20 * std::exp (std::log (1000.0) * time / sweepSeconds); }
inline double sweep (double sample)
{
    const auto time = sample / rate;
    if (time < 0 || time >= sweepSeconds) return 0;
    const auto growth = std::log (1000.0) / sweepSeconds;
    const auto fade = std::min ({ 1.0, time / 0.02, (sweepSeconds - time) / 0.02 });
    return 0.8 * fade * std::sin (2 * pi * 20 / growth * std::expm1 (growth * time));
}
}
