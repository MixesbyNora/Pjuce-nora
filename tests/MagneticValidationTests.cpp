#include "helpers/MagneticValidation.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <bit>
#include <fstream>
#include <iomanip>
#include <optional>

namespace
{
bool finite (double value)
{
    return (std::bit_cast<std::uint64_t> (value) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
void checkTrace (const MagneticValidation::Trace& trace)
{
    REQUIRE (trace.samples.size() == MagneticValidation::windowSize);
    bool valid = true;
    for (auto value : trace.samples)
        valid &= finite (value) && std::abs (value) < 2.0;
    CHECK (valid);
}
double lineDb (std::complex<double> value, double referenceRms)
{
    return 20.0 * std::log10 (std::max (std::sqrt (2.0) * std::abs (value) / referenceRms, 1.0e-15));
}
}

TEST_CASE ("Validation alignment preserves the audio band at every fractional delay", "[magnetic-validation][calibration]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (double fraction : { 0.0, 0.125, 0.25, 0.5, 0.75, 0.99 })
            for (double frequency : { 0.0, 20.0, 1000.0, 10000.0, 18000.0, 20000.0 })
            {
                const auto error = MagneticValidation::alignmentErrorDb (fraction, frequency / rate);
                CAPTURE (rate, fraction, frequency, error);
                CHECK (error < -100.0);
            }
}

TEST_CASE ("Validation stimuli are bounded coherent and have declared silent tails", "[magnetic-validation][stimuli]")
{
    using namespace MagneticValidation;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto signal : signals)
        {
            const Stimulus input (signal, rate, 4.0);
            bool valid = true;
            for (int i = 0; i < windowSize; ++i)
                valid &= finite (input.at (i)) && std::abs (input.at (i)) <= 4.0001;
            CHECK (valid);
            for (int i = 0; i < input.toneCount; ++i)
                CHECK (input.bins[static_cast<std::size_t> (i)] % 3 == 0);
            if (input.silentTail())
                CHECK (input.at (windowSize - 1024) == 0.0);
        }
}

TEST_CASE ("Analytic reference decimation and candidate identity calibrate independently", "[magnetic-validation][calibration]")
{
    using namespace MagneticValidation;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const Stimulus input (Signal::closeHigh, rate, 0.05);
        const auto reference = render (input, 64, true, true);
        checkTrace (reference);
        Trace direct;
        for (int i = 0; i < windowSize; ++i)
            direct.samples.push_back (input.at (i));
        const auto calibration = measure (reference, direct, input);
        CAPTURE (rate, reference.latencySamples, calibration.bandResidualDb);
        CHECK (calibration.bandResidualDb < -95.0);
        for (int factor : { 8, 16 })
        {
            const auto actual = render (input, factor, false, true);
            const auto error = measure (actual, reference, input);
            CAPTURE (factor, actual.latencySamples, error.bandResidualDb);
            CHECK (error.bandResidualDb < -95.0);
        }
    }
}

TEST_CASE ("Multitone metrics keep genuine intermodulation separate from off-grid artifacts", "[magnetic-validation][metrics]")
{
    using namespace MagneticValidation;
    const Stimulus input (Signal::lowHigh, 48000.0, 1.0);
    Trace reference;
    for (int i = 0; i < windowSize; ++i)
        reference.samples.push_back (input.at (i));
    auto legitimate = reference, spurious = reference;
    const auto product = std::abs (2 * input.bins[0] - input.bins[1]);
    for (int i = 0; i < windowSize; ++i)
    {
        legitimate.samples[static_cast<std::size_t> (i)] += 0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * product * i / windowSize);
        spurious.samples[static_cast<std::size_t> (i)] += 0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * 11.0 * i / windowSize);
    }
    const auto valid = measure (legitimate, reference, input);
    const auto invalid = measure (spurious, reference, input);
    CHECK (valid.offGridDb < -100.0);
    CHECK (valid.bandResidualDb > -60.0);
    CHECK (invalid.offGridDb > -60.0);
}

TEST_CASE ("Transient residuals expose persistent state error and late oscillation", "[magnetic-validation][metrics]")
{
    using namespace MagneticValidation;
    const Stimulus input (Signal::gaussian, 48000.0, 1.0);
    const auto result = compare (input);
    CHECK (result.converged);
    checkTrace (result.reference);
    checkTrace (result.candidate);
    checkTrace (result.baseline);
    // A real magnetic remanence is valid; the error is disagreement with it.
    CHECK (std::abs (result.reference.samples.back()) > 0.01);
    auto offset = result.reference, oscillating = result.reference;
    for (int i = windowSize - 1024; i < windowSize; ++i)
    {
        offset.samples[static_cast<std::size_t> (i)] += 0.001;
        oscillating.samples[static_cast<std::size_t> (i)] += i % 2 == 0 ? 0.001 : -0.001;
    }
    const auto stateError = measure (offset, result.reference, input);
    const auto oscillation = measure (oscillating, result.reference, input);
    CHECK (stateError.tailErrorDb > -80.0);
    CHECK (stateError.bandResidualDb > -80.0);
    CHECK (oscillation.tailDriftDb > -80.0);
    CHECK (result.candidateMetrics.tailDriftDb < -100.0);
}

TEST_CASE ("Audio-band peak metrics separate ultrasonic reference differences", "[magnetic-validation][calibration]")
{
    using namespace MagneticValidation;
    const Stimulus input (Signal::lowHigh, 48000.0, 1.0);
    Trace reference;
    reference.samples.resize (windowSize);
    for (int i = 0; i < windowSize; ++i)
        reference.samples[static_cast<std::size_t> (i)] = std::sin (2.0 * juce::MathConstants<double>::pi * 300.0 * i / windowSize);
    auto changed = reference;
    for (int i = 0; i < windowSize; ++i)
        changed.samples[static_cast<std::size_t> (i)] += 0.01 * std::sin (2.0 * juce::MathConstants<double>::pi * 7500.0 * i / windowSize);
    const auto metrics = measure (changed, reference, input);
    CHECK (metrics.maximumError > 0.009);
    CHECK (metrics.bandMaximumError < 1.0e-7);
    CHECK (std::abs (metrics.bandPeakErrorDb) < 1.0e-5);
}

TEST_CASE ("Export offline magnetic multitone and transient validation", "[magnetic-validation-report]")
{
    using namespace MagneticValidation;
    const auto* path = std::getenv ("LUMIERE_MAGNETIC_VALIDATION_REPORT_DIR");
    if (path == nullptr)
    {
        SUCCEED ("Set LUMIERE_MAGNETIC_VALIDATION_REPORT_DIR for the full offline grid; no Benchmarks target.");
        return;
    }
    const auto directory = juce::File (juce::String::fromUTF8 (path));
    REQUIRE (directory.createDirectory().wasOk());
    auto file = [&] (const char* name)
    {
        std::ofstream stream (directory.getChildFile (name).getFullPathName().toStdString());
        stream << std::setprecision (12);
        return stream;
    };
    auto output = file ("comparison.csv"), inputs = file ("stimuli.csv"), convergence = file ("convergence.csv"), products = file ("intermodulation.csv");
    output << "# Offline normalized H/M; 8x quadratic turning-point vs original 16x; analytic high-rate input, original core, FIR decimation reference. Blank metrics do not apply.\n"
        "signal,sample_rate_hz,nominal_field_peak,method,band_residual_dbr,full_residual_dbr,off_grid_dbr,band_peak_error_db,band_max_error_over_reference_peak,tail_error_dbr,tail_drift_dbr,identity_residual_dbr,reference_identity_dbr,reference_factor,reference_difference_dbr,reference_band_max_error,converged,latency_samples,full_peak_error_db,full_max_error_over_reference_peak,reference_full_max_error,reference_full_agreement\n";
    inputs << "signal,sample_rate_hz,nominal_field_peak,actual_base_peak,periodic_multitone,silent_tail,f1_hz,f2_hz,f3_hz,f4_hz,f5_hz,f6_hz\n";
    convergence << "signal,sample_rate_hz,nominal_field_peak,reference_factor,band_difference_dbr,full_max_error_over_reference_peak,tail_error_dbr,band_max_error_over_reference_peak\n";
    products << "signal,sample_rate_hz,nominal_field_peak,method,product,frequency_hz,reference_dbr,output_dbr,line_error_dbr\n";
    std::optional<Result> worstTransient, worstMultitone;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto signal : signals)
            for (double peak : { 0.05, 1.0, 4.0 })
            {
                const Stimulus input (signal, rate, peak);
                auto result = compare (input);
                checkTrace (result.reference);
                checkTrace (result.candidate);
                checkTrace (result.baseline);
                auto basePeak = 0.0;
                for (int i = 0; i < windowSize; ++i)
                    basePeak = std::max (basePeak, std::abs (input.at (i)));
                inputs << name (signal) << ',' << rate << ',' << peak << ',' << basePeak << ',' << input.periodic() << ',' << input.silentTail();
                for (int i = 0; i < 6; ++i)
                {
                    inputs << ',';
                    if (i < input.toneCount)
                        inputs << input.bins[static_cast<std::size_t> (i)] * rate / windowSize;
                }
                inputs << '\n';
                for (int i = 0; i < result.attempts; ++i)
                {
                    const auto& step = result.convergence[static_cast<std::size_t> (i)];
                    convergence << name (signal) << ',' << rate << ',' << peak << ',' << step[0] << ',' << step[1] << ',' << step[2] << ',';
                    if (input.silentTail()) convergence << step[3];
                    convergence << ',' << step[4] << '\n';
                }
                for (int method = 0; method < 2; ++method)
                {
                    const auto& metrics = method == 0 ? result.candidateMetrics : result.baselineMetrics;
                    const auto& trace = method == 0 ? result.candidate : result.baseline;
                    const auto* label = method == 0 ? "turning8" : "point16";
                    for (double value : { metrics.bandResidualDb, metrics.fullResidualDb, metrics.offGridDb, metrics.bandPeakErrorDb, metrics.bandMaximumError, metrics.peakErrorDb, metrics.maximumError, metrics.tailErrorDb, metrics.tailDriftDb, metrics.identityResidualDb, result.referenceIdentityDb, result.referenceDifferenceDb, result.referenceMaximumError, result.referenceBandMaximumError })
                        REQUIRE (finite (value));
                    output << name (signal) << ',' << rate << ',' << peak << ',' << label << ',' << metrics.bandResidualDb << ',' << metrics.fullResidualDb << ',';
                    if (input.periodic()) output << metrics.offGridDb;
                    output << ',' << metrics.bandPeakErrorDb << ',' << metrics.bandMaximumError << ',';
                    if (input.silentTail()) output << metrics.tailErrorDb;
                    output << ',';
                    if (input.silentTail()) output << metrics.tailDriftDb;
                    output << ',' << metrics.identityResidualDb << ',' << result.referenceIdentityDb << ',' << result.referenceFactor << ',' << result.referenceDifferenceDb << ',' << result.referenceBandMaximumError << ',' << result.converged << ',' << trace.latencySamples
                        << ',' << metrics.peakErrorDb << ',' << metrics.maximumError << ',' << result.referenceMaximumError << ',' << result.referenceFullConverged << '\n';
                    if (input.toneCount == 2)
                    {
                        const auto refBins = spectrum (result.reference.samples), outBins = spectrum (trace.samples);
                        auto rms = 0.0;
                        for (auto value : result.reference.samples) rms += value * value / windowSize;
                        rms = std::sqrt (rms);
                        const std::array bins { input.bins[1] - input.bins[0], std::abs (2 * input.bins[0] - input.bins[1]), 2 * input.bins[1] - input.bins[0], input.bins[0] + input.bins[1] };
                        constexpr std::array labels { "f2-f1", "abs(2f1-f2)", "2f2-f1", "f1+f2" };
                        for (std::size_t i = 0; i < bins.size(); ++i)
                        {
                            const auto frequency = bins[i] * rate / windowSize;
                            if (bins[i] <= 0 || bins[i] >= windowSize / 2 || frequency > 20000.0) continue;
                            const auto bin = static_cast<std::size_t> (bins[i]);
                            products << name (signal) << ',' << rate << ',' << peak << ',' << label << ',' << labels[i] << ',' << frequency << ','
                                << lineDb (refBins[bin], rms) << ',' << lineDb (outBins[bin], rms) << ',' << lineDb (outBins[bin] - refBins[bin], rms) << '\n';
                        }
                    }
                }
                if (result.converged)
                {
                    auto& worst = input.periodic() ? worstMultitone : worstTransient;
                    if (! worst || result.candidateMetrics.bandResidualDb > worst->candidateMetrics.bandResidualDb)
                        worst = result;
                }
                output.flush();
                std::cout << "Magnetic validation: " << rate << " Hz, " << name (signal) << ", field " << peak << ", reference " << result.referenceFactor << "x / " << result.referenceDifferenceDb << " dBr, converged=" << result.converged << '\n' << std::flush;
            }
    REQUIRE (output.good()); REQUIRE (inputs.good()); REQUIRE (convergence.good()); REQUIRE (products.good());
    REQUIRE (worstTransient.has_value()); REQUIRE (worstMultitone.has_value());
    auto trace = file ("worst_transient.csv");
    const auto& worst = *worstTransient;
    trace << "# " << name (worst.stimulus.signal) << "; rate=" << worst.stimulus.rate << "; H=" << worst.stimulus.peak << "; worst converged transient by turning8 band residual.\n"
        "time_ms,input_h,reference_m,turning8_m,point16_m,error8_m,error16_m,reference_band_m,turning8_band_m,point16_band_m,error8_band_m,error16_band_m\n";
    const auto refBand = audioBand (worst.reference.samples, worst.stimulus.rate);
    const auto candidateBand = audioBand (worst.candidate.samples, worst.stimulus.rate);
    const auto baselineBand = audioBand (worst.baseline.samples, worst.stimulus.rate);
    auto candidateDifference = worst.candidate.samples, baselineDifference = worst.baseline.samples;
    for (std::size_t i = 0; i < candidateDifference.size(); ++i)
    {
        candidateDifference[i] -= worst.reference.samples[i];
        baselineDifference[i] -= worst.reference.samples[i];
    }
    const auto errorBand = audioBand (candidateDifference, worst.stimulus.rate);
    const auto baselineErrorBand = audioBand (baselineDifference, worst.stimulus.rate);
    for (int i = 0; i < windowSize; ++i)
    {
        const auto n = static_cast<std::size_t> (i);
        trace << i * 1000.0 / worst.stimulus.rate << ',' << worst.stimulus.at (i) << ',' << worst.reference.samples[n] << ',' << worst.candidate.samples[n] << ',' << worst.baseline.samples[n] << ','
            << worst.candidate.samples[n] - worst.reference.samples[n] << ',' << worst.baseline.samples[n] - worst.reference.samples[n]
            << ',' << refBand[n] << ',' << candidateBand[n] << ',' << baselineBand[n] << ',' << errorBand[n] << ',' << baselineErrorBand[n] << '\n';
    }
    REQUIRE (trace.good());
    auto spectral = file ("worst_multitone.csv");
    const auto& multi = *worstMultitone;
    spectral << "# " << name (multi.stimulus.signal) << "; rate=" << multi.stimulus.rate << "; H=" << multi.stimulus.peak << "; worst converged multitone by turning8 band residual.\n"
        "frequency_hz,reference_dbr,turning8_dbr,point16_dbr,error8_dbr,error16_dbr\n";
    const auto a = spectrum (multi.reference.samples), b = spectrum (multi.candidate.samples), c = spectrum (multi.baseline.samples);
    auto rms = 0.0;
    for (auto value : multi.reference.samples) rms += value * value / windowSize;
    rms = std::sqrt (rms);
    // Positive-frequency RMS lines only; DC and Nyquist use a different scaling.
    for (std::size_t i = 1; i + 1 < a.size(); ++i)
        spectral << static_cast<double> (i) * multi.stimulus.rate / windowSize << ',' << lineDb (a[i], rms) << ',' << lineDb (b[i], rms) << ',' << lineDb (c[i], rms) << ',' << lineDb (b[i] - a[i], rms) << ',' << lineDb (c[i] - a[i], rms) << '\n';
    REQUIRE (spectral.good());
}
