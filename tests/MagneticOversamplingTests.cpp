#include "helpers/MagneticOversampling.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <fstream>
#include <iomanip>

namespace
{
bool finiteNumber (double value)
{
    return (std::bit_cast<std::uint64_t> (value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
juce::AudioBuffer<double> input (int channels, int samples)
{
    juce::AudioBuffer<double> buffer (channels, samples);
    std::uint32_t random = 12345;
    for (int i = 0; i < samples; ++i)
    {
        random = random * 1664525U + 1013904223U;
        const auto sample = static_cast<double> (random) / 4294967295.0 * 8.0 - 4.0;
        for (int channel = 0; channel < channels; ++channel)
            buffer.setSample (channel, i, channel == 0 ? sample : -sample * 0.7);
    }
    return buffer;
}
}

TEST_CASE ("Offline oversampling FIR round trip has measured linear phase and flat passband", "[magnetic-os][filters]")
{
    for (int factor : MagneticOversampling::factors)
    {
        CAPTURE (factor);
        const auto filter = MagneticOversampling::measureFilter (factor);
        if (factor == 1)
            CHECK (filter.latencySamples == Catch::Approx (0.0).margin (1.0e-12));
        else
            CHECK (filter.latencySamples > 0.0);
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double frequency : { 20.0, 1000.0, 10000.0, 20000.0 })
            {
                CAPTURE (rate, frequency);
                const auto response = MagneticOversampling::responseAt (filter, rate, frequency);
                CHECK (response.gainDb == Catch::Approx (0.0).margin (0.05));
                CHECK (response.delayRemovedPhaseDegrees == Catch::Approx (0.0).margin (0.001));
            }
    }
}

TEST_CASE ("Offline oversampling preserves state across partitions reset and independent channels", "[magnetic-os][state]")
{
    juce::ScopedNoDenormals noDenormals;
    constexpr int count = 8192;
    for (int factor : MagneticOversampling::factors)
    {
        CAPTURE (factor);
        auto regular = input (2, count), partitioned = regular;
        MagneticOversampling::Processor a (factor, 2, 4096), b (factor, 2, 4096);
        CHECK (a.factor() == static_cast<std::size_t> (factor));
        for (int start = 0; start < count; start += 4096)
        {
            std::array<double*, 2> pointers { regular.getWritePointer (0, start), regular.getWritePointer (1, start) };
            juce::AudioBuffer<double> block (pointers.data(), 2, 4096);
            a.process (block);
        }
        int chunk = 0;
        constexpr std::array sizes { 1, 13, 127, 256, 4096 };
        for (int start = 0; start < count;)
        {
            const auto size = juce::jmin (sizes[static_cast<std::size_t> (chunk++) % sizes.size()], count - start);
            std::array<double*, 2> pointers { partitioned.getWritePointer (0, start), partitioned.getWritePointer (1, start) };
            juce::AudioBuffer<double> block (pointers.data(), 2, size);
            b.process (block);
            start += size;
        }
        double error = 0.0;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
                error = std::max (error, std::abs (regular.getSample (channel, i) - partitioned.getSample (channel, i)));
        CHECK (error < 1.0e-12);
        a.reset();
        b.reset();
        auto block = input (2, 256), reference = block;
        block.clear (1, 0, 256);
        reference.clear (1, 0, 256);
        a.process (block);
        MagneticOversampling::Processor fresh (factor, 1, 256);
        juce::AudioBuffer<double> mono (1, 256);
        mono.copyFrom (0, 0, reference, 0, 0, 256);
        fresh.process (mono);
        double channelError = 0.0;
        for (int i = 0; i < 256; ++i)
        {
            channelError = std::max (channelError, std::abs (block.getSample (0, i) - mono.getSample (0, i)));
            REQUIRE (block.getSample (1, i) == Catch::Approx (0.0).margin (1.0e-15));
        }
        CHECK (channelError < 1.0e-12);
        if (factor == 1)
        {
            MagneticPrototype::PlayPreisachCore direct;
            for (int i = 0; i < 256; ++i)
                REQUIRE (block.getSample (0, i) == Catch::Approx (direct.processField (reference.getSample (0, i))).margin (1.0e-15));
        }
    }
}

TEST_CASE ("Offline oversampling stays finite for silence quiet loud DC and noise at all rates", "[magnetic-os][stability]")
{
    juce::ScopedNoDenormals noDenormals;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int factor : MagneticOversampling::factors)
        {
            CAPTURE (rate, factor);
            MagneticOversampling::Processor processor (factor, 2, 4096);
            auto valid = true;
            for (int size : { 1, 64, 4096 })
            {
                juce::AudioBuffer<double> block (2, size);
                for (int signal = 0; signal < 5; ++signal)
                {
                    for (int i = 0; i < size; ++i)
                    {
                        const auto sine = std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / rate);
                        const auto sample = signal == 0 ? 0.0 : signal == 1 ? sine * 1.0e-12
                            : signal == 2 ? sine * 15.8489319246 : signal == 3 ? 15.8489319246 : (i % 2 == 0 ? 4.0 : -4.0);
                        block.setSample (0, i, sample);
                        block.setSample (1, i, -sample);
                    }
                    processor.process (block);
                    for (int channel = 0; channel < 2; ++channel)
                        for (int i = 0; i < size; ++i)
                            valid &= finiteNumber (block.getSample (channel, i)) && std::abs (block.getSample (channel, i)) < 4.0;
                }
            }
            CHECK (valid);
        }
}

TEST_CASE ("Offline oversampling comparison reduces measured aliases and checks reference convergence", "[magnetic-os][spectrum]")
{
    const auto result = MagneticOversampling::compare (48000.0, 7000.0, 1.0);
    CHECK (result.referenceConvergenceDbr < -90.0);
    CHECK (result.comparisons[0].aliasDbr > -60.0);
    CHECK (result.comparisons.back().aliasDbr < result.comparisons[0].aliasDbr - 30.0);
    for (const auto& row : result.comparisons)
    {
        CHECK (finiteNumber (row.aliasDbr));
        CHECK (finiteNumber (row.alignedResidualDbr));
        CHECK (finiteNumber (row.passbandResidualDbr));
        CHECK (row.audioBandAliasDbr <= row.aliasDbr + 1.0e-10);
        CHECK (std::abs (row.gainErrorDb) < 0.1);
        CHECK (std::abs (row.phaseErrorDegrees) < 1.0);
    }
}

TEST_CASE ("Oversampling separates ultrasonic filter transition aliases and verifies difficult references", "[magnetic-os][spectrum]")
{
    const auto ultrasonic = MagneticOversampling::compare (96000.0, 7000.0, 4.0);
    CHECK (ultrasonic.comparisons.back().peakAliasHz > 40000.0);
    CHECK (ultrasonic.comparisons.back().aliasDbr > -60.0);
    CHECK (ultrasonic.comparisons.back().audioBandAliasDbr < -100.0);
    const auto quiet = MagneticOversampling::compare (44100.0, 10000.0, 0.05);
    CHECK (quiet.referenceInitialDbr > -100.0);
    CHECK (quiet.referenceFactor == 128);
    CHECK (quiet.referenceConvergenceDbr < -100.0);
}

TEST_CASE ("Export offline filtered oversampling comparison", "[magnetic-os-report]")
{
    const auto* path = std::getenv ("LUMIERE_MAGNETIC_OS_REPORT_DIR");
    if (path == nullptr)
    {
        SUCCEED ("Set LUMIERE_MAGNETIC_OS_REPORT_DIR for the full filtered comparison; no Benchmarks target.");
        return;
    }
    const auto directory = juce::File (juce::String::fromUTF8 (path));
    REQUIRE (directory.createDirectory().wasOk());
    std::ofstream results (directory.getChildFile ("comparison.csv").getFullPathName().toStdString());
    results << "# Offline normalized H/M; linear-phase FIR, -120 dB stopband target; 32x/64x references, adaptive 128x if disagreement exceeds -100 dBr.\n"
        "sample_rate_hz,requested_frequency_hz,frequency_hz,field_peak,factor,alias_dbr,audio_band_alias_dbr,peak_alias_hz,aligned_residual_dbr,passband_residual_dbr,gain_error_db,phase_error_deg,latency_samples,reference_32v64_dbr,reference_factor,reference_last_dbr\n" << std::setprecision (12);
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double frequency : { 50.0, 1000.0, 7000.0, 10000.0 })
            for (double peak : { 0.05, 1.0, 4.0 })
            {
                const auto result = MagneticOversampling::compare (rate, frequency, peak);
                REQUIRE (finiteNumber (result.referenceConvergenceDbr));
                for (const auto& row : result.comparisons)
                {
                    for (double value : { row.aliasDbr, row.audioBandAliasDbr, row.peakAliasHz, row.alignedResidualDbr, row.passbandResidualDbr, row.gainErrorDb, row.phaseErrorDegrees, row.latencySamples })
                        REQUIRE (finiteNumber (value));
                    results << rate << ',' << frequency << ',' << result.frequency << ',' << peak << ',' << row.factor << ','
                        << row.aliasDbr << ',' << row.audioBandAliasDbr << ',' << row.peakAliasHz << ',' << row.alignedResidualDbr << ',' << row.passbandResidualDbr << ',' << row.gainErrorDb << ','
                        << row.phaseErrorDegrees << ',' << row.latencySamples << ',' << result.referenceInitialDbr << ',' << result.referenceFactor << ',' << result.referenceConvergenceDbr << '\n';
                }
                results.flush();
                std::cout << "Oversampling report: " << rate << " Hz, tone " << frequency << " Hz, field " << peak << " complete\n" << std::flush;
            }
    REQUIRE (results.good());
    std::ofstream responses (directory.getChildFile ("filters.csv").getFullPathName().toStdString());
    responses << "# Linear identity round trip, no core; phase removes reported FIR latency.\n"
        "sample_rate_hz,factor,frequency_hz,gain_db,delay_removed_phase_deg,latency_samples,latency_ms\n" << std::setprecision (12);
    std::ofstream costs (directory.getChildFile ("cost.csv").getFullPathName().toStdString());
    costs << "# " << juce::SystemStats::getCpuModel().toStdString() << "; " << (JUCE_DEBUG ? "Debug" : "Non-Debug")
        << "; median 5 x 32768 mono base samples; includes filters/core/copy/checksum, excludes prepare/FFT.\n"
        "factor,ns_per_base_mono_sample,latency_samples,checksum\n" << std::setprecision (12);
    for (int factor : MagneticOversampling::factors)
    {
        const auto filter = MagneticOversampling::measureFilter (factor);
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double frequency : { 20.0, 50.0, 1000.0, 7000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
            {
                const auto response = MagneticOversampling::responseAt (filter, rate, frequency);
                REQUIRE (finiteNumber (response.gainDb));
                REQUIRE (finiteNumber (response.delayRemovedPhaseDegrees));
                responses << rate << ',' << factor << ',' << frequency << ',' << response.gainDb << ','
                    << response.delayRemovedPhaseDegrees << ',' << filter.latencySamples << ',' << filter.latencySamples * 1000.0 / rate << '\n';
            }
        const auto cost = MagneticOversampling::timing (factor);
        REQUIRE (cost[0] > 0.0);
        REQUIRE (finiteNumber (cost[1]));
        costs << factor << ',' << cost[0] << ',' << filter.latencySamples << ',' << cost[1] << '\n';
    }
    REQUIRE (responses.good());
    REQUIRE (costs.good());
}
