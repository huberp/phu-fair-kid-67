#include "PluginProcessor.h"
#include "PluginEditor.h"

PhuFairKid67AudioProcessor::PhuFairKid67AudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

PhuFairKid67AudioProcessor::~PhuFairKid67AudioProcessor() {}

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

void PhuFairKid67AudioProcessor::prepareToPlay(double, int) {}
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
    // Pass-through: no processing yet
    juce::ignoreUnused(buffer);
}

bool PhuFairKid67AudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* PhuFairKid67AudioProcessor::createEditor() {
    return new PhuFairKid67AudioProcessorEditor(*this);
}

void PhuFairKid67AudioProcessor::getStateInformation(juce::MemoryBlock&) {}
void PhuFairKid67AudioProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PhuFairKid67AudioProcessor();
}
