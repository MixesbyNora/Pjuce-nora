#pragma once

#include "DbGainEnvelope.h"
#include "SoftKneeGainComputer.h"
#include "SidechainFilter.h"
#include "GainTube.h"
#include "AmpTube.h"
#include "AnalogDetector.h"

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
        bool operator== (const GainElement& other) const noexcept
        {
            return tube == other.tube && amp == other.amp && settings.tube == other.settings.tube
                && std::abs (settings.gridLevelDb - other.settings.gridLevelDb) < 1.0e-9 && std::abs (settings.imbalance - other.settings.imbalance) < 1.0e-12;
        }
    };
    void setGainElement (GainElement) noexcept;
    // Headroom's extra drive into both tubes, level-compensated (docs/Overdrive.md).
    // It ramps over 20 ms like Makeup, so automating it is smooth; it is not part
    // of the element, whose changes crossfade.
    void setHeadroomDriveDb (double driveDb, bool immediately = false) noexcept;
    // The detector (docs/Detector.md): the 12AL5 analog sidechain, whose control
    // voltage biases the gain tube, or the Digital law (fixed 4:1, 6 dB knee, dB
    // envelope). Digital here by default; the plugin's default voicing is analog.
    // A switch carries the present gain reduction across.
    struct Detector
    {
        bool analog = false;
        double sidechainGainDb = 0.0;
    };
    void setDetector (Detector) noexcept;

    float getGainReductionDb() const noexcept { return reduction; }

private:
    struct Element
    {
        GainElement settings;
        std::array<GainTube::Cell, 2> cells;
        std::array<AmpTube::Grid, 2> grids;
        void configure (GainElement, double sampleRate) noexcept;
        double render (std::size_t channel, double x, double reductionDb, double digitalGain, double makeupGain, double headroomGain) noexcept
        {
            const auto y = settings.tube ? cells[channel].process (x, reductionDb, headroomGain) * makeupGain : x * digitalGain;
            return settings.amp ? AmpTube::stage().outputForGrid (grids[channel].process (y, headroomGain)) / headroomGain : y;
        }
    };
    Element active, next;
    GainElement requested;
    juce::SmoothedValue<float> elementFade;
    bool fading = false;
    double rate = 48000.0;
    juce::SmoothedValue<float> calibrationDb;
    SoftKneeGainComputer gainComputer;
    DbGainEnvelope envelope;
    AnalogDetector detector;
    AnalogDetector::Settings analogSettings;
    bool analog = false;
    float reduction = 0.0f;
    double analogGain = 1.0, calibrationGain = 1.0;
    std::uint32_t calibrationBits = 0; // The cached gain's Calibration (0 dB).
    SidechainFilter sidechainFilter;
    juce::SmoothedValue<float> inputDb;
    juce::SmoothedValue<float> makeupDb;
    juce::SmoothedValue<double> headroomDriveDb;
    juce::SmoothedValue<float> feedbackAmount;
};
