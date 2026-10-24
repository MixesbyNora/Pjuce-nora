#pragma once

#include "DbGainEnvelope.h"
#include "SoftKneeGainComputer.h"
#include "SidechainFilter.h"
#include "GainTube.h"
#include "AmpTube.h"

class StereoCompressor
{
public:
    void prepare (double sampleRate, const CompressorParameters::Values& values) noexcept;
    void setParameters (const CompressorParameters::Values& values) noexcept;
    // Returns the block's maximum reduction for metering; it does not feed back into DSP.
    float process (juce::AudioBuffer<float>& buffer, int numInputChannels) noexcept;
    // The vari-mu gain element (docs/GainTube.md) and the 5751 amplifier after
    // Makeup (docs/TubeStage.md). Off (the default) is the exact digital gain; a
    // change crossfades over 20 ms from the previous element.
    struct GainElement
    {
        bool tube = false;
        GainTube::Settings settings;
        bool amp = false; // Makeup drives it: the amplifier follows the makeup gain.
        double ampDriveDb = 0.0; // Headroom's drive into the amplifier, level-compensated.
        bool operator== (const GainElement& other) const noexcept
        {
            return tube == other.tube && amp == other.amp && std::abs (ampDriveDb - other.ampDriveDb) < 1.0e-9 && settings.tube == other.settings.tube
                && std::abs (settings.gridLevelDb - other.settings.gridLevelDb) < 1.0e-9 && std::abs (settings.imbalance - other.settings.imbalance) < 1.0e-12;
        }
    };
    void setGainElement (GainElement) noexcept;

    float getGainReductionDb() const noexcept { return envelope.getGainReductionDb(); }

private:
    struct Element
    {
        GainElement settings;
        std::array<GainTube::Cell, 2> cells;
        std::array<AmpTube::Grid, 2> grids;
        void configure (GainElement, double sampleRate) noexcept;
        double render (std::size_t channel, double x, double reductionDb, double digitalGain, double makeupGain) noexcept
        {
            const auto y = settings.tube ? cells[channel].process (x, reductionDb) * makeupGain : x * digitalGain;
            return settings.amp ? AmpTube::stage().outputForGrid (grids[channel].process (y * ampDrive)) / ampDrive : y;
        }
        double ampDrive = 1.0;
    };
    Element active, next;
    GainElement requested;
    juce::SmoothedValue<float> elementFade;
    bool fading = false;
    double rate = 48000.0;
    juce::SmoothedValue<float> calibrationDb;
    SoftKneeGainComputer gainComputer;
    DbGainEnvelope envelope;
    SidechainFilter sidechainFilter;
    juce::SmoothedValue<float> inputDb;
    juce::SmoothedValue<float> makeupDb;
    juce::SmoothedValue<float> feedbackAmount;
};
