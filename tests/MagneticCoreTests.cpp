#include <experimental/PlayPreisachCore.h>
#include "helpers/MagneticAnalysis.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <type_traits>

namespace
{
using Core = MagneticPrototype::PlayPreisachCore;
bool finiteNumber (double value)
{
    return (std::bit_cast<std::uint64_t> (value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
double moveTo (Core& core, double from, double to, int divisions = 128)
{
    double output = 0.0;
    for (int i = 1; i <= divisions; ++i)
        output = core.processField (from + (to - from) * i / divisions);
    return output;
}
}

TEST_CASE ("Magnetic core has normalized positive weights and fixed nonallocating storage", "[magnetic]")
{
    static_assert (std::is_trivially_copyable_v<Core>);
    static_assert (noexcept (std::declval<Core&>().processField (0.0)));
    static_assert (Core::maximumPlayUpdates == 16);
    static_assert (Core::maximumTanhCalls == 17);
    static_assert (sizeof (Core) <= 512);
    Core core;
    CHECK (std::accumulate (core.getWeights().begin(), core.getWeights().end(), 0.0) == Catch::Approx (1.0).margin (1.0e-14));
    for (std::size_t i = 0; i < core.getWeights().size(); ++i)
    {
        CHECK (core.getWeights()[i] > 0.0);
        CHECK (core.getRadii()[i] > 0.0);
    }
    Core memoryless ({ 0.0, 1.0 });
    for (double field : { -16.0, -1.0, -0.001, 0.0, 0.001, 1.0, 16.0 })
        CHECK (memoryless.processField (field) == Catch::Approx (std::tanh (field)).margin (1.0e-15));
}

TEST_CASE ("Magnetic core retains history remanence and deterministic demagnetized reset", "[magnetic][history]")
{
    Core positive, negative, fresh;
    positive.processField (3.0);
    negative.processField (-3.0);
    const auto remanentPositive = moveTo (positive, 3.0, 0.0);
    const auto remanentNegative = moveTo (negative, -3.0, 0.0);
    CHECK (remanentPositive > 0.01);
    CHECK (remanentNegative < -0.01);
    CHECK (remanentPositive == Catch::Approx (-remanentNegative).margin (1.0e-14));
    CHECK (fresh.processField (0.0) == Catch::Approx (0.0).margin (1.0e-15));
    for (int i = 0; i < 192000; ++i)
        positive.processField (0.0);
    CHECK (positive.getMagnetization() == Catch::Approx (remanentPositive).margin (1.0e-15));
    positive.reset();
    CHECK (positive.processField (0.0) == Catch::Approx (0.0).margin (1.0e-15));
    for (double field : { 0.2, -0.1, 1.0, -2.0, 0.3 })
        CHECK (positive.processField (field) == Catch::Approx (fresh.processField (field)).margin (1.0e-15));
}

TEST_CASE ("Magnetic minor loops close return points and larger excursions wipe nested history", "[magnetic][history]")
{
    Core core;
    core.processField (-3.0);
    moveTo (core, -3.0, 2.0);
    const auto outerState = core.getPlayStates();
    const auto outerM = core.getMagnetization();
    moveTo (core, 2.0, 0.2);
    moveTo (core, 0.2, 1.2);
    const auto innerState = core.getPlayStates();
    const auto innerM = core.getMagnetization();
    moveTo (core, 1.2, 0.6);
    CHECK (moveTo (core, 0.6, 1.2) == Catch::Approx (innerM).margin (1.0e-12));
    for (std::size_t i = 0; i < innerState.size(); ++i)
        CHECK (core.getPlayStates()[i] == Catch::Approx (innerState[i]).margin (1.0e-12));
    CHECK (moveTo (core, 1.2, 2.0) == Catch::Approx (outerM).margin (1.0e-12));
    for (std::size_t i = 0; i < outerState.size(); ++i)
        CHECK (core.getPlayStates()[i] == Catch::Approx (outerState[i]).margin (1.0e-12));
    Core different;
    different.processField (3.0);
    different.processField (-1.0);
    moveTo (core, 2.0, -3.0);
    moveTo (different, -1.0, -3.0);
    for (double field : { -2.0, 0.0, 1.0, 3.0 })
        CHECK (core.processField (field) == Catch::Approx (different.processField (field)).margin (1.0e-12));
}

TEST_CASE ("Magnetic branches are continuous monotone odd under mirrored histories and dissipative", "[magnetic]")
{
    Core core, mirror;
    core.processField (-3.0);
    mirror.processField (3.0);
    double lastField = -3.0, lastM = core.getMagnetization(), work = 0.0, maximumStep = 0.0;
    for (int direction : { 1, -1 })
        for (int i = 1; i <= 6000; ++i)
        {
            const auto field = direction == 1 ? -3.0 + i * 0.001 : 3.0 - i * 0.001;
            const auto m = core.processField (field);
            const auto other = mirror.processField (-field);
            REQUIRE (m == Catch::Approx (-other).margin (1.0e-12));
            REQUIRE (direction * (m - lastM) >= -1.0e-14);
            maximumStep = std::max (maximumStep, std::abs (m - lastM));
            work += 0.5 * (field + lastField) * (m - lastM);
            lastM = m;
            lastField = field;
        }
    CHECK (maximumStep <= 0.0010000001);
    CHECK (work > 0.01);
}

TEST_CASE ("Magnetic field path is rate independent at common times across four sample rates", "[magnetic][rates]")
{
    std::array<double, 101> reference {};
    for (int rate : { 44100, 48000, 96000, 192000 })
    {
        CAPTURE (rate);
        Core core;
        for (int i = 0; i <= rate; ++i)
        {
            // One-Hz triangle includes exact turning points at every tested rate.
            const auto phase = static_cast<double> (i) / rate;
            const auto field = phase <= 0.5 ? -3.0 + 12.0 * phase : 9.0 - 12.0 * phase;
            const auto m = core.processField (field);
            if (i % (rate / 100) == 0)
            {
                const auto point = static_cast<std::size_t> (i / (rate / 100));
                if (rate == 44100)
                    reference[point] = m;
                else
                    CHECK (m == Catch::Approx (reference[point]).margin (1.0e-12));
            }
        }
    }
}

TEST_CASE ("Magnetic core bounds extremes rejects invalid samples and keeps channels independent", "[magnetic][stability]")
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    Core invalid ({ nan, inf });
    CHECK (invalid.getParameters().memory == Catch::Approx (MagneticPrototype::Settings::defaultMemory));
    CHECK (invalid.getParameters().width == Catch::Approx (MagneticPrototype::Settings::defaultWidth));
    for (double memory : { -100.0, 0.0, 0.35, 1.0, 100.0 })
        for (double width : { -100.0, 0.05, 1.0, 4.0, 100.0 })
        {
            Core core ({ memory, width }), silent;
            for (double field : { 0.0, 1.0e-300, -1.0e-300, 15.8489319246, -15.8489319246, 1.0e300, -1.0e300, 0.0, nan, inf, -inf })
            {
                const auto before = core.getMagnetization();
                const auto output = core.processField (field);
                REQUIRE (finiteNumber (output));
                REQUIRE (output >= -1.0);
                REQUIRE (output <= 1.0);
                if (! finiteNumber (field))
                    CHECK (output == Catch::Approx (before).margin (1.0e-15));
                CHECK (silent.processField (0.0) == Catch::Approx (0.0).margin (1.0e-15));
            }
            // Deterministic noise and DC holds, with arbitrary block partitions.
            std::uint32_t random = 12345;
            auto valid = true;
            for (int size : { 1, 64, 4096 })
                for (int sample = 0; sample < size; ++sample)
                {
                    random = random * 1664525U + 1013904223U;
                    const auto field = static_cast<double> (random) / 4294967295.0 * 32.0 - 16.0;
                    const auto m = core.processField (field);
                    valid &= finiteNumber (m) && std::abs (m) <= 1.0;
                    for (std::size_t i = 0; i < core.getPlayStates().size(); ++i)
                        valid &= std::abs (field - core.getPlayStates()[i]) <= core.getRadii()[i] + 1.0e-12;
                }
            CHECK (valid);
        }
}

TEST_CASE ("Offline spectral check detects folded harmonics without claiming antialiasing", "[magnetic][aliasing]")
{
    const auto quiet = MagneticAnalysis::sampling (48000.0, 7000.0, 0.001, { 0.0, 1.0 });
    const auto driven = MagneticAnalysis::sampling (48000.0, 7000.0, 4.0, { 0.0, 1.0 });
    CHECK (quiet.nonHarmonicAliasDbr < -100.0);
    CHECK (driven.nonHarmonicAliasDbr > -60.0);
    CHECK (driven.inBandResidualDbr > quiet.inBandResidualDbr + 20.0);
    CHECK (driven.referenceConvergenceDbr < driven.inBandResidualDbr - 20.0);
    CHECK (finiteNumber (driven.referenceRms));
}

TEST_CASE ("Export offline magnetic prototype loops sampling and cost reports", "[magnetic-report]")
{
    const auto* path = std::getenv ("LUMIERE_MAGNETIC_REPORT_DIR");
    if (path == nullptr)
    {
        SUCCEED ("Set LUMIERE_MAGNETIC_REPORT_DIR to export the offline experiment; no Benchmarks target is used.");
        return;
    }
    const auto directory = juce::File (juce::String::fromUTF8 (path));
    REQUIRE (directory.createDirectory().wasOk());
    std::ofstream loops (directory.getChildFile ("loops.csv").getFullPathName().toStdString());
    loops << "# Modeled normalized H/M; not hardware B-H data or plugin audio.\n"
        "case,point,field,magnetization,memory,width\n" << std::setprecision (12);
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        const auto memory = scenario == 0 ? 0.0 : MagneticPrototype::Settings::defaultMemory;
        Core core ({ memory, 1.0 });
        core.processField (-3.0);
        const std::vector<double> knots = scenario == 2 ? std::vector<double> { -3.0, 3.0, -1.0, 1.0, -0.5, 0.5, 0.0, 0.5, 1.0, 2.0, 3.0, -3.0 }
                                                      : std::vector<double> { -3.0, 3.0, -3.0 };
        const auto name = scenario == 0 ? "memoryless" : scenario == 1 ? "major" : "nested";
        int point = 0;
        for (std::size_t segment = 1; segment < knots.size(); ++segment)
            for (int i = 0; i <= 256; ++i)
            {
                const auto field = knots[segment - 1] + (knots[segment] - knots[segment - 1]) * i / 256.0;
                loops << name << ',' << point++ << ',' << field << ',' << core.processField (field) << ',' << memory << ",1\n";
            }
    }
    REQUIRE (loops.good());
    std::ofstream sampling (directory.getChildFile ("sampling.csv").getFullPathName().toStdString());
    sampling << "# Magnetization proxy, not transformer audio. 8x offline reference; 4x/8x convergence check.\n"
        "sample_rate_hz,requested_frequency_hz,frequency_hz,field_peak,reference_rms,nonharmonic_alias_dbr,inband_residual_dbr,reference_4v8_dbr\n" << std::setprecision (12);
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double frequency : { 50.0, 1000.0, 7000.0, 10000.0 })
            for (double peak : { 0.05, 1.0, 4.0 })
            {
                const auto result = MagneticAnalysis::sampling (rate, frequency, peak);
                for (double value : { result.referenceRms, result.nonHarmonicAliasDbr, result.inBandResidualDbr, result.referenceConvergenceDbr })
                    REQUIRE (finiteNumber (value));
                sampling << rate << ',' << frequency << ',' << result.frequency << ',' << peak << ',' << result.referenceRms << ','
                    << result.nonHarmonicAliasDbr << ',' << result.inBandResidualDbr << ',' << result.referenceConvergenceDbr << '\n';
            }
    REQUIRE (sampling.good());
    const auto cost = MagneticAnalysis::timing();
    REQUIRE (cost[0] > 0.0);
    REQUIRE (finiteNumber (cost[1]));
    std::ofstream timing (directory.getChildFile ("cost.csv").getFullPathName().toStdString());
    timing << "# Local Debug timing; median of 5 x 32768 mono samples; includes loop/checksum; not a realtime deadline guarantee.\n"
        << "# " << juce::SystemStats::getCpuModel().toStdString() << '\n'
        << "ns_per_mono_sample,core_bytes,max_play_updates,max_tanh_calls,checksum\n" << std::setprecision (12)
        << cost[0] << ',' << cost[2] << ',' << Core::maximumPlayUpdates << ',' << Core::maximumTanhCalls << ',' << cost[1] << '\n';
    REQUIRE (timing.good());
}
