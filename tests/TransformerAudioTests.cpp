#include "helpers/TransformerAudioPrototype.h"
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <complex>
#include <limits>

namespace
{
using Circuit = MagneticPrototype::TransformerAudioCircuit;
bool finite (double x) { return (std::bit_cast<std::uint64_t> (x) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL; }
}

TEST_CASE ("Audio coupling trial law matches canonical play law without committing history", "[transformer-audio]")
{
    Circuit circuit;
    circuit.prepare (48000.0, {});
    bool matches = true, unchanged = true;
    for (int n = 0; n < 512; ++n)
    {
        circuit.processSample (std::sin (n * 0.017));
        const auto states = circuit.getCore().getPlayStates();
        const auto before = circuit.getCore().getMagnetization();
        for (double field : { -100.0, -2.0, -0.01, 0.0, 0.03, 1.0, 90.0 })
        {
            auto copy = circuit.getCore();
            const auto trial = circuit.evaluateTrial (field);
            matches &= std::abs (trial.magnetization - copy.processField (field)) < 1.0e-14 && trial.derivative >= 0.0;
        }
        unchanged &= states == circuit.getCore().getPlayStates()
            && std::bit_cast<std::uint64_t> (before) == std::bit_cast<std::uint64_t> (circuit.getCore().getMagnetization());
    }
    CHECK (matches);
    CHECK (unchanged);
}

TEST_CASE ("Linear transformer reference matches the bilinear high-pass amplitude and phase", "[transformer-audio]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double frequency : { 20.0, 50.0, 1000.0, 8000.0 })
        {
            Circuit circuit;
            circuit.prepare (rate, {}, true);
            std::complex<double> input {}, output {};
            const auto count = static_cast<int> (rate);
            for (int n = 0; n < count * 2; ++n)
            {
                const auto angle = 2.0 * juce::MathConstants<double>::pi * frequency * n / rate;
                const auto x = 0.001 * std::cos (angle), y = circuit.processSample (x);
                if (n >= count)
                {
                    const auto phasor = std::polar (1.0, -angle);
                    input += x * phasor;
                    output += y * phasor;
                }
            }
            const auto omega = 2.0 * rate * std::tan (juce::MathConstants<double>::pi * frequency / rate);
            const auto jw = std::complex<double> (0.0, omega);
            const auto expected = jw / (2.0 * juce::MathConstants<double>::pi * MagneticPrototype::AudioSettings::cornerHz + jw);
            CAPTURE (rate, frequency);
            CHECK (std::abs (output / input - expected) < 1.0e-11);
        }
}

TEST_CASE ("Voltage coupling obeys its committed-state equation and preserves silent remanence", "[transformer-audio]")
{
    Circuit circuit;
    circuit.prepare (48000.0, {});
    double worst = 0.0, outputIdentityError = 0.0, previousVoltage = 0.0;
    for (int n = 0; n < 48000; ++n)
    {
        const auto oldField = circuit.getField(), oldFlux = circuit.getFlux();
        const auto x = 0.7 * std::sin (n * 0.007) + (n < 24000 ? 0.2 : 0.0);
        const auto y = circuit.processSample (x);
        const auto dt = 1.0 / 48000.0;
        const auto equation = circuit.getFlux() - oldFlux - dt / (2.0 * circuit.getTimeConstant())
            * (circuit.getDrive() * (x + previousVoltage) - oldField - circuit.getField());
        worst = std::max (worst, std::abs (equation));
        outputIdentityError = std::max (outputIdentityError, std::abs (x - circuit.getField() / circuit.getDrive() - y));
        previousVoltage = x;
    }
    CHECK (worst < 5.0e-12);
    CHECK (outputIdentityError < 1.0e-14);
    CHECK (circuit.getDiagnostics().solverFailures == 0);
    circuit.reset();
    double dc = 0.0, silentPeak = 0.0;
    for (int n = 0; n < 48000; ++n) dc = circuit.processSample (0.2);
    CHECK (std::abs (dc) < 1.0e-10);
    for (int n = 0; n < 48000; ++n)
    {
        const auto y = circuit.processSample (0.0);
        if (n >= 48000 - 1024) silentPeak = std::max (silentPeak, std::abs (y));
    }
    CHECK (silentPeak < 1.0e-10);
    CHECK (std::abs (circuit.getMagnetization()) > 0.001);
    circuit.reset();
    CHECK (circuit.processSample (0.0) == 0.0);
}

TEST_CASE ("Small-signal coupled audio agrees with its independently rendered linear reference", "[transformer-audio]")
{
    using namespace TransformerAudioPrototype;
    using namespace MagneticValidation;
    for (double frequency : { 20.0, 50.0, 1000.0, 8000.0 })
    {
        const auto bin = static_cast<int> (std::round (frequency * windowSize / 48000.0));
        const Waveform input = [=] (double n) { return 0.001 * std::sin (2.0 * juce::MathConstants<double>::pi * bin * n / windowSize); };
        const auto actual = TransformerAudioPrototype::render (48000.0, input, 16);
        const auto reference = TransformerAudioPrototype::render (48000.0, input, 64, true, true);
        const auto a = spectrum (actual.trace.samples), b = spectrum (reference.trace.samples);
        const auto errorDb = 20.0 * std::log10 (std::abs (a[static_cast<std::size_t> (bin)] / b[static_cast<std::size_t> (bin)]));
        CAPTURE (frequency, errorDb);
        CHECK (std::abs (errorDb) < 0.01);
        CHECK (actual.diagnostics.solverFailures == 0);
    }
}

TEST_CASE ("Coupled audio is finite at extreme settings and invalid samples cannot poison history", "[transformer-audio]")
{
    bool valid = true;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double memory : { 0.0, 0.35, 1.0 })
            for (double width : { 0.05, 1.0, 4.0 })
                for (double drive : { -24.0, 0.0, 12.0 })
                {
                    Circuit circuit;
                    circuit.prepare (rate * 16.0, { 30.0, drive, 16.0, { memory, width } });
                    std::uint32_t random = 12345;
                    for (int n = 0; n < 2048; ++n)
                    {
                        random = random * 1664525U + 1013904223U;
                        const auto noise = static_cast<double> (random) / 4294967295.0 * 32.0 - 16.0;
                        const auto y = circuit.processSample (n < 512 ? 16.0 : n < 1024 ? -16.0 : n < 1536 ? noise : 1.0e-20);
                        valid &= finite (y) && std::abs (y) < 64.0 && finite (circuit.getFlux());
                    }
                    CAPTURE (rate, memory, width, drive, circuit.getDiagnostics().maximumEquationResidual);
                    CHECK (circuit.getDiagnostics().solverFailures == 0);
                }
    CHECK (valid);
    Circuit circuit;
    circuit.prepare (std::numeric_limits<double>::quiet_NaN(), { std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(), -1.0, { -1.0, std::numeric_limits<double>::quiet_NaN() } });
    for (double x : { std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1.0e300, -1.0e300, 0.2, 0.0 })
        CHECK (finite (circuit.processSample (x)));
    CHECK (circuit.getDiagnostics().invalidInputs == 3);
    CHECK (circuit.getDiagnostics().limitedInputs == 2);
}

TEST_CASE ("Oversampled voltage coupling is stereo independent resettable and block-size invariant", "[transformer-audio]")
{
    using namespace TransformerAudioPrototype;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Processor whole (rate, 16, 2), pieces (rate, 16, 2);
        juce::AudioBuffer<double> reference (2, 4096), split (2, 4096);
        for (int n = 0; n < 4096; ++n)
        {
            const auto value = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 73.0 * n / rate);
            reference.setSample (0, n, value);
            split.setSample (0, n, value);
            reference.setSample (1, n, 0.0);
            split.setSample (1, n, 0.0);
        }
        whole.process (reference); // Oversized blocks use bounded prepared chunks.
        int start = 0;
        for (int size : { 1, 17, 256, 3822 })
        {
            std::array<double*, 2> pointers { split.getWritePointer (0, start), split.getWritePointer (1, start) };
            juce::AudioBuffer<double> block (pointers.data(), 2, size);
            pieces.process (block);
            start += size;
        }
        REQUIRE (start == 4096);
        double maximumError = 0.0;
        bool valid = true;
        for (int n = 0; n < 4096; ++n)
        {
            maximumError = std::max (maximumError, std::abs (split.getSample (0, n) - reference.getSample (0, n)));
            valid &= finite (split.getSample (0, n)) && std::abs (split.getSample (1, n)) < 1.0e-15;
        }
        CHECK (maximumError < 1.0e-12);
        CHECK (valid);
        CHECK (whole.getCircuit().getDiagnostics().solverFailures == 0);
        whole.reset();
        reference.clear();
        whole.process (reference);
        CHECK (reference.getMagnitude (0, 4096) == 0.0);
        Processor mono (rate, 16, 1);
        juce::AudioBuffer<double> silentMono (1, 1);
        silentMono.clear();
        mono.process (silentMono);
        CHECK (silentMono.getSample (0, 0) == 0.0);
    }
}
