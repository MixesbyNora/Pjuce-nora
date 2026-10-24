#pragma once

#include <dsp/StereoCompressor.h>

// The compressor alone at the host rate, with the slice of the processor's
// interface the compressor-law tests use. In the plugin the compressor runs
// inside the oversampled transformer chain, where the input transformer blocks
// DC; these tests drive DC steps, so they exercise the compressor directly.
// Parameter plumbing is covered by processor-level tests.
class CompressorUnderTest
{
public:
    void prepareToPlay (double sampleRate, int) { compressor.prepare (sampleRate, values); }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        compressor.setParameters (values);
        reduction = compressor.process (buffer, buffer.getNumChannels());
    }
    // As PluginProcessor: the block's maximum reduction.
    float getGainReductionDb() const noexcept { return reduction; }
    int getLatencySamples() const noexcept { return 0; }
    void set (const juce::String& id, float value)
    {
        if (id == "input") values.inputDb = value;
        else if (id == "threshold") values.thresholdDb = value;
        else if (id == "attack") values.attackMs = value;
        else if (id == "release") values.releaseMs = value;
        else if (id == "makeup") values.makeupDb = value;
        else if (id == "fbff") values.fbffPercent = value;
        else if (id == "scFilter") values.scFilterMode = juce::roundToInt (value);
        else if (id == "calibration") values.calibrationDb = value;
        else if (id == "releaseMod") values.releaseModPercent = value;
        else jassertfalse;
    }

    CompressorParameters::Values values;

private:
    StereoCompressor compressor;
    float reduction = 0.0f;
};
