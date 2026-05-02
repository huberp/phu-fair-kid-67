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

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kParamQuality, 1}, "Quality",
        juce::StringArray{"Draft", "High"}, 1));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kParamBypass, 1}, "Bypass", false));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kParamLinkMode, 1}, "Link Mode",
        juce::StringArray{"Independent", "Linked Max", "Linked Avg"}, 1));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kParamTimingPosition, 1}, "Timing",
        juce::StringArray{"1", "2", "3", "4", "5", "6"}, 0));

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

    // Apply oversampling and quality settings from parameters.
    const int osOrder =
        static_cast<int>(apvts.getRawParameterValue(kParamOversampling)->load());
    const int qualityChoice =
        static_cast<int>(apvts.getRawParameterValue(kParamQuality)->load());

    oversamplingChain_.setOversamplingOrder(osOrder);
    oversamplingChain_.setQuality(qualityChoice == 0
                                      ? Models::ProcessingQuality::Draft
                                      : Models::ProcessingQuality::High);
    oversamplingChain_.prepare(sampleRate, samplesPerBlock);

    // Report oversampling filter latency to the host so it can compensate.
    setLatencySamples(oversamplingChain_.getLatencySamples());

    lastTimingPosition_    = -1; // force reconfiguration on next processBlock
    lastOversamplingOrder_ = osOrder;
    lastQualityChoice_     = qualityChoice;
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

    // ── Detect oversampling / quality parameter changes ───────────────────────
    //
    // A change triggers a safe reinit.  This involves memory allocation
    // (juce::dsp::Oversampling::initProcessing) and a momentary glitch, which
    // is acceptable — the host is expected to mute during transport operations
    // that change DSP configuration.  A short output ramp from the
    // outputGain smoothed ramp mitigates the click on the wet path.
    {
        const int currentOsOrder =
            static_cast<int>(apvts.getRawParameterValue(kParamOversampling)->load());
        const int currentQuality =
            static_cast<int>(apvts.getRawParameterValue(kParamQuality)->load());

        if (currentOsOrder != lastOversamplingOrder_) {
            lastOversamplingOrder_ = currentOsOrder;
            oversamplingChain_.setOversamplingOrder(currentOsOrder);
            oversamplingChain_.prepare(getSampleRate(), buffer.getNumSamples());
            setLatencySamples(oversamplingChain_.getLatencySamples());
        }

        if (currentQuality != lastQualityChoice_) {
            lastQualityChoice_ = currentQuality;
            oversamplingChain_.setQuality(currentQuality == 0
                                              ? Models::ProcessingQuality::Draft
                                              : Models::ProcessingQuality::High);
        }
    }

    // Update link mode and timing position from parameters.
    {
        const int linkChoice =
            static_cast<int>(apvts.getRawParameterValue(kParamLinkMode)->load());
        switch (linkChoice) {
            case 0:
                oversamplingChain_.core().setLinkMode(Models::LinkMode::Independent);
                break;
            case 1:
                oversamplingChain_.core().setLinkMode(Models::LinkMode::Linked);
                oversamplingChain_.core().setEnvelopeStrategy(Models::LinkedEnvelopeStrategy::Max);
                break;
            default: // 2 — Linked Avg
                oversamplingChain_.core().setLinkMode(Models::LinkMode::Linked);
                oversamplingChain_.core().setEnvelopeStrategy(Models::LinkedEnvelopeStrategy::Average);
                break;
        }

        const int timingChoice =
            static_cast<int>(apvts.getRawParameterValue(kParamTimingPosition)->load());
        if (timingChoice != lastTimingPosition_) {
            lastTimingPosition_ = timingChoice;
            oversamplingChain_.core().setTimingPosition(
                static_cast<Models::Sidechain::TimingPosition>(timingChoice));
        }
    }

    juce::dsp::AudioBlock<float> block(buffer);
    dryWetMixer.pushDrySamples(block);

    juce::dsp::ProcessContextReplacing<float> context(block);
    inputGain.process(context);

    // Process through the Fairchild 670 core with optional oversampling.
    oversamplingChain_.core().resetPeakMeters();
    oversamplingChain_.process(buffer);

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

