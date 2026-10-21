#pragma once

#include "MagneticOversampling.h"
#include "TransformerSettings.h"
#include "../TransformerVoicing.h"
#include "../experimental/TransformerAudioCircuit.h"

// Voltage-driven magnetic transformer. Shared by live audio and measurements.
class TransformerSaturation
{
public:
    void prepare (double sampleRate, float amountPercent, bool out,
        TransformerVoicing::Values voicing = {}, bool levelMatch = false);
    void setParameters (float amountPercent, bool out) noexcept;
    void setVoicing (TransformerVoicing::Values voicing, bool levelMatch = false) noexcept;
    void process (juce::AudioBuffer<float>& buffer, int channels) noexcept;
    int getLatencySamples() const noexcept { return latency; }
    int getOversamplingFactor() const noexcept { return factor; }
    const MagneticPrototype::TransformerAudioCircuit& getCircuit (int channel = 0) const noexcept
    { return banks[currentBank].state[static_cast<std::size_t> (channel)].core; }

private:
    struct Channel
    {
        MagneticPrototype::TransformerAudioCircuit core;
    };
    struct Bank
    {
        std::array<Channel, 2> state;
        double driveScale = 1.0;
        TransformerVoicing::Word voicing = 0;
    };
    static double driveGainFor (float amountPercent) noexcept;
    void configureBank (Bank&, TransformerVoicing::Word) noexcept;
    static double processBank (Bank&, int channel, double original, double drive) noexcept;
    void resetCoreHistory() noexcept;
    void resetHistory() noexcept;
    std::array<Bank, 2> banks;
    juce::dsp::Oversampling<double> filters { 2 };
    juce::AudioBuffer<double> scratch;
    juce::AudioBuffer<float> delayedDry;
    MagneticAudio::StereoDelay dryDelay;
    std::size_t currentBank = 0;
    TransformerVoicing::Word requestedVoicing = 0;
    double rate = 48000.0, internalRate = 768000.0;
    int latency = 0, factor = 16;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> driveGain;
    juce::SmoothedValue<float> active, voicingFade;
    std::array<float, MagneticAudio::capacity> activity {};
    double inputPower = 0.0, outputPower = 0.0, matchGain = 1.0;
    double powerCoefficient = 0.0, gainCoefficient = 0.0, maximumMatchGain = 1.0;
    bool fading = false, matchRequested = false, wasProcessing = false;
};
