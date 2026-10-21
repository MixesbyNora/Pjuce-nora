#include <dsp/TransformerSaturation.h>
#include <catch2/catch_test_macros.hpp>
#include <complex>
#include <fstream>
#include <iomanip>

namespace
{
constexpr double rate = 48000.0;
constexpr int blockSize = 256;
constexpr int warm = 48128;
constexpr int capture = 24000; // Coherent: 30 bass periods and 1000 probe periods.
constexpr double probePeak = 0.001; // -60 dBFS before IN gain.
using Diagnostics = MagneticPrototype::TransformerAudioCircuit::Diagnostics;
struct Result
{
    std::array<std::complex<double>, 2> probe {};
    std::array<Diagnostics, 2> diagnostics {};
    std::array<double, 2> coreInputPeak {};
    double mirrorError = 0.0;
};

// Offline counterfactual only. Replacing v by v/S, D by D*S and scaling
// the result by S preserves D*v, H, M and y until the voltage guard acts.
// This raises only the effective input guard from 16 to 16*S, without
// changing production code, constitutive settings or solver tolerance.
Result render (double bassDb, double inDb, double calibrationDb, int polarity,
    bool bassPresent, double guardScale = 1.0, bool compareShipped = false)
{
    juce::ScopedNoDenormals noDenormals;
    TransformerVoicing::Values voice;
    voice[TransformerVoicing::driveCalibration] = static_cast<float> (calibrationDb);
    TransformerSaturation stage;
    stage.prepare (rate, 100.0f, false, voice, false);
    juce::dsp::Oversampling<double> filters (2);
    MagneticAudio::prepareFilters (filters, stage.getOversamplingFactor());
    std::array<MagneticPrototype::TransformerAudioCircuit, 2> circuits;
    const auto drive = juce::Decibels::decibelsToGain (12.0)
        * juce::Decibels::decibelsToGain (calibrationDb);
    for (auto& core : circuits)
    {
        core.prepare (rate * stage.getOversamplingFactor(), {});
        core.setDriveGain (drive * guardScale);
    }
    juce::AudioBuffer<float> block (2, blockSize);
    juce::AudioBuffer<double> scratch (2, blockSize);
    const auto bass = bassPresent ? juce::Decibels::decibelsToGain (bassDb) : 0.0;
    const auto inputGain = juce::Decibels::decibelsToGain (inDb);
    Result result;
    std::array<Diagnostics, 2> before {};
    // Align capture to an exact warm boundary and the FIR's integer delay.
    const auto first = warm + stage.getLatencySamples();
    const auto end = ((first + capture + blockSize - 1) / blockSize) * blockSize;
    for (int start = 0; start < end; start += blockSize)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
            {
                const auto t = (start + n) / rate;
                const auto phase = ch == 0 ? 0.0 : juce::MathConstants<double>::halfPi;
                const auto sample = static_cast<float> (inputGain * (bass * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t + phase)
                    + polarity * probePeak * std::sin (2.0 * juce::MathConstants<double>::pi * 2000.0 * t)));
                block.setSample (ch, n, sample);
                scratch.setSample (ch, n, sample);
            }
        if (guardScale == 1.0 && ! compareShipped)
            stage.process (block, 2);
        else
        {
            auto base = juce::dsp::AudioBlock<double> (scratch);
            auto high = filters.processSamplesUp (base);
            for (std::size_t n = 0; n < high.getNumSamples(); ++n)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto i = static_cast<std::size_t> (ch);
                    const auto coreInput = high.getSample (ch, static_cast<int> (n));
                    if (start >= warm && start < warm + capture)
                        result.coreInputPeak[i] = std::max (result.coreInputPeak[i], std::abs (coreInput));
                    const auto voltage = circuits[i].processSample (coreInput / guardScale) * guardScale;
                    high.setSample (ch, static_cast<int> (n), voltage);
                }
            filters.processSamplesDown (base);
            if (compareShipped) stage.process (block, 2);
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < blockSize; ++n)
                {
                    const auto sample = static_cast<float> (scratch.getSample (ch, n));
                    if (compareShipped) result.mirrorError = std::max (result.mirrorError, std::abs (static_cast<double> (sample) - block.getSample (ch, n)));
                    block.setSample (ch, n, sample);
                }
        }
        // Diagnostics refer to core input time, rather than FIR output time.
        // Snapshot on block boundaries; use a half-second integer-period window.
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto i = static_cast<std::size_t> (ch);
            const auto& d = guardScale == 1.0 && ! compareShipped ? stage.getCircuit (ch).getDiagnostics() : circuits[i].getDiagnostics();
            if (start + blockSize == warm) before[i] = d;
            if (start + blockSize == warm + 24064)
            {
                result.diagnostics[i] = d;
                result.diagnostics[i].limitedInputs -= before[i].limitedInputs;
                result.diagnostics[i].invalidInputs -= before[i].invalidInputs;
                result.diagnostics[i].solverFailures -= before[i].solverFailures;
            }
            for (int n = 0; n < blockSize; ++n)
                if (start + n >= first && start + n < first + capture)
                {
                    const auto phase = -2.0 * juce::MathConstants<double>::pi * 2000.0 * (start + n - stage.getLatencySamples()) / rate;
                    result.probe[i] += static_cast<double> (block.getSample (ch, n)) * std::polar (2.0 / capture, phase);
                }
        }
    }
    return result;
}

double db (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-30)); }
}

TEST_CASE ("Offline diagnosis separates magnetic HF survival from the input safety clamp", "[magnetic-diagnosis]")
{
    const auto* directory = std::getenv ("LUMIERE_TRANSFORMER_DIAGNOSIS_DIR");
    const auto full = directory != nullptr;
    std::ofstream csv;
    if (full)
    {
        const juce::File root (juce::String::fromUTF8 (directory));
        REQUIRE (root.createDirectory().wasOk());
        csv.open (root.getChildFile ("diagnosis.csv").getFullPathName().toStdString());
        csv << std::setprecision (17) << "bass_dbfs,in_gain_db,calibration_db,total_drive_db,channel,probe_output_dbfs,probe_gain_db,hf_survival_ratio,hf_survival_db,limited_inputs,invalid_inputs,diagnostic_internal_samples,limited_percent,raised_guard_probe_gain_db,raised_guard_hf_survival_ratio,raised_guard_hf_survival_db,raised_guard_limited_inputs,raised_guard_invalid_inputs,core_input_peak,mirror_max_error,solver_failures\n";
    }
    for (double bassDb : { -12.0, -6.0, 0.0 })
        for (double inDb : { 0.0, 6.0, 12.0, 18.0, 24.0 })
            for (double calibration : { -12.0, 0.0, 12.0 })
            {
                if (! full && (bassDb != 0.0 || calibration != 0.0 || (inDb != 0.0 && inDb != 24.0))) continue;
                const auto plus = render (bassDb, inDb, calibration, 1, true);
                const auto minus = render (bassDb, inDb, calibration, -1, true);
                const auto probeOnly = render (bassDb, inDb, calibration, 1, false);
                const auto raisedPlus = render (bassDb, inDb, calibration, 1, true, 16.0);
                const auto raisedMinus = render (bassDb, inDb, calibration, -1, true, 16.0);
                const auto mirror = render (bassDb, inDb, calibration, 1, true, 1.0, true);
                for (std::size_t ch = 0; ch < 2; ++ch)
                {
                    const auto component = std::abs ((plus.probe[ch] - minus.probe[ch]) * 0.5);
                    const auto raisedComponent = std::abs ((raisedPlus.probe[ch] - raisedMinus.probe[ch]) * 0.5);
                    const auto expected = probePeak * juce::Decibels::decibelsToGain (inDb);
                    const auto survival = component / std::abs (probeOnly.probe[ch]);
                    const auto raisedSurvival = raisedComponent / std::abs (probeOnly.probe[ch]);
                    const auto& d = plus.diagnostics[ch];
                    CAPTURE (bassDb, inDb, calibration, ch, survival, raisedSurvival, d.limitedInputs);
                    CHECK (std::isfinite (survival));
                    CHECK (d.invalidInputs == 0);
                    CHECK (d.solverFailures == 0);
                    CHECK (d.maximumEquationResidual <= MagneticPrototype::AudioSettings::rootTolerance);
                    CHECK (raisedPlus.diagnostics[ch].limitedInputs == 0);
                    CHECK (raisedPlus.diagnostics[ch].invalidInputs == 0);
                    CHECK (raisedPlus.diagnostics[ch].solverFailures == 0);
                    CHECK (mirror.mirrorError < 1.0e-6);
                    CHECK (mirror.diagnostics[ch].limitedInputs == d.limitedInputs);
                    if (inDb == 0.0) CHECK (d.limitedInputs == 0);
                    if (inDb == 24.0 && bassDb == 0.0)
                    {
                        CHECK (d.limitedInputs == 0);
                        CHECK (raisedSurvival > 0.98);
                        CHECK (survival > 0.98);
                    }
                    if (full)
                        csv << bassDb << ',' << inDb << ',' << calibration << ',' << 12.0 + calibration << ',' << (ch == 0 ? 'L' : 'R')
                            << ',' << db (component) << ',' << db (component / expected) << ',' << survival << ',' << db (survival)
                            << ',' << d.limitedInputs << ',' << d.invalidInputs << ',' << 24064 * 16 << ',' << 100.0 * static_cast<double> (d.limitedInputs) / (24064 * 16)
                            << ',' << db (raisedComponent / expected) << ',' << raisedSurvival << ',' << db (raisedSurvival)
                            << ',' << raisedPlus.diagnostics[ch].limitedInputs << ',' << raisedPlus.diagnostics[ch].invalidInputs
                            << ',' << mirror.coreInputPeak[ch] << ',' << mirror.mirrorError << ',' << d.solverFailures << '\n';
                }
            }
    if (full) REQUIRE (csv.good());
}
