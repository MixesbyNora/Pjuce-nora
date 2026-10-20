#include "helpers/MagneticValidation.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>

namespace
{
using Evaluation = MagneticOversampling::Evaluation;
struct Method { const char* name; int factor; Evaluation evaluation; };
constexpr std::array methods {
    Method { "point16", 16, Evaluation::point },
    Method { "quadratic8", 8, Evaluation::turningPoint },
    Method { "quadratic16", 16, Evaluation::turningPoint },
    Method { "quartic8", 8, Evaluation::quartic },
    Method { "quartic16", 16, Evaluation::quartic },
    Method { "quartic32", 32, Evaluation::quartic }
};
bool finite (double x) { return (std::bit_cast<std::uint64_t> (x) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL; }
bool passes (const MagneticValidation::Metrics& m, const MagneticValidation::Result& r)
{
    return r.converged && r.referenceIdentityDb <= -95.0 && m.identityResidualDb <= -95.0
        && m.bandResidualDb <= -80.0 && std::abs (m.bandPeakErrorDb) <= 0.1
        && (! r.stimulus.periodic() || m.offGridDb <= -80.0)
        && (! r.stimulus.silentTail() || (m.tailErrorDb <= -80.0 && m.tailDriftDb <= -100.0));
}
}

TEST_CASE ("Quartic history reconstructs smooth extrema without reset sentinel extrema", "[magnetic-refinement]")
{
    MagneticOversampling::QuarticCore candidate;
    MagneticPrototype::PlayPreisachCore reference;
    // A quartic field with a non-grid maximum, then a monotone return. The
    // fine reference also visits the exact maximum; compare at delayed endpoints.
    const auto field = [] (double n) { const auto x = (n - 24.3) / 30.0; return 0.7 - x * x - 0.1 * x * x * x * x; };
    for (int n = 0; n < 50; ++n)
    {
        const auto actual = candidate.processField (field (n));
        if (n < 2)
            CHECK (actual == 0.0);
        else
        {
            if (n - 2 == 25)
                reference.processField (field (24.3));
            const auto expected = reference.processField (field (n - 2));
            CHECK (actual == Catch::Approx (expected).epsilon (0.0).margin (1.0e-11));
        }
    }
    candidate.reset();
    reference.reset();
    for (double value : { 0.8, 0.2, 0.1, 0.05 })
        candidate.processField (value);
    reference.processField (0.8);
    const auto expected = reference.processField (0.2);
    CHECK (candidate.getMagnetization() == Catch::Approx (expected).epsilon (0.0).margin (1.0e-15));
    const auto held = candidate.getMagnetization();
    CHECK (candidate.processField (std::numeric_limits<double>::quiet_NaN()) == Catch::Approx (held).epsilon (0.0).margin (1.0e-15));
    CHECK (candidate.processField (std::numeric_limits<double>::infinity()) == Catch::Approx (held).epsilon (0.0).margin (1.0e-15));
}

TEST_CASE ("Quartic candidate is symmetric finite and independent of block boundaries", "[magnetic-refinement]")
{
    MagneticOversampling::QuarticCore positive, negative;
    bool valid = true;
    for (int i = 0; i < 8192; ++i)
    {
        const auto input = 64.0 * std::sin (i * 0.137);
        const auto a = positive.processField (input), b = negative.processField (-input);
        valid &= finite (a) && std::abs (a) <= 1.0 && std::abs (a + b) < 1.0e-12;
    }
    CHECK (valid);
    for (int factor : { 8, 16, 32 })
    {
        MagneticOversampling::Processor whole (factor, 2, 256, {}, Evaluation::quartic);
        MagneticOversampling::Processor split (factor, 2, 256, {}, Evaluation::quartic);
        juce::AudioBuffer<double> block (2, 256), fragment (2, 1);
        auto maximumDifference = 0.0;
        for (int pass = 0; pass < 8; ++pass)
        {
            for (int i = 0; i < 256; ++i)
            {
                block.setSample (0, i, std::sin ((pass * 256 + i) * 0.431));
                block.setSample (1, i, -block.getSample (0, i));
            }
            whole.process (block);
            for (int i = 0; i < 256; ++i)
            {
                fragment.setSample (0, 0, std::sin ((pass * 256 + i) * 0.431));
                fragment.setSample (1, 0, -fragment.getSample (0, 0));
                split.process (fragment);
                maximumDifference = std::max (maximumDifference, std::abs (block.getSample (0, i) - fragment.getSample (0, 0)));
                valid &= finite (fragment.getSample (0, 0)) && std::abs (fragment.getSample (0, 0) + fragment.getSample (1, 0)) < 1.0e-12;
            }
        }
        CHECK (maximumDifference < 1.0e-12);
        CHECK (valid);
        whole.reset();
        block.clear();
        whole.process (block);
        CHECK (block.getMagnitude (0, 256) == 0.0);
    }
}

TEST_CASE ("Quartic lookahead is included in independently calibrated identity latency", "[magnetic-refinement]")
{
    using namespace MagneticValidation;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const Stimulus input (Signal::closeHigh, rate, 0.05);
        const auto reference = render (input, 64, true, true);
        for (int factor : { 8, 16, 32 })
        {
            const auto actual = renderCandidate (input, factor, Evaluation::quartic, true);
            CHECK (measure (actual, reference, input).bandResidualDb < -95.0);
        }
    }
}

TEST_CASE ("Refined extrema resolve close-tone and impulse state regressions", "[magnetic-refinement]")
{
    using namespace MagneticValidation;
    for (const auto& input : { Stimulus (Signal::closeHigh, 44100.0, 4.0), Stimulus (Signal::impulse, 48000.0, 1.0) })
    {
        const auto result = compare (input);
        REQUIRE (result.converged);
        const auto output = renderCandidate (input, 16, Evaluation::quartic);
        const auto error = measure (output, result.reference, input);
        CHECK (error.bandResidualDb < -85.0);
        CHECK (error.bandResidualDb < result.candidateMetrics.bandResidualDb - 15.0);
        if (input.periodic())
            CHECK (error.offGridDb < -85.0);
        else
            CHECK (measure (renderCandidate (input, 8, Evaluation::quartic), result.reference, input).bandResidualDb < -80.0);
    }
}

TEST_CASE ("Export focused magnetic artifact refinement comparisons", "[magnetic-refinement-report]")
{
    const auto* directory = std::getenv ("LUMIERE_MAGNETIC_REFINEMENT_REPORT_DIR");
    if (directory == nullptr)
    {
        SUCCEED ("Set LUMIERE_MAGNETIC_REFINEMENT_REPORT_DIR for offline research export");
        return;
    }
    using namespace MagneticValidation;
    const juce::File root (juce::String::fromUTF8 (directory));
    REQUIRE (root.createDirectory().wasOk());
    std::ofstream csv (root.getChildFile ("comparison.csv").getFullPathName().toStdString());
    REQUIRE (csv.good());
    csv << std::setprecision (17)
        << "signal,sample_rate_hz,field_peak,method,band_residual_dbr,off_grid_dbr,band_peak_error_db,band_max_error,full_residual_dbr,tail_error_dbr,tail_drift_dbr,late_mean_error_m,identity_residual_dbr,reference_identity_dbr,reference_factor,reference_difference_dbr,reference_band_max_error,reference_full_agreement,latency_samples,pass\n";
    const auto full = std::getenv ("LUMIERE_MAGNETIC_REFINEMENT_FULL_GRID") != nullptr;
    for (auto signal : signals)
    {
        if (! full && signal != Signal::closeHigh && signal != Signal::impulse)
            continue;
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (double peak : { 0.05, 1.0, 4.0 })
            {
                const Stimulus input (signal, rate, peak);
                const auto result = compare (input);
                REQUIRE (result.converged);
                const auto identity = render (input, result.referenceFactor, true, true);
                std::ofstream trace;
                const auto saveTrace = (signal == Signal::impulse && rate == 48000.0 && peak == 1.0)
                    || (signal == Signal::closeHigh && rate == 44100.0 && peak == 4.0);
                if (saveTrace)
                {
                    trace.open (root.getChildFile (juce::String (name (signal)) + ".csv").getFullPathName().toStdString());
                    trace << std::setprecision (17) << "sample,input,reference";
                    for (auto method : methods)
                        trace << ',' << method.name;
                    trace << '\n';
                }
                std::array<Trace, methods.size()> outputs;
                for (std::size_t index = 0; index < methods.size(); ++index)
                {
                    const auto method = methods[index];
                    auto output = renderCandidate (input, method.factor, method.evaluation);
                    auto m = measure (output, result.reference, input);
                    m.identityResidualDb = measure (renderCandidate (input, method.factor, method.evaluation, true), identity, input).bandResidualDb;
                    bool valid = true;
                    for (auto value : output.samples)
                        valid &= finite (value) && std::abs (value) < 2.0;
                    CHECK (valid);
                    const auto lateMean = (std::accumulate (output.samples.end() - 1024, output.samples.end(), 0.0)
                        - std::accumulate (result.reference.samples.end() - 1024, result.reference.samples.end(), 0.0)) / 1024.0;
                    csv << name (signal) << ',' << rate << ',' << peak << ',' << method.name << ',' << m.bandResidualDb << ',';
                    if (input.periodic()) csv << m.offGridDb;
                    csv << ',' << m.bandPeakErrorDb << ',' << m.bandMaximumError << ',' << m.fullResidualDb << ',';
                    if (input.silentTail()) csv << m.tailErrorDb;
                    csv << ',';
                    if (input.silentTail()) csv << m.tailDriftDb;
                    csv << ',' << lateMean << ',' << m.identityResidualDb << ',' << result.referenceIdentityDb << ','
                        << result.referenceFactor << ',' << result.referenceDifferenceDb << ',' << result.referenceBandMaximumError << ','
                        << result.referenceFullConverged << ',' << output.latencySamples << ',' << passes (m, result) << '\n';
                    if (saveTrace) outputs[index] = std::move (output);
                }
                if (saveTrace)
                {
                    for (int i = 0; i < windowSize; ++i)
                    {
                        trace << i << ',' << input.at (i) << ',' << result.reference.samples[static_cast<std::size_t> (i)];
                        for (const auto& output : outputs)
                            trace << ',' << output.samples[static_cast<std::size_t> (i)];
                        trace << '\n';
                    }
                    if (input.periodic())
                    {
                        std::ofstream spectral (root.getChildFile ("close_high_spectrum.csv").getFullPathName().toStdString());
                        spectral << std::setprecision (17) << "frequency_hz,reference_dbr";
                        for (auto method : methods)
                            spectral << ',' << method.name << "_error_dbr";
                        spectral << '\n';
                        const auto referenceBins = spectrum (result.reference.samples);
                        const auto power = std::inner_product (result.reference.samples.begin(), result.reference.samples.end(), result.reference.samples.begin(), 0.0) / windowSize;
                        const auto lineDb = [=] (std::complex<double> value)
                        {
                            return 10.0 * std::log10 (std::max (2.0 * std::norm (value) / power, 1.0e-30));
                        };
                        std::array<std::vector<std::complex<double>>, methods.size()> errors;
                        for (std::size_t index = 0; index < methods.size(); ++index)
                        {
                            auto difference = outputs[index].samples;
                            for (std::size_t n = 0; n < difference.size(); ++n)
                                difference[n] -= result.reference.samples[n];
                            errors[index] = spectrum (difference);
                        }
                        for (int bin = 1; bin < windowSize / 2; ++bin)
                        {
                            const auto index = static_cast<std::size_t> (bin);
                            spectral << bin * rate / windowSize << ',' << lineDb (referenceBins[index]);
                            for (const auto& error : errors)
                                spectral << ',' << lineDb (error[index]);
                            spectral << '\n';
                        }
                        REQUIRE (spectral.good());
                    }
                    REQUIRE (trace.good());
                }
                csv.flush();
            }
    }
    REQUIRE (csv.good());
    std::ofstream profile (root.getChildFile ("timing.csv").getFullPathName().toStdString());
    profile << std::setprecision (17) << "method,stimulus,median_ns_per_base_sample,checksum\n";
    for (auto method : methods)
        for (auto stimulus : { MagneticOversampling::TimingInput::slowSine, MagneticOversampling::TimingInput::quietHighSine, MagneticOversampling::TimingInput::noise })
        {
            const auto measurement = MagneticOversampling::timing (method.factor, method.evaluation, stimulus);
            profile << method.name << ',' << static_cast<int> (stimulus) << ',' << measurement[0] << ',' << measurement[1] << '\n';
            CHECK (finite (measurement[0]));
            CHECK (finite (measurement[1]));
        }
    REQUIRE (profile.good());
}
