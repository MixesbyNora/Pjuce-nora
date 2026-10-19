#include "GainLinkController.h"

GainLinkController::GainLinkController (juce::AudioProcessorValueTreeState& state)
    : parameters (state),
      inParameter (state.getParameter (BaParameters::inGain.id)),
      outParameter (state.getParameter (BaParameters::outGain.id)),
      inValue (state.getRawParameterValue (BaParameters::inGain.id)),
      outValue (state.getRawParameterValue (BaParameters::outGain.id)),
      linked (state.getRawParameterValue (BaParameters::ioLink.id)),
      lastIn (inValue->load()), lastOut (outValue->load())
{
    for (const auto* id : { BaParameters::inGain.id, BaParameters::outGain.id, BaParameters::ioLink.id })
        parameters.addParameterListener (id, this);
    startTimerHz (60);
}

GainLinkController::~GainLinkController()
{
    stopTimer();
    for (const auto* id : { BaParameters::inGain.id, BaParameters::outGain.id, BaParameters::ioLink.id })
        parameters.removeParameterListener (id, this);
}

void GainLinkController::parameterChanged (const juce::String& id, float value)
{
    if (id == BaParameters::ioLink.id)
    {
        if (value < 0.5f)
        {
            pendingIn.store (0.0f, std::memory_order_relaxed);
            pendingOut.store (0.0f, std::memory_order_relaxed);
        }
        return;
    }
    const auto isInput = id == BaParameters::inGain.id;
    auto& previous = isInput ? lastIn : lastOut;
    const auto delta = value - previous.exchange (value, std::memory_order_relaxed);
    if (restoring.load (std::memory_order_relaxed))
        return;
    // Ignore our own synchronous notification, without suppressing a concurrent
    // host change to a different value or parameter.
    if (movingPartner.load (std::memory_order_acquire)
        && isInput == partnerIsInput.load (std::memory_order_relaxed)
        && std::abs (value - partnerTarget.load (std::memory_order_relaxed)) < 0.001f)
        return;
    if (linked->load (std::memory_order_relaxed) >= 0.5f)
        (isInput ? pendingIn : pendingOut).fetch_add (delta, std::memory_order_relaxed);
}

void GainLinkController::movePartner (bool isInput, float targetDb)
{
    auto* parameter = isInput ? inParameter : outParameter;
    const auto current = (isInput ? inValue : outValue)->load (std::memory_order_relaxed);
    if (std::abs (current - targetDb) < 0.001f)
        return;
    partnerIsInput.store (isInput, std::memory_order_relaxed);
    partnerTarget.store (targetDb, std::memory_order_relaxed);
    movingPartner.store (true, std::memory_order_release);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (targetDb));
    parameter->endChangeGesture();
    movingPartner.store (false, std::memory_order_release);
}

void GainLinkController::timerCallback()
{
    if (updating || restoring.load (std::memory_order_relaxed))
        return;
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto inputDelta = pendingIn.exchange (0.0f, std::memory_order_relaxed);
    const auto outputDelta = pendingOut.exchange (0.0f, std::memory_order_relaxed);
    if (linked->load (std::memory_order_relaxed) < 0.5f)
        return;
    const auto newInput = GainLink::compensate (inValue->load (std::memory_order_relaxed), outputDelta,
        true, BaParameters::inGain.minimum, BaParameters::inGain.maximum);
    const auto newOutput = GainLink::compensate (outValue->load (std::memory_order_relaxed), inputDelta,
        true, BaParameters::outGain.minimum, BaParameters::outGain.maximum);
    movePartner (true, newInput);
    movePartner (false, newOutput);
}

void GainLinkController::beginStateRestore() noexcept
{
    restoring.store (true, std::memory_order_relaxed);
    pendingIn.store (0.0f, std::memory_order_relaxed);
    pendingOut.store (0.0f, std::memory_order_relaxed);
}

void GainLinkController::endStateRestore() noexcept
{
    lastIn.store (inValue->load (std::memory_order_relaxed), std::memory_order_relaxed);
    lastOut.store (outValue->load (std::memory_order_relaxed), std::memory_order_relaxed);
    pendingIn.store (0.0f, std::memory_order_relaxed);
    pendingOut.store (0.0f, std::memory_order_relaxed);
    restoring.store (false, std::memory_order_relaxed);
}
