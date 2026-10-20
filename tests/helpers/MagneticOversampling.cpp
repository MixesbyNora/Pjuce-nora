#include "MagneticOversampling.h"
#include <algorithm>
#include <chrono>

namespace MagneticOversampling
{
namespace
{
using Spectrum = MagneticAnalysis::Spectrum;
constexpr auto size = MagneticAnalysis::fftSize;
double db (double power, double reference) { return 10.0 * std::log10 (std::max (power / reference, 1.0e-30)); }
Spectrum transform (const std::vector<double>& input, double latency)
{
    std::vector<float> data (static_cast<std::size_t> (size * 2), 0.0f);
    for (int i = 0; i < size; ++i)
        data[static_cast<std::size_t> (i)] = static_cast<float> (input[static_cast<std::size_t> (i)]);
    juce::dsp::FFT fft (MagneticAnalysis::fftOrder);
    fft.performRealOnlyForwardTransform (data.data());
    Spectrum result;
    result.bins.resize (static_cast<std::size_t> (size / 2 + 1));
    for (std::size_t i = 0; i < result.bins.size(); ++i)
    {
        const auto correction = std::polar (1.0, 2.0 * juce::MathConstants<double>::pi * static_cast<double> (i) * latency / size);
        result.bins[i] = std::complex<double> (data[i * 2], data[i * 2 + 1]) / static_cast<double> (size) * correction;
    }
    return result;
}
double power (const Spectrum& spectrum)
{
    double result = std::norm (spectrum.bins[0]);
    for (int i = 1; i < size / 2; ++i)
        result += 2.0 * std::norm (spectrum.bins[static_cast<std::size_t> (i)]);
    return result;
}
double difference (const Spectrum& a, const Spectrum& b, int lastBin)
{
    double result = std::norm (a.bins[0] - b.bins[0]);
    for (int i = 1; i <= lastBin; ++i)
        result += 2.0 * std::norm (a.bins[static_cast<std::size_t> (i)] - b.bins[static_cast<std::size_t> (i)]);
    return result;
}
}

Processor::Processor (int multiplier, int channels, int capacity, MagneticPrototype::PlayPreisachCore::Parameters parameters)
    : filters (static_cast<std::size_t> (channels))
{
    jassert (channels >= 1 && channels <= 2);
    jassert (std::find (factors.begin(), factors.end(), multiplier) != factors.end());
    if (multiplier > 1)
    {
        filters.clearOversamplingStages();
        for (int stage = 1; stage < multiplier; stage *= 2)
        {
            const auto width = stage == 1 ? firstTransition : laterTransition;
            filters.addOversamplingStage (juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple,
                width, stopbandDb, width, stopbandDb);
        }
    }
    filters.setUsingIntegerLatency (false);
    filters.initProcessing (static_cast<std::size_t> (capacity));
    for (auto& core : cores)
        core.prepare (parameters);
    reset();
}

void Processor::resetCores()
{
    for (auto& core : cores)
        core.reset();
}
void Processor::reset()
{
    filters.reset();
    resetCores();
}
void Processor::process (juce::AudioBuffer<double>& audio, bool identity) noexcept
{
    juce::dsp::AudioBlock<double> block (audio);
    auto high = filters.processSamplesUp (block);
    if (! identity)
        for (std::size_t channel = 0; channel < high.getNumChannels(); ++channel)
        {
            auto* samples = high.getChannelPointer (channel);
            for (std::size_t sample = 0; sample < high.getNumSamples(); ++sample)
                samples[sample] = cores[channel].processField (samples[sample]);
        }
    filters.processSamplesDown (block);
}

Render renderTone (int multiplier, int bin, double peak, MagneticPrototype::PlayPreisachCore::Parameters parameters)
{
    juce::ScopedNoDenormals noDenormals;
    Processor processor (multiplier, 1, blockSize, parameters);
    juce::AudioBuffer<double> audio (1, blockSize);
    std::vector<double> output (static_cast<std::size_t> (size));
    const auto omega = 2.0 * juce::MathConstants<double>::pi * bin / size;
    // First settle the filters with identity processing. Reset only the core,
    // then warm its history for another window. This excludes cold-filter
    // transients that could otherwise leave a different remanent core state.
    for (int pass = 0; pass < 3; ++pass)
    {
        if (pass == 1)
            processor.resetCores();
        for (int start = 0; start < size; start += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
                audio.setSample (0, i, peak * std::sin (omega * (start + i)));
            processor.process (audio, pass == 0);
            if (pass == 2)
                std::copy_n (audio.getReadPointer (0), blockSize, output.data() + start);
        }
    }
    return { transform (output, processor.latency()), processor.latency() };
}

Case compare (double rate, double requestedFrequency, double peak)
{
    const auto bin = std::clamp (2 * static_cast<int> (std::round ((requestedFrequency * size / rate - 1.0) / 2.0)) + 1, 1, size / 2 - 1);
    const auto reference32 = MagneticAnalysis::toneSpectrum (32, bin, peak, {});
    const auto reference64 = MagneticAnalysis::toneSpectrum (64, bin, peak, {});
    auto reference = reference64;
    auto referencePower = power (reference);
    Case result;
    result.sampleRate = rate;
    result.requestedFrequency = requestedFrequency;
    result.frequency = bin * rate / size;
    result.fieldPeak = peak;
    result.referenceInitialDbr = db (difference (reference32, reference64, size / 2 - 1), referencePower);
    result.referenceConvergenceDbr = result.referenceInitialDbr;
    if (result.referenceInitialDbr > -100.0)
    {
        reference = MagneticAnalysis::toneSpectrum (128, bin, peak, {});
        referencePower = power (reference);
        result.referenceConvergenceDbr = db (difference (reference64, reference, size / 2 - 1), referencePower);
        result.referenceFactor = 128;
    }
    const auto passbandLast = static_cast<int> (std::floor (std::min (20000.0, rate * 0.4) * size / rate));
    for (std::size_t f = 0; f < factors.size(); ++f)
    {
        const auto rendered = renderTone (factors[f], bin, peak);
        double aliasPower = 0.0, audioBandAliasPower = 0.0, peakAliasPower = 0.0, peakAliasHz = 0.0;
        for (int i = 1; i < size / 2; ++i)
            if (i % bin != 0)
            {
                const auto binPower = 2.0 * std::norm (rendered.spectrum.bins[static_cast<std::size_t> (i)]);
                const auto frequency = i * rate / size;
                aliasPower += binPower;
                if (frequency >= 20.0 && frequency <= 20000.0)
                    audioBandAliasPower += binPower;
                if (binPower > peakAliasPower)
                {
                    peakAliasPower = binPower;
                    peakAliasHz = frequency;
                }
            }
        const auto transfer = rendered.spectrum.bins[static_cast<std::size_t> (bin)] / reference.bins[static_cast<std::size_t> (bin)];
        result.comparisons[f] = { factors[f], db (aliasPower, referencePower),
            db (difference (rendered.spectrum, reference, size / 2 - 1) + std::norm (rendered.spectrum.bins.back()), referencePower),
            db (difference (rendered.spectrum, reference, passbandLast), referencePower),
            20.0 * std::log10 (std::abs (transfer)), std::arg (transfer) * 180.0 / juce::MathConstants<double>::pi,
            rendered.latencySamples, db (audioBandAliasPower, referencePower), peakAliasHz };
    }
    return result;
}

Filter measureFilter (int multiplier)
{
    juce::ScopedNoDenormals noDenormals;
    Processor processor (multiplier);
    juce::AudioBuffer<double> audio (1, blockSize);
    Filter result;
    result.latencySamples = processor.latency();
    result.impulse.resize (static_cast<std::size_t> (size));
    for (int start = 0; start < size; start += blockSize)
    {
        audio.clear();
        if (start == 0)
            audio.setSample (0, 0, 1.0);
        processor.process (audio, true);
        std::copy_n (audio.getReadPointer (0), blockSize, result.impulse.data() + start);
    }
    return result;
}

Response responseAt (const Filter& filter, double rate, double frequency)
{
    std::complex<double> transfer {};
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / rate;
    for (std::size_t i = 0; i < filter.impulse.size(); ++i)
        transfer += filter.impulse[i] * std::polar (1.0, -omega * static_cast<double> (i));
    transfer *= std::polar (1.0, omega * filter.latencySamples);
    return { 20.0 * std::log10 (std::abs (transfer)), std::arg (transfer) * 180.0 / juce::MathConstants<double>::pi };
}

std::array<double, 2> timing (int multiplier)
{
    juce::ScopedNoDenormals noDenormals;
    Processor processor (multiplier);
    juce::AudioBuffer<double> audio (1, blockSize);
    std::array<double, blockSize> input {};
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = 4.0 * std::sin (2.0 * juce::MathConstants<double>::pi * static_cast<double> (i) / blockSize);
    std::array<double, 5> elapsed {};
    double checksum = 0.0;
    constexpr int samples = 32768;
    for (auto& duration : elapsed)
    {
        processor.reset();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < samples; i += blockSize)
        {
            std::copy (input.begin(), input.end(), audio.getWritePointer (0));
            processor.process (audio);
            checksum += audio.getSample (0, blockSize - 1);
        }
        duration = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count() / samples;
    }
    std::sort (elapsed.begin(), elapsed.end());
    return { elapsed[2], checksum };
}
}
