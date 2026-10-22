#include "ShuntAnalysis.h"
#include <dsp/TransformerSaturation.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

namespace
{
using namespace ModuleResearch;
using namespace ShuntResearch;
using Input = std::function<double (int, double)>;
struct Rendered
{
    std::array<std::vector<double>, 2> audio;
    double latency = 0;
    int factor = 0;
    std::uint64_t failures = 0, limited = 0, invalid = 0;
};
std::ofstream csv (const juce::File& root, const char* file, const char* header)
{
    std::ofstream result (root.getChildFile (file).getFullPathName().toStdString());
    result << std::setprecision (17) << header << '\n'; return result;
}
Rendered renderLong (int model, int factor, double driveDb, const Input& input, int length, int warm = 48000)
{
    std::unique_ptr<Processor> prototype;
    std::unique_ptr<TransformerSaturation> shipped;
    double latency = 0;
    if (model < 2)
    {
        TransformerParameters parameters; parameters.driveDb = driveDb;
        prototype = std::make_unique<Processor> (rate, factor, 2, model == 0 ? Variant::smooth : Variant::play3, Topology::shunt, parameters);
        latency = prototype->latency();
    }
    else if (model == 2)
    {
        shipped = std::make_unique<TransformerSaturation>();
        // Amount 100 is +12 dB. Calibration -12/-6 makes the net applied
        // drive 0/+6 dB, identical to the prototype's drive, without changing
        // any shipped magnetic defaults or using its level matcher.
        TransformerVoicing::Values voice; voice[TransformerVoicing::driveCalibration] = static_cast<float> (driveDb - 12);
        shipped->prepare (rate, 100, false, voice, false);
        latency = shipped->getLatencySamples(); factor = shipped->getOversamplingFactor();
    }
    const auto count = ((warm + length + 1024 + capacity - 1) / capacity) * capacity;
    std::array<std::vector<double>, 2> raw;
    for (auto& channel : raw) channel.resize (static_cast<std::size_t> (count));
    juce::AudioBuffer<double> block (2, capacity);
    juce::AudioBuffer<float> floatBlock (2, capacity);
    for (int start = 0; start < count; start += capacity)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < capacity; ++n) block.setSample (ch, n, input (ch, start + n - warm));
        if (prototype) prototype->process (block);
        else if (shipped)
        {
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < capacity; ++n) floatBlock.setSample (ch, n, static_cast<float> (block.getSample (ch, n)));
            shipped->process (floatBlock, 2);
            for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < capacity; ++n) block.setSample (ch, n, floatBlock.getSample (ch, n));
        }
        for (int ch = 0; ch < 2; ++ch) std::copy_n (block.getReadPointer (ch), capacity, raw[static_cast<std::size_t> (ch)].data() + start);
    }
    Rendered result; result.latency = latency; result.factor = model == 3 ? 0 : factor;
    for (int ch = 0; ch < 2; ++ch)
        result.audio[static_cast<std::size_t> (ch)] = MagneticValidation::alignTrace (raw[static_cast<std::size_t> (ch)], warm, latency, length).samples;
    if (prototype)
    {
        result.failures = prototype->diagnostics().failures;
        result.limited = prototype->transformer.safety.limited;
        result.invalid = prototype->transformer.safety.invalid;
    }
    if (shipped)
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& d = shipped->getCircuit (ch).getDiagnostics();
            result.failures += d.solverFailures; result.limited += d.limitedInputs; result.invalid += d.invalidInputs;
        }
    CHECK (result.failures == 0); CHECK (result.invalid == 0);
    for (const auto& channel : result.audio) for (auto x : channel) if (! isFinite (x)) { FAIL ("Nonfinite shunt render"); }
    return result;
}
const char* modelName (int model) { return model == 0 ? "A" : model == 1 ? "B" : model == 2 ? "shipped" : "input"; }
void sineReport (const juce::File& root)
{
    auto out = csv (root, "sines.csv", "kind,model,drive_db,requested_hz,actual_hz,fundamental_bin,factor,band_low_hz,band_high_hz,fundamental_dbfs,harmonic_dbfs,nonharmonic_dbfs,nonharmonic_to_harmonic_db,residual_to_reference_dbfs,reference_64_128_dbr,period_difference_dbr,solver_failures,limited_inputs,invalid_inputs");
    auto harmonics = csv (root, "harmonics.csv", "kind,model,drive_db,actual_hz,factor,order,frequency_hz,amplitude_dbfs");
    // Odd coherent bins ensure low-order folded components do not all land on
    // harmonic bins (a literal 1 kHz tone at 48 kHz would hide that ambiguity).
    for (int model = 0; model < 3; ++model)
        for (double drive : { 0.0, 6.0 })
            for (double requested : { 55.0, 110.0, 440.0, 1000.0, 7001.0, 13001.0, 19001.0 })
            {
                const auto bin = static_cast<int> (2 * std::round ((requested * sineSize / rate - 1) / 2) + 1);
                const auto frequency = bin * rate / sineSize;
                const Input wave = [=] (int, double n) { return 0.8 * std::sin (2 * pi * bin * n / sineSize); };
                auto reference = renderLong (model, 128, drive, wave, static_cast<int> (sineSize * 2), static_cast<int> (sineSize));
                const auto refSpan = std::span (reference.audio[0]).first (sineSize);
                double referenceError = 0;
                if (model < 2)
                {
                    const auto lower = renderLong (model, 64, drive, wave, static_cast<int> (sineSize), static_cast<int> (sineSize));
                    referenceError = relativeDifference (lower.audio[0], refSpan);
                }
                for (int factor : { 4, 8, 16, 32 })
                {
                    if (model == 2 && factor != 16) continue;
                    const auto candidate = model == 2 ? reference : renderLong (model, factor, drive, wave, static_cast<int> (sineSize * 2), static_cast<int> (sineSize));
                    const auto first = std::span (candidate.audio[0]).first (sineSize), second = std::span (candidate.audio[0]).subspan (sineSize);
                    const auto periodError = relativeDifference (first, second);
                    const auto values = split (first, refSpan, bin);
                    for (std::size_t band = 0; band < values.size(); ++band)
                    {
                        const auto& r = values[band];
                        out << "measurement," << modelName (model) << ',' << drive << ',' << requested << ',' << frequency << ',' << bin << ',' << candidate.factor << ',' << bandEdges[band] << ',' << bandEdges[band + 1] << ','
                            << powerDb (r.fundamental) << ',' << powerDb (r.harmonic) << ',' << powerDb (r.nonHarmonic) << ',' << powerDb (r.nonHarmonic / std::max (r.harmonic, 1.0e-30)) << ','
                            << powerDb (r.residual) << ',' << referenceError << ',' << periodError << ',' << candidate.failures << ',' << candidate.limited << ',' << candidate.invalid << '\n';
                    }
                    const auto bins = fft (first);
                    for (int order = 1; static_cast<double> (order) * frequency <= 20000; ++order)
                        harmonics << "measurement," << modelName (model) << ',' << drive << ',' << frequency << ',' << candidate.factor << ',' << order << ',' << order * frequency << ',' << db (2 * std::abs (bins[static_cast<std::size_t> (order * bin)])) << '\n';
                }
                out.flush(); harmonics.flush();
                std::cout << "shunt sine " << modelName (model) << ' ' << drive << ' ' << frequency << " reference " << referenceError << std::endl;
            }
    REQUIRE (out.good()); REQUIRE (harmonics.good());
}
void gridReport (const juce::File& root)
{
    auto out = csv (root, "off_grid.csv", "kind,variant,rate_hz,signal,peak,factor,warm_frames,warm_seconds,off_grid_dbr,reference_off_grid_dbr,band_residual_dbr,reference_64_128_dbr,solver_failures,original_screen_pass");
    // Repeat every previously failing shunt setting at every originally tested
    // factor. The original helper and its -80 dBr screen remain untouched.
    const auto oldPath = root.getParentDirectory().getChildFile ("module_spike/validation.csv");
    std::ifstream old (oldPath.getFullPathName().toStdString()); REQUIRE (old.good());
    std::string line; std::getline (old, line);
    std::set<std::tuple<int, std::string, double>> failed;
    while (std::getline (old, line))
    {
        std::vector<std::string> fields; std::istringstream stream (line); std::string value;
        while (std::getline (stream, value, ',')) fields.push_back (value);
        if (fields.size() > 17 && fields[2] == "shunt" && fields[3] == "192000" && fields[7] == "8" && (fields[5] == "low_high" || fields[5] == "close_high" || fields[5] == "six_tone") && std::stod (fields[15]) > -80)
            failed.emplace (fields[1] == "A" ? 0 : 1, fields[5], std::stod (fields[6]));
    }
    REQUIRE (! failed.empty());
    for (const auto& [model, signalName, peak] : failed)
    {
        const auto variant = model == 0 ? Variant::smooth : Variant::play3;
        const auto signal = signalName == "low_high" ? MagneticValidation::Signal::lowHigh : signalName == "close_high" ? MagneticValidation::Signal::closeHigh : MagneticValidation::Signal::sixTone;
        const MagneticValidation::Stimulus s (signal, 192000, peak);
        const Waveform wave = [&] (double n) { return s.at (n); };
        for (int warm : { 16384, 192000, 384000 })
        {
            const auto reference = render (192000, 128, variant, Topology::shunt, {}, wave, true, Coupling::transformer, 1000, true, 1, warm);
            const auto lower = render (192000, 64, variant, Topology::shunt, {}, wave, true, Coupling::transformer, 1000, true, 1, warm);
            const auto refMetrics = MagneticValidation::measure (reference.trace, reference.trace, s);
            const auto convergence = MagneticValidation::measure (lower.trace, reference.trace, s);
            for (int factor : { 1, 2, 4, 8 })
            {
                const auto candidate = render (192000, factor, variant, Topology::shunt, {}, wave, false, Coupling::transformer, 1000, true, 1, warm);
                const auto m = MagneticValidation::measure (candidate.trace, reference.trace, s);
                out << "measurement," << name (variant) << ",192000," << signalName << ',' << peak << ',' << factor << ',' << warm << ',' << static_cast<double> (warm) / 192000 << ','
                    << m.offGridDb << ',' << refMetrics.offGridDb << ',' << m.bandResidualDb << ',' << convergence.bandResidualDb << ',' << candidate.diagnostics.failures + reference.diagnostics.failures + lower.diagnostics.failures << ',' << (m.offGridDb <= MagneticValidation::residualScreenDb) << '\n';
                CHECK (candidate.diagnostics.failures == 0);
            }
            CHECK (reference.diagnostics.failures == 0); CHECK (lower.diagnostics.failures == 0); out.flush();
            std::cout << "shunt window " << name (variant) << ' ' << signalName << ' ' << peak << ' ' << warm << std::endl;
        }
    }
    REQUIRE (out.good());
}
struct AudioFile
{
    std::string stem;
    Rendered render;
    double rms = 0, peak = 0;
};
void writeWav (const juce::File& path, const Rendered& render, double gain)
{
    juce::AudioBuffer<float> audio (2, static_cast<int> (render.audio[0].size()));
    for (int ch = 0; ch < 2; ++ch) for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample (ch, n, static_cast<float> (gain * render.audio[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]));
    auto stream = path.createOutputStream(); REQUIRE (stream != nullptr);
    REQUIRE (stream->setPosition (0)); REQUIRE (stream->truncate().wasOk());
    std::unique_ptr<juce::OutputStream> output = std::move (stream);
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (output, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    REQUIRE (writer != nullptr); REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
}
// STFT peak tracks are candidates, not automatic proof of aliasing. Require a
// descending ridge, proximity to an internal-rate folded harmonic, and excess
// over a separately rendered high-rate reference. Keep all tracks in CSV.
void sweepTracks (std::ofstream& out, int model, int factor, double drive, const Rendered& candidate, const Rendered& reference)
{
    constexpr std::size_t size = 4096, hop = 512;
    struct Peak { double frequency = 0, amplitude = 0, referenceAmplitude = 0; int frame = 0; };
    std::vector<Peak> previous;
    for (std::size_t start = 0; start + size <= candidate.audio[0].size(); start += hop)
    {
        std::vector<double> segment (size), referenceSegment (size);
        for (std::size_t n = 0; n < size; ++n)
        {
            const auto window = 0.5 - 0.5 * std::cos (2 * pi * static_cast<double> (n) / static_cast<double> (size));
            segment[n] = candidate.audio[0][start + n] * window;
            referenceSegment[n] = reference.audio[0][start + n] * window;
        }
        const auto bins = fft (segment), ref = fft (referenceSegment);
        const auto time = static_cast<double> (start + size / 2) / rate;
        const auto inputHz = sweepFrequency (time);
        std::vector<Peak> current;
        for (std::size_t bin = 3; bin + 1 < size / 2; ++bin)
        {
            const auto hz = static_cast<double> (bin) * rate / size;
            const auto amplitude = 4 * std::abs (bins[bin]);
            if (hz > 20000 || hz < 250 || db (amplitude) < -110 || std::norm (bins[bin]) <= std::norm (bins[bin - 1]) || std::norm (bins[bin]) <= std::norm (bins[bin + 1])) continue;
            const Peak peak { hz, amplitude, 4 * std::abs (ref[bin]), static_cast<int> (start / hop) };
            current.push_back (peak);
            const Peak* match = nullptr;
            for (const auto& old : previous)
                if (old.frequency > hz && old.frequency - hz <= 1500 && (! match || old.frequency - hz < match->frequency - hz)) match = &old;
            if (! match) continue;
            const auto slope = (hz - match->frequency) / (hop / rate);
            int harmonic = 0, image = 0; double distance = 1.0e30;
            // Negative-slope branch f = m*internalRate - h*inputFrequency.
            for (int m = 1; m <= 2; ++m)
            {
                const auto h = static_cast<int> (std::round ((m * rate * factor - hz) / inputHz));
                if (h < 2 || h > 4096) continue;
                const auto error = std::abs (m * rate * factor - h * inputHz - hz);
                if (error < distance) { distance = error; harmonic = h; image = m; }
            }
            const auto expectedSlope = -harmonic * inputHz * std::log (1000.0) / sweepSeconds;
            const auto slopeMatch = std::abs (expectedSlope) > 1.0e-12 && std::abs (slope / expectedSlope - 1) < 0.35;
            const auto confirmedCandidate = model < 2 && harmonic > 0 && distance < 2 * rate / size && slopeMatch && db (amplitude / std::max (peak.referenceAmplitude, 1.0e-15)) >= 10;
            out << "measurement," << modelName (model) << ',' << drive << ',' << factor << ',' << time << ',' << inputHz << ',' << hz << ',' << match->frequency << ',' << slope << ','
                << db (amplitude) << ',' << db (peak.referenceAmplitude) << ',' << harmonic << ',' << image << ',' << distance << ',' << expectedSlope << ',' << confirmedCandidate << '\n';
        }
        previous = std::move (current);
    }
}
void referenceBandReport (const juce::File& root)
{
    auto out = csv (root, "reference_band_check.csv", "kind,material,model,drive_db,reference_64_128_full_dbr,reference_64_128_band_dbr,solver_failures");
    for (int type = 0; type < 4; ++type)
        for (int model = 0; model < 2; ++model)
            for (double drive : { 0.0, 6.0 })
            {
                const auto frames = static_cast<int> (type == 3 ? sweepSeconds * rate : rate);
                const Input input = [=] (int ch, double n) { return type == 3 ? sweep (n) : material (type, ch, n); };
                const auto a = renderLong (model, 64, drive, input, frames);
                const auto b = renderLong (model, 128, drive, input, frames);
                out << "measurement," << (type == 0 ? "bass" : type == 1 ? "drums" : type == 2 ? "vocal-like" : "log-sweep") << ',' << modelName (model) << ',' << drive << ','
                    << relativeDifference (a.audio[0], b.audio[0]) << ',' << bandDifference (a.audio[0], b.audio[0]) << ',' << a.failures + b.failures << '\n';
                out.flush();
                std::cout << "shunt reference-band " << type << ' ' << modelName (model) << ' ' << drive << std::endl;
            }
    REQUIRE (out.good());
}
void renderReport (const juce::File& root)
{
    const auto audioRoot = root.getChildFile ("audio"); REQUIRE (audioRoot.createDirectory().wasOk());
    auto out = csv (root, "renders.csv", "kind,file,material,model,drive_db,factor,raw_rms,raw_peak,applied_gain,matched_rms,matched_peak,rate_hz,channels,frames,latency_samples,solver_failures,limited_inputs,invalid_inputs");
    auto tracks = csv (root, "sweep_tracks.csv", "kind,model,drive_db,factor,time_s,input_hz,frequency_hz,previous_frequency_hz,slope_hz_per_s,level_dbfs,reference_level_dbfs,fold_harmonic,fold_image,fold_distance_hz,expected_slope_hz_per_s,fold_candidate");
    auto spectrumOut = csv (root, "material_bands.csv", "kind,material,model,drive_db,factor,band_low_hz,band_high_hz,raw_band_dbfs");
    auto convergence = csv (root, "render_convergence.csv", "kind,material,model,drive_db,reference_64_128_dbr");
    for (int materialIndex = 0; materialIndex < 4; ++materialIndex)
        for (double drive : { 0.0, 6.0 })
        {
            const auto frames = static_cast<int> (materialIndex == 3 ? sweepSeconds * rate : rate);
            const auto label = materialIndex == 0 ? "bass" : materialIndex == 1 ? "drums" : materialIndex == 2 ? "vocal-like" : "log-sweep";
            const Input wave = [=] (int ch, double n) { return materialIndex == 3 ? sweep (n) : material (materialIndex, ch, n); };
            std::vector<AudioFile> files;
            for (int model = 0; model < 4; ++model)
            {
                Rendered reference;
                if (model < 2)
                {
                    reference = renderLong (model, 128, drive, wave, frames);
                    const auto lower = renderLong (model, 64, drive, wave, frames);
                    convergence << "measurement," << label << ',' << modelName (model) << ',' << drive << ',' << relativeDifference (lower.audio[0], reference.audio[0]) << '\n'; convergence.flush();
                }
                for (int factor : { 4, 8, 16, 32 })
                {
                    if (model >= 2 && factor != 16) continue;
                    auto render = renderLong (model, factor, drive, wave, frames);
                    if (materialIndex == 3 && model < 2) sweepTracks (tracks, model, factor, drive, render, reference);
                    AudioFile file;
                    file.stem = std::string (label) + "_" + modelName (model) + "_shunt_" + std::to_string (model == 3 ? 0 : render.factor) + "x_drive-" + std::to_string (static_cast<int> (drive)) + "dB_tanh_rolloff-off";
                    // Preserve coherent one-second clips. Zero-padding is for
                    // material band summaries only; it is never a sine screen.
                    std::vector<double> padded (sineSize, 0);
                    std::copy_n (render.audio[0].begin(), std::min (padded.size(), render.audio[0].size()), padded.begin());
                    const auto bins = fft (padded);
                    for (std::size_t band = 0; band + 1 < bandEdges.size(); ++band)
                    {
                        double power = 0;
                        for (std::size_t k = 1; k < bins.size() / 2; ++k)
                            if (static_cast<double> (k) * rate / sineSize >= bandEdges[band] && static_cast<double> (k) * rate / sineSize < bandEdges[band + 1]) power += 2 * std::norm (bins[k]);
                        spectrumOut << "measurement," << label << ',' << modelName (model) << ',' << drive << ',' << render.factor << ',' << bandEdges[band] << ',' << bandEdges[band + 1] << ',' << powerDb (power * static_cast<double> (sineSize) / std::min (static_cast<double> (frames), static_cast<double> (sineSize))) << '\n';
                    }
                    for (const auto& channel : render.audio) for (auto x : channel) { file.rms += x * x; file.peak = std::max (file.peak, std::abs (x)); }
                    file.rms = std::sqrt (file.rms / (2 * frames)); file.render = std::move (render); files.push_back (std::move (file));
                }
            }
            double target = 0.1;
            for (const auto& file : files) target = std::min (target, 0.9 * file.rms / std::max (file.peak, 1.0e-30));
            for (const auto& file : files)
            {
                const auto gain = target / std::max (file.rms, 1.0e-30);
                writeWav (audioRoot.getChildFile (juce::String (file.stem) + ".wav"), file.render, gain);
                out << "measurement,audio/" << file.stem << ".wav," << label << ',' << (file.stem.find ("_shipped_") != std::string::npos ? "shipped" : file.stem.find ("_input_") != std::string::npos ? "input" : file.stem.find ("_A_") != std::string::npos ? "A" : "B") << ',' << drive << ',' << file.render.factor << ',' << file.rms << ',' << file.peak << ',' << gain << ',' << file.rms * gain << ',' << file.peak * gain << ",48000,2," << frames << ',' << file.render.latency << ',' << file.render.failures << ',' << file.render.limited << ',' << file.render.invalid << '\n';
            }
            out.flush(); tracks.flush(); spectrumOut.flush();
            std::cout << "shunt material " << label << ' ' << drive << std::endl;
        }
    REQUIRE (out.good()); REQUIRE (tracks.good()); REQUIRE (spectrumOut.good()); REQUIRE (convergence.good());
}
}

TEST_CASE ("Shunt harmonic split separates coherent harmonics folded lines and broadband energy", "[shunt-analysis]")
{
    using namespace ShuntResearch;
    CHECK (ModuleResearch::periodicWarmFrames (48000) >= 48000);
    CHECK (ModuleResearch::periodicWarmFrames (192000) == 192000);
    constexpr std::size_t count = 4096; constexpr int bin = 5;
    std::vector<double> reference (count), signal (count);
    for (std::size_t n = 0; n < count; ++n)
    {
        reference[n] = std::sin (2 * pi * bin * static_cast<double> (n) / count) + 0.1 * std::sin (2 * pi * bin * 3 * static_cast<double> (n) / count);
        signal[n] = reference[n] + 0.001 * std::sin (2 * pi * 37 * static_cast<double> (n) / count);
    }
    const auto result = split (signal, reference, bin);
    CHECK (std::abs (result[0].fundamental - 0.5) < 1.0e-12);
    CHECK (std::abs (result[0].harmonic - 0.005) < 1.0e-12);
    CHECK (std::abs (result[1].nonHarmonic - 0.0000005) < 1.0e-14);
    CHECK (std::abs (result[1].residual - 0.0000005) < 1.0e-14);
    // Parseval sanity check keeps a silent/incorrect FFT from passing the split.
    const auto bins = fft (signal);
    double timePower = 0, spectralPower = 0;
    for (auto x : signal) timePower += x * x / count;
    for (auto x : bins) spectralPower += std::norm (x);
    CHECK (std::abs (timePower - spectralPower) < 1.0e-12);
    CHECK (std::abs (bandDifference (signal, reference) - powerDb (0.0000005 / 0.505)) < 1.0e-8);
}

TEST_CASE ("Shunt off-grid screen distinguishes initial conditioning from stationary output", "[shunt-analysis]")
{
    using namespace ModuleResearch;
    const MagneticValidation::Stimulus stimulus (MagneticValidation::Signal::lowHigh, 192000, 0.05);
    const Waveform wave = [&] (double n) { return stimulus.at (n); };
    for (auto variant : { Variant::smooth, Variant::play3 })
    {
        const auto initial = render (192000, 8, variant, Topology::shunt, {}, wave);
        const auto settled = render (192000, 8, variant, Topology::shunt, {}, wave, false, Coupling::transformer, 1000, true, 1, 192000);
        const auto initialMetrics = MagneticValidation::measure (initial.trace, initial.trace, stimulus);
        const auto settledMetrics = MagneticValidation::measure (settled.trace, settled.trace, stimulus);
        CAPTURE (name (variant), initialMetrics.offGridDb, settledMetrics.offGridDb);
        CHECK (initialMetrics.offGridDb > MagneticValidation::residualScreenDb);
        CHECK (settledMetrics.offGridDb <= MagneticValidation::residualScreenDb);
        CHECK (initial.diagnostics.failures == 0); CHECK (settled.diagnostics.failures == 0);
    }
}

TEST_CASE ("Export shunt improvement diagnosis without model changes", "[shunt-report]")
{
    const auto* taskValue = std::getenv ("LUMIERE_MODULE_TASK");
    const std::string task = taskValue ? taskValue : "";
    if (! task.starts_with ("shunt-step1")) { SUCCEED ("Explicit shunt measurement opt-in"); return; }
    const auto* directory = std::getenv ("LUMIERE_MODULE_REPORT_DIR"); REQUIRE (directory != nullptr);
    const juce::File root (juce::String::fromUTF8 (directory)); REQUIRE (root.createDirectory().wasOk());
    if (task == "shunt-step1" || task == "shunt-step1-sines") sineReport (root);
    if (task == "shunt-step1" || task == "shunt-step1-grid") gridReport (root);
    if (task == "shunt-step1" || task == "shunt-step1-renders") renderReport (root);
    if (task == "shunt-step1" || task == "shunt-step1-convergence") referenceBandReport (root);
}
