#include "TransformerMeasurement.h"
#include "TransformerVoicingState.h"
#include "dsp/VtbOutputTransformer.h"
#include <complex>

namespace TransformerMeasurement
{
const Row& Result::at (int frequency, int level, int amount) const noexcept
{
    return rows[(static_cast<std::size_t> (frequency) * levels.size() + static_cast<std::size_t> (level))
        * amounts.size() + static_cast<std::size_t> (amount)];
}

std::optional<Metrics> measurePoint (TransformerVoicing::Values voice, double frequencyHz,
    float inputDbfs, float amountPercent, const Cancel& cancel)
{
    juce::ScopedNoDenormals noDenormals;
    VtbOutputTransformer transformer;
    transformer.prepare (sampleRate, amountPercent, false, voice, false);
    juce::AudioBuffer<float> block (1, 256);
    constexpr std::array orders { 1, 2, 3, 5, 7 };
    std::array<std::complex<double>, 5> sums {}, oscillators {}, rotations {};
    const auto omega = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
    const auto amplitude = juce::Decibels::decibelsToGain (static_cast<double> (inputDbfs));
    for (std::size_t n = 0; n < orders.size(); ++n)
    {
        oscillators[n] = std::polar (1.0, -omega * orders[n] * warmupSamples);
        rotations[n] = std::polar (1.0, -omega * orders[n]);
    }
    double inputPower = 0.0, outputPower = 0.0;
    for (int start = 0; start < warmupSamples + analysisSamples; start += 256)
    {
        if (cancel && cancel())
            return std::nullopt;
        const auto count = juce::jmin (256, warmupSamples + analysisSamples - start);
        block.setSize (1, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const auto value = static_cast<float> (amplitude * std::sin (omega * (start + i)));
            block.setSample (0, i, value);
            if (start + i >= warmupSamples)
                inputPower += static_cast<double> (value) * value;
        }
        transformer.process (block, 1);
        for (int i = juce::jmax (0, warmupSamples - start); i < count; ++i)
        {
            const auto value = static_cast<double> (block.getSample (0, i));
            outputPower += value * value;
            for (std::size_t n = 0; n < orders.size(); ++n)
            {
                sums[n] += value * oscillators[n];
                oscillators[n] *= rotations[n];
            }
        }
    }
    const auto fundamental = 2.0 * std::abs (sums[0]) / analysisSamples;
    Metrics metrics;
    metrics.fundamentalGainDb = juce::Decibels::gainToDecibels (fundamental / amplitude, -300.0);
    metrics.rmsGainDb = juce::Decibels::gainToDecibels (std::sqrt (outputPower / inputPower), -300.0);
    for (std::size_t n = 0; n < harmonics.size(); ++n)
    {
        metrics.available[n] = frequencyHz * harmonics[n] < sampleRate * 0.5;
        metrics.harmonicDbc[n] = metrics.available[n]
            ? juce::Decibels::gainToDecibels (2.0 * std::abs (sums[n + 1]) / analysisSamples / fundamental, -300.0) : -300.0;
    }
    return metrics;
}

std::optional<Result> measure (const Request& request, const Cancel& cancel,
    const std::function<void (float)>& progress)
{
    Result result;
    result.request = request;
    std::size_t row = 0;
    for (const auto frequency : frequencies)
        for (const auto level : levels)
            for (const auto amount : amounts)
            {
                auto& point = result.rows[row];
                point.frequencyHz = frequency;
                point.inputDbfs = level;
                point.amountPercent = amount;
                for (std::size_t voice = 0; voice < 2; ++voice)
                {
                    const auto metrics = measurePoint (voice == 0 ? request.a : request.b, frequency, level, amount, cancel);
                    if (! metrics)
                        return std::nullopt;
                    point.voices[voice] = *metrics;
                    if (progress)
                        progress (static_cast<float> (row * 2 + voice + 1) / static_cast<float> (rowCount * 2));
                }
                ++row;
            }
    return result;
}

juce::String toCsv (const Result& result)
{
    juce::String csv = "# Transformer snapshots A/B; voltage-driven magnetic hysteresis; match off; transformer In.\n"
        "# 48000 Hz; discard 0.25 s; analyze 1 s; no emphasis shelves; not hardware measurements.\n";
    csv += "# Workbench schema: " + juce::String (TransformerVoicing::model) + "\n";
    for (int voice = 0; voice < 2; ++voice)
    {
        const auto values = voice == 0 ? result.request.a : result.request.b;
        csv += juce::String (voice == 0 ? "# A: " : "# B: ") + TransformerVoicingState::describe (values) + "\n";
    }
    csv += "# " + result.request.contextJson.replaceCharacters ("\r\n", "  ") + "\n";
    csv += "voicing,sample_rate_hz,frequency_hz,input_dbfs,amount_percent,fundamental_gain_db,rms_gain_db,H2_dBc,H3_dBc,H5_dBc,H7_dBc\n";
    for (const auto& row : result.rows)
        for (std::size_t voice = 0; voice < 2; ++voice)
        {
            const auto& metrics = row.voices[voice];
            csv += juce::String (voice == 0 ? "A" : "B") + ",48000," + juce::String (row.frequencyHz, 6) + ","
                + juce::String (row.inputDbfs, 6) + "," + juce::String (row.amountPercent, 6) + ","
                + juce::String (metrics.fundamentalGainDb, 6) + "," + juce::String (metrics.rmsGainDb, 6);
            for (std::size_t n = 0; n < harmonics.size(); ++n)
                csv += "," + (metrics.available[n] ? juce::String (metrics.harmonicDbc[n], 6) : juce::String ("NA"));
            csv += "\n";
        }
    return csv;
}
}
