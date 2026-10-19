#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/StereoCompressor.h"

class PluginProcessor : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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

    // Positive dB of attenuation; safe for future message-thread meters to poll.
    float getGainReductionDb() const noexcept
    {
        return currentGainReductionDb.load (std::memory_order_relaxed);
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    CompressorParameters::Values readParameters() const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    const std::atomic<float>* input = nullptr;
    const std::atomic<float>* threshold = nullptr;
    const std::atomic<float>* attack = nullptr;
    const std::atomic<float>* release = nullptr;
    const std::atomic<float>* makeup = nullptr;
    StereoCompressor compressor;
    std::atomic<float> currentGainReductionDb { 0.0f };

    static_assert (std::atomic<float>::is_always_lock_free);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
