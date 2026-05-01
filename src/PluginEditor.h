#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

class PhuFairKid67AudioProcessorEditor : public juce::AudioProcessorEditor {
  public:
    explicit PhuFairKid67AudioProcessorEditor(PhuFairKid67AudioProcessor&);
    ~PhuFairKid67AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    PhuFairKid67AudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhuFairKid67AudioProcessorEditor)
};
