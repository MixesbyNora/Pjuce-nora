#include "helpers/MagneticIntervalCore.h"
#include "helpers/MagneticOversampling.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <fstream>
#include <iomanip>
#include <limits>

namespace
{
bool finiteValue (double value)
{
    return (std::bit_cast<std::uint64_t> (value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
}

TEST_CASE ("Interval core matches endpoint history through reversals and parameter extremes", "[magnetic-interval][state]")
{
    for (double memory : { 0.0, 0.35, 1.0 })
        for (double width : { 0.05, 1.0, 4.0 })
        {
            MagneticPrototype::PlayPreisachCore point ({ memory, width });
            MagneticOversampling::IntervalCore interval ({ memory, width });
            auto maximum = 0.0;
            for (int i = 0; i < 4096; ++i)
            {
                const auto field = i < 512 ? 0.0 : 40.0 * std::sin (i * 0.037) * std::sin (i * 0.013);
                const auto expected = point.processField (field);
                const auto average = interval.processField (field);
                REQUIRE (finiteValue (average));
                REQUIRE (std::abs (average) <= 1.0);
                maximum = std::max (maximum, std::abs (expected - interval.getMagnetization()));
                REQUIRE (point.getPlayStates() == interval.getPlayStates());
            }
            CHECK (maximum < 1.0e-14);
            interval.reset();
            CHECK (interval.processField (0.0) == 0.0);
            interval.processField (1.0);
            const auto saved = std::bit_cast<std::uint64_t> (interval.getMagnetization());
            CHECK (std::bit_cast<std::uint64_t> (interval.processField (std::numeric_limits<double>::infinity())) == saved);
            CHECK (std::bit_cast<std::uint64_t> (interval.processField (std::numeric_limits<double>::quiet_NaN())) == saved);
        }
}

TEST_CASE ("Interval mean matches independent dense quadrature including play activation", "[magnetic-interval][integration]")
{
    constexpr int subdivisions = 32768;
    MagneticOversampling::IntervalCore interval;
    MagneticPrototype::PlayPreisachCore dense;
    auto previous = 0.0;
    for (double field : { 0.01, 0.5, 0.7, 0.69, -0.2, -4.0, 4.0, 4.0, 4.00000001, 0.0 })
    {
        auto sum = 0.0;
        for (int i = 0; i < subdivisions; ++i)
        {
            const auto position = (static_cast<double> (i) + 0.5) / subdivisions;
            sum += dense.processField (previous + position * (field - previous));
        }
        dense.processField (field);
        const auto measured = interval.processField (field);
        CAPTURE (previous, field);
        CHECK (measured == Catch::Approx (sum / subdivisions).margin (2.0e-8));
        previous = field;
    }
    for (double value : { -1000.0, -32.0, -0.1, 0.0, 0.1, 32.0, 1000.0 })
    {
        CHECK (MagneticOversampling::IntervalCore::averageTanh (value, value) == Catch::Approx (std::tanh (value)).margin (1.0e-14));
        CHECK (finiteValue (MagneticOversampling::IntervalCore::averageTanh (value, value + 1.0e-12)));
    }
}

TEST_CASE ("Interval reconstruction exposes its added delay and linear response", "[magnetic-interval][filters]")
{
    for (int factor : MagneticOversampling::factors)
    {
        const auto filter = MagneticOversampling::measureFilter (factor, MagneticOversampling::Evaluation::interval);
        const auto point = MagneticOversampling::measureFilter (factor);
        CHECK (filter.latencySamples - point.latencySamples == Catch::Approx (0.5 / factor).margin (1.0e-12));
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double frequency : { 20.0, 1000.0, 10000.0, 20000.0 })
            {
                const auto response = MagneticOversampling::responseAt (filter, rate, frequency);
                const auto base = MagneticOversampling::responseAt (point, rate, frequency);
                const auto expected = 20.0 * std::log10 (std::cos (juce::MathConstants<double>::pi * frequency / (rate * factor)));
                CHECK (response.gainDb - base.gainDb == Catch::Approx (expected).margin (0.001));
                CHECK (std::abs (response.delayRemovedPhaseDegrees) < 0.001);
            }
    }
}

TEST_CASE ("Reconstructed cores preserve block partitions and channel independence", "[magnetic-interval][magnetic-turning][state]")
{
    for (auto method : { MagneticOversampling::Evaluation::interval, MagneticOversampling::Evaluation::turningPoint })
        for (int factor : MagneticOversampling::factors)
        {
            MagneticOversampling::Processor whole (factor, 2, 4096, {}, method);
            MagneticOversampling::Processor pieces (factor, 1, 4096, {}, method);
            juce::AudioBuffer<double> stereo (2, 4096), mono (1, 4096);
            stereo.clear();
            for (int i = 0; i < 4096; ++i)
            {
                const auto value = std::sin (i * 0.31) * 4.0;
                stereo.setSample (0, i, value);
                mono.setSample (0, i, value);
            }
            whole.process (stereo);
            for (int start = 0; start < 4096;)
            {
                const auto count = std::min (1 + start % 127, 4096 - start);
                auto* pointer = mono.getWritePointer (0, start);
                juce::AudioBuffer<double> block (&pointer, 1, count);
                pieces.process (block);
                start += count;
            }
            auto error = 0.0;
            for (int i = 0; i < 4096; ++i)
            {
                error = std::max (error, std::abs (mono.getSample (0, i) - stereo.getSample (0, i)));
                REQUIRE (stereo.getSample (1, i) == 0.0);
            }
            CHECK (error < 1.0e-12);
            whole.reset();
            pieces.reset();
            stereo.clear();
            whole.process (stereo);
            CHECK (stereo.getMagnitude (0, stereo.getNumSamples()) == 0.0);
        }
}

TEST_CASE ("Reconstructed cores stay finite at all rates and extreme fields", "[magnetic-interval][magnetic-turning][stability]")
{
    for (auto method : { MagneticOversampling::Evaluation::interval, MagneticOversampling::Evaluation::turningPoint })
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int factor : MagneticOversampling::factors)
            {
                MagneticOversampling::Processor processor (factor, 2, 4096, {}, method);
                for (int size : { 1, 64, 4096 })
                {
                    juce::AudioBuffer<double> block (2, size);
                    for (int signal = 0; signal < 5; ++signal)
                    {
                        for (int i = 0; i < size; ++i)
                        {
                            const auto value = signal == 0 ? 0.0 : signal == 1 ? 1.0e-12 : signal == 2 ? 32.0
                                : signal == 3 ? -32.0 : 16.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 10000.0 * i / rate);
                            block.setSample (0, i, value);
                            block.setSample (1, i, -value);
                        }
                        processor.process (block);
                        bool valid = true;
                        for (int channel = 0; channel < 2; ++channel)
                            for (int i = 0; i < size; ++i)
                                valid &= finiteValue (block.getSample (channel, i)) && std::abs (block.getSample (channel, i)) < 4.0;
                        REQUIRE (valid);
                    }
                }
            }
}

TEST_CASE ("Turning-point estimate only refines reversals and has bounded reset history", "[magnetic-turning][state]")
{
    MagneticOversampling::TurningPointCore candidate;
    MagneticPrototype::PlayPreisachCore original;
    auto error = 0.0;
    for (int i = 0; i < 1000; ++i)
    {
        const auto field = 4.0 * i / 1000.0;
        error = std::max (error, std::abs (candidate.processField (field) - original.processField (field)));
    }
    CHECK (error == 0.0);
    candidate.reset();
    original.reset();
    for (double field : { 0.0, 1.0 })
    {
        candidate.processField (field);
        original.processField (field);
    }
    original.processField (1.0 + 1.0 / 15.0); // Quadratic through (0, 0), (1, 1), (2, 0.8).
    CHECK (candidate.processField (0.8) == Catch::Approx (original.processField (0.8)).margin (1.0e-14));
    for (double field : { -1.0e300, 1.0e300, 1.0e-300, -1.0e-300, 0.0 })
        CHECK (finiteValue (candidate.processField (field)));
    const auto saved = std::bit_cast<std::uint64_t> (candidate.getMagnetization());
    CHECK (std::bit_cast<std::uint64_t> (candidate.processField (std::numeric_limits<double>::infinity())) == saved);
    CHECK (std::bit_cast<std::uint64_t> (candidate.processField (std::numeric_limits<double>::quiet_NaN())) == saved);
    candidate.reset();
    CHECK (candidate.processField (0.0) == 0.0);
    candidate.reset();
    original.reset();
    for (double field : { -0.999, -0.998, -0.997 })
        CHECK (candidate.processField (field) == Catch::Approx (original.processField (field)).margin (1.0e-14));
}

TEST_CASE ("Turning-point reconstruction reduces the shallow high-frequency history artifacts", "[magnetic-turning][spectrum]")
{
    const auto original = MagneticOversampling::compare (44100.0, 10000.0, 0.05);
    const auto reconstructed = MagneticOversampling::compare (44100.0, 10000.0, 0.05, MagneticOversampling::Evaluation::turningPoint);
    const auto& point8 = original.comparisons[3];
    const auto& refined8 = reconstructed.comparisons[3];
    CHECK (refined8.factor == 8);
    CHECK (refined8.audioBandAliasDbr < -80.0);
    CHECK (refined8.audioBandAliasDbr < point8.audioBandAliasDbr - 10.0);
    CHECK (std::abs (refined8.gainErrorDb) < 0.001);
    CHECK (std::abs (refined8.phaseErrorDegrees) < 0.05);
    // The reconstruction changes magnetic history, not the FIR response.
    const auto filter = MagneticOversampling::measureFilter (8, MagneticOversampling::Evaluation::turningPoint);
    const auto originalFilter = MagneticOversampling::measureFilter (8);
    CHECK (filter.impulse == originalFilter.impulse);
    CHECK (filter.latencySamples == Catch::Approx (originalFilter.latencySamples).margin (1.0e-12));
    const auto startup = MagneticOversampling::compare (96000.0, 1000.0, 1.0, MagneticOversampling::Evaluation::turningPoint);
    CHECK (startup.comparisons[3].passbandResidualDbr < -100.0);
}

TEST_CASE ("Export paired magnetic processing costs", "[magnetic-cost-report]")
{
    const auto* path = std::getenv ("LUMIERE_MAGNETIC_COST_REPORT_DIR");
    if (path == nullptr)
    {
        SUCCEED ("Set LUMIERE_MAGNETIC_COST_REPORT_DIR to export sequential matched-binary cost sweeps.");
        return;
    }
    const auto directory = juce::File (juce::String::fromUTF8 (path));
    REQUIRE (directory.createDirectory().wasOk());
    std::ofstream output (directory.getChildFile ("profiling.csv").getFullPathName().toStdString());
#if defined(JUCE_DEBUG) && JUCE_DEBUG
    constexpr auto configuration = "Debug";
#else
    constexpr auto configuration = "Release";
#endif
    output << "# " << juce::SystemStats::getCpuModel().toStdString() << "; " << configuration
        << "; matched binary; method_index 0=point 1=interval 2=turning; stimulus_index 0=H4_slow_sine 1=H0.05_53_cycles_per_256_sine 2=H4_seeded_noise; 3 sweeps, each median 5 x 32768 mono samples.\n"
        << "sweep,stimulus_index,method_index,factor,ns_per_base_mono_sample,latency_samples,checksum\n" << std::setprecision (12);
    constexpr std::array methods { MagneticOversampling::Evaluation::point, MagneticOversampling::Evaluation::interval, MagneticOversampling::Evaluation::turningPoint };
    constexpr std::array stimuli { MagneticOversampling::TimingInput::slowSine, MagneticOversampling::TimingInput::quietHighSine, MagneticOversampling::TimingInput::noise };
    for (int sweep = 0; sweep < 3; ++sweep)
        for (int stimulusIndex = 0; stimulusIndex < 3; ++stimulusIndex)
            for (std::size_t f = 0; f < MagneticOversampling::factors.size(); ++f)
                for (int index = 0; index < 3; ++index)
                {
                    const auto methodIndex = (index + sweep) % 3;
                    const auto factorIndex = sweep % 2 == 0 ? f : MagneticOversampling::factors.size() - 1 - f;
                    const auto factor = MagneticOversampling::factors[factorIndex];
                    const auto method = methods[static_cast<std::size_t> (methodIndex)];
                    const auto cost = MagneticOversampling::timing (factor, method, stimuli[static_cast<std::size_t> (stimulusIndex)]);
                    const MagneticOversampling::Processor processor (factor, 1, MagneticOversampling::blockSize, {}, method);
                    REQUIRE (cost[0] > 0.0);
                    REQUIRE (finiteValue (cost[1]));
                    output << sweep << ',' << stimulusIndex << ',' << methodIndex << ',' << factor << ',' << cost[0] << ',' << processor.latency() << ',' << cost[1] << '\n';
                }
    REQUIRE (output.good());
}
