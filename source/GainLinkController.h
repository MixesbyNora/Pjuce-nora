#pragma once

#include "BaParameters.h"
#include "GainLink.h"

// Parameter callbacks only capture deltas in lock-free atomics. The message-
// thread timer moves linked parameters and sends host automation gestures.
class GainLinkController : private juce::AudioProcessorValueTreeState::Listener, private juce::Timer
{
public:
    explicit GainLinkController (juce::AudioProcessorValueTreeState& state);
    ~GainLinkController() override;
    void beginStateRestore() noexcept;
    void endStateRestore() noexcept;

private:
    void parameterChanged (const juce::String& id, float value) override;
    void timerCallback() override;
    void movePartner (bool isInput, float targetDb);

    juce::AudioProcessorValueTreeState& parameters;
    juce::RangedAudioParameter* inParameter;
    juce::RangedAudioParameter* outParameter;
    const std::atomic<float>* inValue;
    const std::atomic<float>* outValue;
    const std::atomic<float>* linked;
    std::atomic<float> lastIn, lastOut;
    std::atomic<float> pendingIn { 0.0f }, pendingOut { 0.0f };
    std::atomic<bool> movingPartner { false }, partnerIsInput { false };
    std::atomic<float> partnerTarget { 0.0f };
    std::atomic<bool> restoring { false };
    bool updating = false;
};
