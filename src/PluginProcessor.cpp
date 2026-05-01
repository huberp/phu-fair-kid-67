#include "PluginProcessor.h"
#include "PluginEditor.h"

PhuFairKid67AudioProcessor::PhuFairKid67AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()) {}

PhuFairKid67AudioProcessor::~PhuFairKid67AudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
PhuFairKid67AudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamInputTrimDb, 1}, "Input Trim",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamOutputTrimDb, 1}, "Output Trim",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamMix, 1}, "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kParamOversampling, 1}, "Oversampling",
        juce::StringArray{"1x", "2x", "4x", "8x"}, 0));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kParamBypass, 1}, "Bypass", false));

    return layout;
}

const juce::String PhuFairKid67AudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool PhuFairKid67AudioProcessor::acceptsMidi() const { return false; }
bool PhuFairKid67AudioProcessor::producesMidi() const { return false; }
bool PhuFairKid67AudioProcessor::isMidiEffect() const { return false; }
double PhuFairKid67AudioProcessor::getTailLengthSeconds() const { return 0.0; }

int PhuFairKid67AudioProcessor::getNumPrograms() { return 1; }
int PhuFairKid67AudioProcessor::getCurrentProgram() { return 0; }
void PhuFairKid67AudioProcessor::setCurrentProgram(int) {}
const juce::String PhuFairKid67AudioProcessor::getProgramName(int) { return {}; }
void PhuFairKid67AudioProcessor::changeProgramName(int, const juce::String&) {}

void PhuFairKid67AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const auto numChannels = static_cast<juce::uint32>(getTotalNumInputChannels());
    juce::dsp::ProcessSpec spec{sampleRate,
                                static_cast<juce::uint32>(samplesPerBlock),
                                numChannels};

    inputGain.prepare(spec);
    inputGain.setRampDurationSeconds(0.005);

    outputGain.prepare(spec);
    outputGain.setRampDurationSeconds(0.005);

    dryWetMixer.prepare(spec);
}

void PhuFairKid67AudioProcessor::releaseResources() {}

bool PhuFairKid67AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}

void PhuFairKid67AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const bool isBypassed =
        apvts.getRawParameterValue(kParamBypass)->load() > 0.5f;
    const float mixValue =
        isBypassed ? 0.0f : apvts.getRawParameterValue(kParamMix)->load();

    inputGain.setGainDecibels(
        apvts.getRawParameterValue(kParamInputTrimDb)->load());
    outputGain.setGainDecibels(
        apvts.getRawParameterValue(kParamOutputTrimDb)->load());
    dryWetMixer.setWetMixProportion(mixValue);

    juce::dsp::AudioBlock<float> block(buffer);
    dryWetMixer.pushDrySamples(block);

    juce::dsp::ProcessContextReplacing<float> context(block);
    inputGain.process(context);
    // DSP placeholder — analog model will be inserted here
    outputGain.process(context);

    dryWetMixer.mixWetSamples(block);
}

bool PhuFairKid67AudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* PhuFairKid67AudioProcessor::createEditor() {
    return new PhuFairKid67AudioProcessorEditor(*this);
}

void PhuFairKid67AudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PhuFairKid67AudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PhuFairKid67AudioProcessor();
}

