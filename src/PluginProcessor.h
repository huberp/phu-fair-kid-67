#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

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
    static constexpr const char* kParamInputTrimDb  = "inputTrimDb";
    static constexpr const char* kParamOutputTrimDb = "outputTrimDb";
    static constexpr const char* kParamMix          = "mix";
    static constexpr const char* kParamOversampling = "oversampling";
    static constexpr const char* kParamBypass       = "bypass";

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

  private:
    juce::dsp::Gain<float>       inputGain;
    juce::dsp::Gain<float>       outputGain;
    juce::dsp::DryWetMixer<float> dryWetMixer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhuFairKid67AudioProcessor)
};
