#include "PluginEditor.h"

PhuFairKid67AudioProcessorEditor::PhuFairKid67AudioProcessorEditor(
    PhuFairKid67AudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
    setSize(400, 300);
}

PhuFairKid67AudioProcessorEditor::~PhuFairKid67AudioProcessorEditor() {}

void PhuFairKid67AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(15.0f));
    g.drawFittedText("PHU FAIR KID 67", getLocalBounds(), juce::Justification::centred, 1);
}

void PhuFairKid67AudioProcessorEditor::resized() {}
