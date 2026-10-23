#include "MagneticValidation.h"
#include <numeric>

namespace MagneticValidation
{
namespace
{
constexpr auto pi = juce::MathConstants<double>::pi;

double db (double amplitude) { return 20.0 * std::log10 (std::max (amplitude, 1.0e-15)); }
double peakOf (const std::vector<double>& values)
{
    auto peak = 0.0;
    for (auto value : values)
        peak = std::max (peak, std::abs (value));
    return peak;
}
double gaussian (double x) noexcept { return std::abs (x) > 9.0 ? 0.0 : std::exp (-0.5 * x * x); }
double decay (double t) noexcept
{
    constexpr double sigma = 0.00008, tau = 0.0015;
    if (t < -9.0 * sigma || t > 32.0 * tau)
        return 0.0;
    return 0.5 * std::exp (sigma * sigma / (2.0 * tau * tau) - t / tau)
        * std::erfc ((sigma * sigma / tau - t) / (std::sqrt (2.0) * sigma));
}
double bessel0 (double x)
{
    auto sum = 1.0, term = 1.0;
    for (int i = 1; i < 64; ++i)
    {
        term *= x * x / (4.0 * i * i);
        sum += term;
        if (term < sum * 1.0e-17)
            break;
    }
    return sum;
}
std::array<double, alignmentRadius * 2 + 1> alignmentKernel (double fraction)
{
    std::array<double, alignmentRadius * 2 + 1> result {};
    constexpr double beta = 12.0;
    const auto normalizer = bessel0 (beta);
    for (int k = -alignmentRadius; k <= alignmentRadius; ++k)
    {
        const auto x = static_cast<double> (k) - fraction;
        const auto position = x / alignmentRadius;
        if (std::abs (position) >= 1.0)
            continue;
        const auto sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
        result[static_cast<std::size_t> (k + alignmentRadius)] = sinc
            * bessel0 (beta * std::sqrt (1.0 - position * position)) / normalizer;
    }
    const auto total = std::accumulate (result.begin(), result.end(), 0.0);
    for (auto& value : result)
        value /= total;
    return result;
}
Trace align (const std::vector<double>& raw, int start, double latency, int length = windowSize)
{
    const auto integer = static_cast<int> (std::floor (latency));
    const auto kernel = alignmentKernel (latency - integer);
    Trace result;
    result.latencySamples = latency;
    result.samples.resize (static_cast<std::size_t> (length));
    for (int n = 0; n < length; ++n)
    {
        auto value = 0.0;
        for (int k = -alignmentRadius; k <= alignmentRadius; ++k)
            value += raw[static_cast<std::size_t> (start + n + integer + k)] * kernel[static_cast<std::size_t> (k + alignmentRadius)];
        result.samples[static_cast<std::size_t> (n)] = value;
    }
    return result;
}

double bandPower (const std::vector<std::complex<double>>& bins, double rate)
{
    auto result = std::norm (bins[0]); // Include DC so remanence differences cannot hide.
    const auto last = std::min (windowSize / 2 - 1, static_cast<int> (20000.0 * windowSize / rate));
    for (int i = 1; i <= last; ++i)
        result += 2.0 * std::norm (bins[static_cast<std::size_t> (i)]);
    return result;
}
double bandDifference (const Trace& a, const Trace& b, double rate)
{
    auto difference = a.samples;
    for (std::size_t i = 0; i < difference.size(); ++i)
        difference[i] -= b.samples[i];
    return db (std::sqrt (bandPower (spectrum (difference), rate) / std::max (bandPower (spectrum (b.samples), rate), 1.0e-30)));
}
}

Trace alignTrace (const std::vector<double>& raw, int start, double latency, int length)
{
    return align (raw, start, latency, length);
}
const char* name (Signal signal) noexcept
{
    switch (signal)
    {
        case Signal::lowHigh: return "low_high";
        case Signal::closeHigh: return "close_high";
        case Signal::sixTone: return "six_tone";
        case Signal::impulse: return "bandlimited_impulse";
        case Signal::lowBurst: return "low_burst";
        case Signal::highBurst: return "high_burst";
        case Signal::gaussian: return "gaussian_pulse";
        case Signal::bipolar: return "bipolar_pulse";
        case Signal::fastDecay: return "fast_attack_decay";
    }
    return "unknown";
}

Stimulus::Stimulus (Signal type, double sampleRate, double fieldPeak) : signal (type), rate (sampleRate), peak (fieldPeak)
{
    constexpr std::array frequencies { 80.0, 300.0, 1000.0, 3000.0, 7000.0, 15000.0 };
    toneCount = signal == Signal::sixTone ? 6 : periodic() ? 2 : 0;
    for (int i = 0; i < toneCount; ++i)
    {
        const auto frequency = signal == Signal::lowHigh ? (i == 0 ? 60.0 : 7000.0)
            : signal == Signal::closeHigh ? (i == 0 ? 19000.0 : 20000.0) : frequencies[static_cast<std::size_t> (i)];
        const auto bin = 3 * static_cast<int> (std::round (frequency * windowSize / (rate * 3.0)));
        bins[static_cast<std::size_t> (i)] = std::clamp (bin, 3, 3 * static_cast<int> (std::floor (20000.0 * windowSize / (rate * 3.0))));
        weights[static_cast<std::size_t> (i)] = signal == Signal::lowHigh ? (i == 0 ? 0.8 : 0.2) : 1.0 / toneCount;
        phases[static_cast<std::size_t> (i)] = signal == Signal::sixTone ? pi * i * (i - 1) / toneCount : 0.0;
    }
    if (signal == Signal::fastDecay)
    {
        auto maximum = 0.0;
        for (int i = 0; i <= 8192; ++i)
            maximum = std::max (maximum, decay (-0.00072 + i * 0.002 / 8192));
        decayScale = 1.0 / maximum;
    }
}
double Stimulus::at (double sample) const noexcept
{
    if (periodic())
    {
        auto result = 0.0;
        for (int i = 0; i < toneCount; ++i)
        {
            const auto index = static_cast<std::size_t> (i);
            result += weights[index] * std::sin (2.0 * pi * bins[index] * sample / windowSize + phases[index]);
        }
        return peak * result;
    }
    const auto offset = sample - (windowSize * 0.25 + 0.25);
    const auto t = offset / rate;
    switch (signal)
    {
        case Signal::impulse:
        {
            const auto harmonics = std::floor (18000.0 * windowSize / rate);
            const auto x = 2.0 * pi * offset / windowSize;
            const auto denominator = std::sin (x * 0.5);
            return peak * (std::abs (denominator) < 1.0e-12 ? 1.0
                : (std::sin ((harmonics + 0.5) * x) / denominator - 1.0) / (2.0 * harmonics));
        }
        case Signal::lowBurst: return peak * gaussian (t / 0.0015) * std::cos (2.0 * pi * 730.0 * t);
        case Signal::highBurst: return peak * gaussian (t / 0.00035) * std::cos (2.0 * pi * 10000.0 * t);
        case Signal::gaussian: return peak * gaussian (t / 0.00015);
        case Signal::bipolar: return peak * (gaussian (t / 0.00015) - gaussian ((t - 0.004) / 0.00015));
        case Signal::fastDecay: return peak * decayScale * decay (t);
        case Signal::lowHigh:
        case Signal::closeHigh:
        case Signal::sixTone: return 0.0;
        default: return 0.0;
    }
}

std::vector<std::complex<double>> spectrum (const std::vector<double>& input)
{
    jassert (input.size() == windowSize);
    std::vector<float> data (windowSize * 2, 0.0f);
    for (int i = 0; i < windowSize; ++i)
        data[static_cast<std::size_t> (i)] = static_cast<float> (input[static_cast<std::size_t> (i)]);
    juce::dsp::FFT fft (fftOrder);
    fft.performRealOnlyForwardTransform (data.data());
    std::vector<std::complex<double>> result (windowSize / 2 + 1);
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = { data[i * 2] / static_cast<double> (windowSize), data[i * 2 + 1] / static_cast<double> (windowSize) };
    return result;
}
std::vector<double> audioBand (const std::vector<double>& input, double rate)
{
    std::vector<float> data (windowSize * 2, 0.0f);
    for (int i = 0; i < windowSize; ++i)
        data[static_cast<std::size_t> (i)] = static_cast<float> (input[static_cast<std::size_t> (i)]);
    juce::dsp::FFT fft (fftOrder);
    fft.performRealOnlyForwardTransform (data.data());
    const auto last = std::min (windowSize / 2 - 1, static_cast<int> (20000.0 * windowSize / rate));
    for (int i = last + 1; i < windowSize - last; ++i)
    {
        data[static_cast<std::size_t> (i * 2)] = 0.0f;
        data[static_cast<std::size_t> (i * 2 + 1)] = 0.0f;
    }
    fft.performRealOnlyInverseTransform (data.data());
    std::vector<double> result (windowSize);
    std::copy_n (data.data(), windowSize, result.data());
    return result;
}
Metrics measure (const Trace& output, const Trace& reference, const Stimulus& input)
{
    Metrics result;
    result.bandResidualDb = bandDifference (output, reference, input.rate);
    auto errorPower = 0.0, referencePower = 0.0;
    for (std::size_t i = 0; i < output.samples.size(); ++i)
    {
        const auto difference = output.samples[i] - reference.samples[i];
        errorPower += difference * difference;
        referencePower += reference.samples[i] * reference.samples[i];
        result.maximumError = std::max (result.maximumError, std::abs (difference));
    }
    result.fullResidualDb = db (std::sqrt (errorPower / std::max (referencePower, 1.0e-30)));
    const auto referencePeak = std::max (peakOf (reference.samples), 1.0e-15);
    result.maximumError /= referencePeak;
    result.peakErrorDb = db (peakOf (output.samples) / referencePeak);
    // Keep audible-band peak/convergence tests independent of near-Nyquist
    // transition aliases. Preserve the unbandlimited metrics as diagnostics.
    auto difference = output.samples;
    for (std::size_t i = 0; i < difference.size(); ++i)
        difference[i] -= reference.samples[i];
    const auto referenceBand = audioBand (reference.samples, input.rate);
    const auto bandPeak = std::max (peakOf (referenceBand), 1.0e-15);
    result.bandMaximumError = peakOf (audioBand (difference, input.rate)) / bandPeak;
    result.bandPeakErrorDb = db (peakOf (audioBand (output.samples, input.rate)) / bandPeak);
    if (input.periodic())
    {
        const auto bins = spectrum (output.samples);
        auto offGrid = 0.0;
        for (int i = 1; i < windowSize / 2 && i * input.rate / windowSize <= 20000.0; ++i)
            if (i % 3 != 0 && i * input.rate / windowSize >= 20.0)
                offGrid += 2.0 * std::norm (bins[static_cast<std::size_t> (i)]);
        result.offGridDb = db (std::sqrt (offGrid / std::max (bandPower (spectrum (reference.samples), input.rate), 1.0e-30)));
    }
    if (input.silentTail())
    {
        constexpr int tailSize = 1024;
        const auto outputMean = std::accumulate (output.samples.end() - tailSize, output.samples.end(), 0.0) / tailSize;
        const auto referenceMean = std::accumulate (reference.samples.end() - tailSize, reference.samples.end(), 0.0) / tailSize;
        result.tailErrorDb = db (std::abs (outputMean - referenceMean) / referencePeak);
        auto drift = 0.0;
        for (auto i = output.samples.size() - tailSize; i < output.samples.size(); ++i)
            drift = std::max (drift, std::abs (output.samples[i] - outputMean));
        result.tailDriftDb = db (drift / referencePeak);
    }
    return result;
}
double alignmentErrorDb (double fraction, double frequency)
{
    const auto kernel = alignmentKernel (fraction);
    std::complex<double> value {};
    for (int k = -alignmentRadius; k <= alignmentRadius; ++k)
        value += kernel[static_cast<std::size_t> (k + alignmentRadius)] * std::polar (1.0, 2.0 * pi * frequency * (k - fraction));
    return db (std::abs (value - std::complex<double> (1.0, 0.0)));
}
}
