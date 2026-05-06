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
        juce::ParameterID{kParamInputTrimLeftDb, 1}, "Input Trim Left",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamInputTrimRightDb, 1}, "Input Trim Right",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamOutputTrimLeftDb, 1}, "Output Trim Left",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamOutputTrimRightDb, 1}, "Output Trim Right",
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
        juce::StringArray{"Independent", "Linked Max", "Linked Avg", "Mid/Side (Vert/Lat)"}, 1));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kParamTimingPosition, 1}, "Timing",
        juce::StringArray{"1", "2", "3", "4", "5", "6"}, 0));

    // Threshold per channel: 0 = no compression (threshold above any possible signal),
    // 10 = maximum sensitivity (threshold at 0 V, always compressing).
    // Maps to thresholdVoltage = (10 - param) volts, matching the detector's
    // 0–10 V full-scale output range.  Default 5 ≈ -6 dBFS onset.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamThresholdLeft, 1}, "Threshold Left",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 5.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamThresholdRight, 1}, "Threshold Right",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 5.0f));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{kParamStereoMode, 1}, "Stereo Mode",
        juce::StringArray{"Stereo", "Mid/Side"}, 0));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kParamSoloLeft, 1}, "Solo Left", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{kParamSoloRight, 1}, "Solo Right", false));

    // Read-only meter parameters: floored at -60 dB, displayed by the editor.
    // The processor writes these each block; they should not be automated.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamMeterGainReductionL, 1}, "GR Meter Left",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -60.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamMeterGainReductionR, 1}, "GR Meter Right",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -60.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamMeterOutputL, 1}, "Output Meter Left",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -60.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dBFS")));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{kParamMeterOutputR, 1}, "Output Meter Right",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -60.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dBFS")));

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

    inputGainL_.prepare(spec);
    inputGainL_.setRampDurationSeconds(0.005);
    inputGainR_.prepare(spec);
    inputGainR_.setRampDurationSeconds(0.005);

    outputGainL_.prepare(spec);
    outputGainL_.setRampDurationSeconds(0.005);
    outputGainR_.prepare(spec);
    outputGainR_.setRampDurationSeconds(0.005);

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

    // Tell the dry/wet mixer how many samples the wet path is delayed so it
    // can delay the dry path by the same amount — prevents comb filtering
    // at any mix value below 1.0 when oversampling is active.
    dryWetMixer.setWetLatency(static_cast<float>(oversamplingChain_.getLatencySamples()));

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

    inputGainL_.setGainDecibels(
        apvts.getRawParameterValue(kParamInputTrimLeftDb)->load());
    inputGainR_.setGainDecibels(
        apvts.getRawParameterValue(kParamInputTrimRightDb)->load());
    outputGainL_.setGainDecibels(
        apvts.getRawParameterValue(kParamOutputTrimLeftDb)->load());
    outputGainR_.setGainDecibels(
        apvts.getRawParameterValue(kParamOutputTrimRightDb)->load());
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
            dryWetMixer.setWetLatency(static_cast<float>(oversamplingChain_.getLatencySamples()));
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
            case 2: // Linked Avg
                oversamplingChain_.core().setLinkMode(Models::LinkMode::Linked);
                oversamplingChain_.core().setEnvelopeStrategy(Models::LinkedEnvelopeStrategy::Average);
                break;
            default: // 3 — Mid/Side (Vert/Lat)
                oversamplingChain_.core().setLinkMode(Models::LinkMode::MidSide);
                break;
        }

        const int timingChoice =
            static_cast<int>(apvts.getRawParameterValue(kParamTimingPosition)->load());
        if (timingChoice != lastTimingPosition_) {
            lastTimingPosition_ = timingChoice;
            oversamplingChain_.core().setTimingPosition(
                static_cast<Models::Sidechain::TimingPosition>(timingChoice));
        }

        // Per-channel threshold: convert the 0–10 dial value to a threshold voltage.
        // thresholdVoltage = (10 − param); at param=0 the threshold is 10 V
        // (above any FS signal = no compression), at param=10 it is 0 V.
        const float thresholdLeft =
            apvts.getRawParameterValue(kParamThresholdLeft)->load();
        const float thresholdRight =
            apvts.getRawParameterValue(kParamThresholdRight)->load();
        oversamplingChain_.core().setThresholdLeft(10.0f - thresholdLeft);
        oversamplingChain_.core().setThresholdRight(10.0f - thresholdRight);
    }

    juce::dsp::AudioBlock<float> block(buffer);
    dryWetMixer.pushDrySamples(block);

    // ── Per-channel input trim ────────────────────────────────────────────────
    {
        auto leftBlock  = block.getSingleChannelBlock(0);
        auto rightBlock = block.getSingleChannelBlock(1);
        juce::dsp::ProcessContextReplacing<float> ctxL(leftBlock);
        juce::dsp::ProcessContextReplacing<float> ctxR(rightBlock);
        inputGainL_.process(ctxL);
        inputGainR_.process(ctxR);
    }

    // ── Stereo Mode: optionally convert L/R → M/S before compression ─────────
    // Encoding uses a 0.5 factor: M = (L+R)/2, S = (L-R)/2.
    // This keeps the intermediate M/S signals within the ±1.0 normalised range
    // even when L and R are both at full scale (e.g. a mono signal).
    // The decode step (M+S → L, M-S → R) restores unity gain end-to-end.
    const bool isMidSideMode =
        static_cast<int>(apvts.getRawParameterValue(kParamStereoMode)->load()) == 1;
    if (isMidSideMode && buffer.getNumChannels() >= 2) {
        auto* dataL = buffer.getWritePointer(0);
        auto* dataR = buffer.getWritePointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float m = (dataL[i] + dataR[i]) * 0.5f;
            const float s = (dataL[i] - dataR[i]) * 0.5f;
            dataL[i] = m;
            dataR[i] = s;
        }
    }

    // Process through the Fairchild 670 core with optional oversampling.
    oversamplingChain_.core().resetPeakMeters();
    oversamplingChain_.process(buffer);

    // ── Stereo Mode: convert M/S → L/R after compression ─────────────────────
    if (isMidSideMode && buffer.getNumChannels() >= 2) {
        auto* dataM = buffer.getWritePointer(0);
        auto* dataS = buffer.getWritePointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float l = dataM[i] + dataS[i];
            const float r = dataM[i] - dataS[i];
            dataM[i] = l;
            dataS[i] = r;
        }
    }

    // ── Per-channel output trim ───────────────────────────────────────────────
    {
        auto leftBlock  = block.getSingleChannelBlock(0);
        auto rightBlock = block.getSingleChannelBlock(1);
        juce::dsp::ProcessContextReplacing<float> ctxL(leftBlock);
        juce::dsp::ProcessContextReplacing<float> ctxR(rightBlock);
        outputGainL_.process(ctxL);
        outputGainR_.process(ctxR);
    }

    // ── Solo ─────────────────────────────────────────────────────────────────
    {
        const bool soloLeft  = apvts.getRawParameterValue(kParamSoloLeft)->load()  > 0.5f;
        const bool soloRight = apvts.getRawParameterValue(kParamSoloRight)->load() > 0.5f;
        if (soloLeft && !soloRight) {
            // Mute right, pass only left.
            buffer.clear(1, 0, buffer.getNumSamples());
        } else if (soloRight && !soloLeft) {
            // Mute left, pass only right.
            buffer.clear(0, 0, buffer.getNumSamples());
        }
    }

    dryWetMixer.mixWetSamples(block);

    // ── Update read-only meter parameters ────────────────────────────────────
    {
        const auto m = oversamplingChain_.core().meters();

        // Gain reduction: inPeak / outPeak in dB (negative = compression).
        constexpr float kFloorDb = -60.0f;
        auto peakToDb = [kFloorDb](float peak) -> float {
            return peak > 0.0f ? std::max(kFloorDb, juce::Decibels::gainToDecibels(peak))
                               : kFloorDb;
        };
        auto grDb = [&](float inPeak, float outPeak) -> float {
            if (inPeak <= 0.0f || outPeak <= 0.0f) return kFloorDb;
            return std::max(kFloorDb, std::min(0.0f, peakToDb(outPeak) - peakToDb(inPeak)));
        };

        apvts.getRawParameterValue(kParamMeterGainReductionL)->store(grDb(m.inPeakL, m.outPeakL));
        apvts.getRawParameterValue(kParamMeterGainReductionR)->store(grDb(m.inPeakR, m.outPeakR));
        apvts.getRawParameterValue(kParamMeterOutputL)->store(peakToDb(m.outPeakL));
        apvts.getRawParameterValue(kParamMeterOutputR)->store(peakToDb(m.outPeakR));
    }
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

