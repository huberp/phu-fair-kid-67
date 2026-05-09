#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/Utils/OversamplingChain.h"

class PhuFairKid67AudioProcessor : public juce::AudioProcessor {
  public:
    PhuFairKid67AudioProcessor();
    ~PhuFairKid67AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /// Parameter IDs (stable string constants shared with the editor).
    static constexpr const char* kParamInputTrimLeftDb   = "inputTrimLeftDb";
    static constexpr const char* kParamInputTrimRightDb  = "inputTrimRightDb";
    static constexpr const char* kParamOutputTrimLeftDb  = "outputTrimLeftDb";
    static constexpr const char* kParamOutputTrimRightDb = "outputTrimRightDb";
    static constexpr const char* kParamMix               = "mix";
    static constexpr const char* kParamOversampling      = "oversampling";
    static constexpr const char* kParamQuality           = "quality";
    static constexpr const char* kParamBypass            = "bypass";
    static constexpr const char* kParamLinkMode          = "linkMode";
    static constexpr const char* kParamTimingPosition    = "timingPosition";
    static constexpr const char* kParamThresholdLeft     = "thresholdLeft";
    static constexpr const char* kParamThresholdRight    = "thresholdRight";
    static constexpr const char* kParamCathodeBypassUf   = "cathodeBypassUf";
    static constexpr const char* kParamStereoMode        = "stereoMode";
    static constexpr const char* kParamSoloLeft          = "soloLeft";
    static constexpr const char* kParamSoloRight         = "soloRight";
    /// Read-only meter parameters (written by the processor each block).
    static constexpr const char* kParamMeterGainReductionL = "meterGainReductionL";
    static constexpr const char* kParamMeterGainReductionR = "meterGainReductionR";
    static constexpr const char* kParamMeterOutputL        = "meterOutputL";
    static constexpr const char* kParamMeterOutputR        = "meterOutputR";

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /// Read-only access to the latest meter snapshot (safe to call from the
    /// message/editor thread; the values are written atomically each processBlock).
    [[nodiscard]] Models::Fairchild670Meters meterSnapshot() const noexcept {
        return oversamplingChain_.core().meters();
    }

    /// Input peak level in dBFS for the left channel (-60..0).
    /// Written atomically by the audio thread each processBlock; safe to read
    /// from the message thread (e.g. editor timer).
    [[nodiscard]] float getMeterInputLDb() const noexcept { return meterInputLDb_.load(); }

    /// Input peak level in dBFS for the right channel (-60..0).
    [[nodiscard]] float getMeterInputRDb() const noexcept { return meterInputRDb_.load(); }

  private:
    juce::dsp::Gain<float>        inputGainL_;
    juce::dsp::Gain<float>        inputGainR_;
    juce::dsp::Gain<float>        outputGainL_;
    juce::dsp::Gain<float>        outputGainR_;
    // Constructed with 512-sample max latency so the dry-path delay line can
    // compensate for the oversampling FIR latency (up to ~200 samples at 8×).
    juce::dsp::DryWetMixer<float> dryWetMixer{512};

    DSP::OversamplingChain oversamplingChain_;

    /// Input peak levels in dBFS, written atomically each processBlock.
    /// Safe to read from the message thread (e.g. editor timer callback).
    std::atomic<float> meterInputLDb_ { -60.0f };
    std::atomic<float> meterInputRDb_ { -60.0f };

    /// Cached timing position to avoid reconstructing detectors every buffer.
    int lastTimingPosition_   = -1;
    /// Cached oversampling order to detect changes between processBlock calls.
    int lastOversamplingOrder_ = -1;
    /// Cached quality choice to detect changes between processBlock calls.
    int lastQualityChoice_     = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhuFairKid67AudioProcessor)
};
