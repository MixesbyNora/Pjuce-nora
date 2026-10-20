#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PluginProcessor::PluginProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       parameters (*this, nullptr, "PARAMETERS", createParameterLayout()),
       input (parameters.getRawParameterValue (CompressorParameters::input.id)),
       threshold (parameters.getRawParameterValue (CompressorParameters::threshold.id)),
       attack (parameters.getRawParameterValue (CompressorParameters::attack.id)),
       release (parameters.getRawParameterValue (CompressorParameters::release.id)),
       makeup (parameters.getRawParameterValue (CompressorParameters::makeup.id)),
       scFilter (parameters.getRawParameterValue (CompressorParameters::scFilterId)),
       fbff (parameters.getRawParameterValue (CompressorParameters::fbff.id)),
       baBypass (parameters.getRawParameterValue (BaParameters::bypass.id)),
       baIn (parameters.getRawParameterValue (BaParameters::inGain.id)),
       baOut (parameters.getRawParameterValue (BaParameters::outGain.id)),
       baLfm (parameters.getRawParameterValue (BaParameters::lfm.id)),
       baMix (parameters.getRawParameterValue (BaParameters::mix.id)),
       baMonitor (parameters.getRawParameterValue (BaParameters::monitorId)),
       xfrmOut (parameters.getRawParameterValue (BaParameters::xfrmOut.id)),
       xfrmAmount (parameters.getRawParameterValue (BaParameters::xfrmAmount.id))
{
    gainLink = std::make_unique<GainLinkController> (parameters);
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& spec : CompressorParameters::specifications)
    {
        const auto decimals = spec.step < 0.1f ? 2 : spec.step < 1.0f ? 1 : 0;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { spec.id, 1 }, spec.name,
            juce::NormalisableRange<float> { spec.minimum, spec.maximum, spec.step },
            spec.defaultValue, juce::AudioParameterFloatAttributes().withLabel (spec.unit)
                .withStringFromValueFunction ([decimals] (float value, int)
                {
                    if (std::abs (value) < 0.5f * std::pow (10.0f, -static_cast<float> (decimals)))
                        value = 0.0f; // Avoid a display-only "-0.00" after normalized round-trips.
                    return juce::String (value, decimals);
                })));
    }
    juce::StringArray filterChoices;
    for (const auto& mode : SidechainFilterSettings::modes)
        filterChoices.add (mode.name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { CompressorParameters::scFilterId, 1 }, "SC Filter",
        filterChoices, SidechainFilterSettings::defaultMode));
    BaParameters::appendLayout (layout);
    const auto& blend = CompressorParameters::fbff;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { blend.id, 2 }, blend.name,
        juce::NormalisableRange<float> { blend.minimum, blend.maximum, blend.step },
        blend.defaultValue, juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int)
            {
                const auto tenths = juce::roundToInt (value * 10.0f);
                if (tenths == 0)
                    return juce::String ("FB");
                if (tenths == 500)
                    return juce::String ("50/50");
                if (tenths == 1000)
                    return juce::String ("FF");
                return juce::String (value, tenths % 10 == 0 ? 0 : 1) + "% FF";
            })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                const auto trimmed = text.trim();
                if (trimmed.equalsIgnoreCase ("FB"))
                    return 0.0f;
                if (trimmed.equalsIgnoreCase ("FF"))
                    return 100.0f;
                if (trimmed == "50/50")
                    return 50.0f;
                return trimmed.getFloatValue();
            })));
    // New AU version hints and appending preserve existing host ordering.
    const auto& out = BaParameters::xfrmOut;
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { out.id, 3 }, out.name, out.initial,
        juce::AudioParameterBoolAttributes()
            .withStringFromValueFunction ([] (bool value, int) { return juce::String (value ? "Out" : "In"); })
            .withValueFromStringFunction ([] (const juce::String& text)
            {
                return text.trim().equalsIgnoreCase ("Out") || text.getIntValue() != 0;
            })));
    const auto& amount = BaParameters::xfrmAmount;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { amount.id, 3 }, amount.name,
        juce::NormalisableRange<float> { amount.minimum, amount.maximum, amount.step }, amount.initial,
        juce::AudioParameterFloatAttributes().withLabel (amount.unit)
            .withStringFromValueFunction ([] (float value, int) { return juce::String (value, 1); })
            .withValueFromStringFunction ([] (const juce::String& text) { return text.getFloatValue(); })));
    return layout;
}

BaParameters::Values PluginProcessor::readBaParameters() const noexcept
{
    return { baIn->load (std::memory_order_relaxed), baOut->load (std::memory_order_relaxed),
        baLfm->load (std::memory_order_relaxed), baMix->load (std::memory_order_relaxed),
        baBypass->load (std::memory_order_relaxed) >= 0.5f,
        juce::roundToInt (baMonitor->load (std::memory_order_relaxed)),
        xfrmOut->load (std::memory_order_relaxed) >= 0.5f,
        xfrmAmount->load (std::memory_order_relaxed) };
}

juce::AudioProcessorParameter* PluginProcessor::getBypassParameter() const
{
    return parameters.getParameter (BaParameters::bypass.id);
}

CompressorParameters::Values PluginProcessor::readParameters() const noexcept
{
    return { input->load (std::memory_order_relaxed),
        threshold->load (std::memory_order_relaxed),
        attack->load (std::memory_order_relaxed),
        release->load (std::memory_order_relaxed),
        makeup->load (std::memory_order_relaxed),
        juce::roundToInt (scFilter->load (std::memory_order_relaxed)),
        fbff->load (std::memory_order_relaxed) };
}

PluginProcessor::~PluginProcessor()
{
}

//==============================================================================
const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PluginProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double PluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PluginProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int PluginProcessor::getCurrentProgram()
{
    return 0;
}

void PluginProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PluginProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    // Steinberg's VST3 validator fails plugins whose single default program has no name
    return "Default";
}

void PluginProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    signalPath.prepare (sampleRate, samplesPerBlock, readParameters(), readBaParameters());
    currentGainReductionDb.store (0.0f, std::memory_order_relaxed);
    levelPeaks.reset();
}

void PluginProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    processAudio (buffer, midiMessages, false);
}

void PluginProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    processAudio (buffer, midiMessages, true);
}

void PluginProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, bool forceBypass)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear output channels without a corresponding input.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    auto baValues = readBaParameters();
    baValues.bypassed = baValues.bypassed || forceBypass;
    const auto blockPeakDb = signalPath.process (buffer, totalNumInputChannels, readParameters(), baValues, &levelPeaks);
    currentGainReductionDb.store (blockPeakDb, std::memory_order_relaxed);
}

//==============================================================================
bool PluginProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (parameters.state.getType()))
        {
            gainLink->beginStateRestore();
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            gainLink->endStateRestore();
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
