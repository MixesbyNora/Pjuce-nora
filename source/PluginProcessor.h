#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/BaSignalPath.h"
#include "GainLinkController.h"
#include "TransformerVoicingState.h"

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    TransformerVoicingState& getTransformerVoicing() noexcept { return transformerVoicing; }
    TransformerVoicingState& getInputTransformerVoicing() noexcept { return inputTransformerVoicing; }
    // Workbench feedback JSON for the output (default) or input transformer.
    juce::String exportTransformerVoicing (bool inputStage = false) const;

    // Maximum positive attenuation in the most recent block; safe for UI polling.
    float getGainReductionDb() const noexcept
    {
        return currentGainReductionDb.load (std::memory_order_relaxed);
    }

    // Single UI consumer: each channel resets atomically after returning its peak.
    LevelMeterPeaks::Readings readLevelPeaks() noexcept
    {
        auto readings = levelPeaks.read();
        if (getTotalNumInputChannels() == 1)
        {
            readings.inR = readings.inL;
            readings.outR = readings.outL;
        }
        return readings;
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    CompressorParameters::Values readParameters() const noexcept;
    BaParameters::Values readBaParameters() const noexcept;
    void processAudio (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool forceBypass);

    juce::AudioProcessorValueTreeState parameters;
    const std::atomic<float>* input = nullptr;
    const std::atomic<float>* threshold = nullptr;
    const std::atomic<float>* attack = nullptr;
    const std::atomic<float>* release = nullptr;
    const std::atomic<float>* makeup = nullptr;
    const std::atomic<float>* scFilter = nullptr;
    const std::atomic<float>* fbff = nullptr;
    const std::atomic<float>* baBypass = nullptr;
    const std::atomic<float>* baIn = nullptr;
    const std::atomic<float>* baOut = nullptr;
    const std::atomic<float>* baLfm = nullptr;
    const std::atomic<float>* baMix = nullptr;
    const std::atomic<float>* baMonitor = nullptr;
    const std::atomic<float>* xfrmOut = nullptr;
    const std::atomic<float>* xfrmAmount = nullptr;
    BaSignalPath signalPath;
    std::unique_ptr<GainLinkController> gainLink;
    std::atomic<float> currentGainReductionDb { 0.0f };
    LevelMeterPeaks levelPeaks;
    TransformerVoicingState transformerVoicing, inputTransformerVoicing;

    static_assert (std::atomic<float>::is_always_lock_free);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
