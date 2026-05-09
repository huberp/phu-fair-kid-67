#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Layout and style constants
// All geometry and font sizes are defined here; no bare literals appear in the
// layout functions below.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// ── Window ───────────────────────────────────────────────────────────────────
constexpr int kWindowWidth  = 740;
constexpr int kWindowHeight = 530;

// ── Spacing ──────────────────────────────────────────────────────────────────
constexpr int kOuterPad   = 10; ///< Outer margin on all sides
constexpr int kSectionGap =  8; ///< Gap between major sections / columns
constexpr int kItemGap    =  4; ///< Gap between items inside a section

// ── Top bar ───────────────────────────────────────────────────────────────────
constexpr int kTopBarH       = 28; ///< Height of the title / status row
constexpr int kTopTitleW     = 150; ///< Width for the plugin title label
constexpr int kTopBypassW    =  70; ///< Width of the bypass toggle button
constexpr int kTopComboLabelW =  76; ///< Width of label next to a top-bar combo
constexpr int kTopComboW      =  62; ///< Width of a top-bar combo box

// ── Column controls ───────────────────────────────────────────────────────────
constexpr int kGroupLabelH   = 16; ///< Section/group header label height
constexpr int kControlLabelH = 14; ///< Label above each knob or combo
constexpr int kKnobSize      = 52; ///< Rotary slider square side length
constexpr int kTextBoxH      = 14; ///< Slider text-box height below the knob
constexpr int kComboH        = 22; ///< Combo box height
constexpr int kButtonH       = 26; ///< Toggle button height

// ── Meters ────────────────────────────────────────────────────────────────────
constexpr int kMeterLabelW  = 52; ///< Width of the row label column
constexpr int kMeterBarH    = 14; ///< Height of one meter bar
constexpr int kMeterNumRows =  6; ///< Rows: in L, in R, GR L, GR R, out L, out R
/// Total height of the meter section, including the section header.
constexpr int kMeterSectionH =
    kGroupLabelH + kItemGap + kMeterNumRows * (kMeterBarH + kItemGap);

// ── Font sizes (all labels use one of these three values) ─────────────────────
constexpr float kFontTitle   = 14.0f; ///< Plugin title
constexpr float kFontGroup   = 10.5f; ///< Section group header
constexpr float kFontControl = 10.5f; ///< Control labels and top-bar labels
constexpr float kFontStatus  = 10.0f; ///< Status / readout labels (latency etc.)

// ── Colours ───────────────────────────────────────────────────────────────────
const juce::Colour kColBackground { 0xFF28282E };
const juce::Colour kColPanel      { 0xFF32323C };
const juce::Colour kColGroupLabel { 0xFFCCAA44 };
const juce::Colour kColText       { 0xFFCCCCCC };
const juce::Colour kColDimText    { 0xFF888888 };

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

PhuFairKid67AudioProcessorEditor::PhuFairKid67AudioProcessorEditor(
    PhuFairKid67AudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p) {

    // Helper: configure a section-group label (amber, slightly bold weight).
    auto setupGroupLabel = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(kFontGroup).withStyle("Bold")));
        l.setColour(juce::Label::textColourId, kColGroupLabel);
        l.setJustificationType(juce::Justification::centredLeft);
    };

    // Helper: configure a plain control label (light grey, consistent size).
    auto setupLabel = [](juce::Label& l, const juce::String& text,
                         juce::Justification just = juce::Justification::centred) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(kFontControl)));
        l.setColour(juce::Label::textColourId, kColText);
        l.setJustificationType(just);
    };

    // Helper: configure a rotary slider (vertical drag, text box below).
    auto setupRotary = [](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kKnobSize, kTextBoxH);
    };

    // ── Top bar ───────────────────────────────────────────────────────────────

    titleLabel_.setText("PHU FAIR KID 67", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(kFontTitle).withStyle("Bold")));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    bypassButton_.setButtonText("Bypass");
    addAndMakeVisible(bypassButton_);
    bypassAttach_ = std::make_unique<ButtonAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamBypass, bypassButton_);

    setupLabel(oversamplingLabel_, "Oversample", juce::Justification::centredRight);
    addAndMakeVisible(oversamplingLabel_);

    oversamplingBox_.addItem("1x", 1);
    oversamplingBox_.addItem("2x", 2);
    oversamplingBox_.addItem("4x", 3);
    oversamplingBox_.addItem("8x", 4);
    addAndMakeVisible(oversamplingBox_);
    oversamplingAttach_ = std::make_unique<ComboAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamOversampling, oversamplingBox_);

    setupLabel(qualityLabel_, "Quality", juce::Justification::centredRight);
    addAndMakeVisible(qualityLabel_);

    qualityBox_.addItem("Draft", 1);
    qualityBox_.addItem("High",  2);
    addAndMakeVisible(qualityBox_);
    qualityAttach_ = std::make_unique<ComboAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamQuality, qualityBox_);

    latencyLabel_.setFont(juce::Font(juce::FontOptions(kFontStatus)));
    latencyLabel_.setColour(juce::Label::textColourId, kColDimText);
    latencyLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(latencyLabel_);

    // ── Left column: Input / Output trims + Mix ───────────────────────────────

    setupGroupLabel(inputGroupLabel_, "INPUT");
    addAndMakeVisible(inputGroupLabel_);

    setupRotary(inputTrimLSlider_);
    setupRotary(inputTrimRSlider_);
    // Labels will be set in timerCallback() to "L"/"R" or "Mid"/"Side" for M/S mode.
    setupLabel(inputTrimLLabel_, "L");
    setupLabel(inputTrimRLabel_, "R");
    addAndMakeVisible(inputTrimLSlider_);
    addAndMakeVisible(inputTrimRSlider_);
    addAndMakeVisible(inputTrimLLabel_);
    addAndMakeVisible(inputTrimRLabel_);
    inputTrimLAttach_ = std::make_unique<SliderAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamInputTrimLeftDb,  inputTrimLSlider_);
    inputTrimRAttach_ = std::make_unique<SliderAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamInputTrimRightDb, inputTrimRSlider_);

    setupGroupLabel(outputGroupLabel_, "OUTPUT");
    addAndMakeVisible(outputGroupLabel_);

    setupRotary(outputTrimLSlider_);
    setupRotary(outputTrimRSlider_);
    setupLabel(outputTrimLLabel_, "L");
    setupLabel(outputTrimRLabel_, "R");
    addAndMakeVisible(outputTrimLSlider_);
    addAndMakeVisible(outputTrimRSlider_);
    addAndMakeVisible(outputTrimLLabel_);
    addAndMakeVisible(outputTrimRLabel_);
    outputTrimLAttach_ = std::make_unique<SliderAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamOutputTrimLeftDb,  outputTrimLSlider_);
    outputTrimRAttach_ = std::make_unique<SliderAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamOutputTrimRightDb, outputTrimRSlider_);

    setupLabel(mixLabel_, "Mix");
    addAndMakeVisible(mixLabel_);
    setupRotary(mixSlider_);
    addAndMakeVisible(mixSlider_);
    mixAttach_ = std::make_unique<SliderAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamMix, mixSlider_);

    // ── Center column: Compression ────────────────────────────────────────────

    setupGroupLabel(compressionGroupLabel_, "COMPRESSION");
    addAndMakeVisible(compressionGroupLabel_);

    setupRotary(thresholdLSlider_);
    setupRotary(thresholdRSlider_);
    setupLabel(thresholdLLabel_, "Threshold L");
    setupLabel(thresholdRLabel_, "Threshold R");
    addAndMakeVisible(thresholdLSlider_);
    addAndMakeVisible(thresholdRSlider_);
    addAndMakeVisible(thresholdLLabel_);
    addAndMakeVisible(thresholdRLabel_);
    thresholdLAttach_ = std::make_unique<SliderAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamThresholdLeft,  thresholdLSlider_);
    thresholdRAttach_ = std::make_unique<SliderAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamThresholdRight, thresholdRSlider_);

    setupLabel(timingLabel_, "Timing");
    addAndMakeVisible(timingLabel_);
    for (int i = 1; i <= 6; ++i)
        timingBox_.addItem(juce::String(i), i);
    addAndMakeVisible(timingBox_);
    timingAttach_ = std::make_unique<ComboAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamTimingPosition, timingBox_);

    setupLabel(linkModeLabel_, "Link Mode");
    addAndMakeVisible(linkModeLabel_);
    linkModeBox_.addItem("Independent",  1);
    linkModeBox_.addItem("Linked Max",   2);
    linkModeBox_.addItem("Linked Avg",   3);
    linkModeBox_.addItem("Mid/Side",     4);
    addAndMakeVisible(linkModeBox_);
    linkModeAttach_ = std::make_unique<ComboAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamLinkMode, linkModeBox_);

    setupLabel(ckLabel_, "Ck");
    addAndMakeVisible(ckLabel_);
    setupRotary(ckSlider_);
    addAndMakeVisible(ckSlider_);
    ckAttach_ = std::make_unique<SliderAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamCathodeBypassUf, ckSlider_);

    // ── Right column: Stereo tools ────────────────────────────────────────────

    setupGroupLabel(stereoGroupLabel_, "STEREO");
    addAndMakeVisible(stereoGroupLabel_);

    setupLabel(stereoModeLabel_, "Mode");
    addAndMakeVisible(stereoModeLabel_);
    stereoModeBox_.addItem("Stereo",   1);
    stereoModeBox_.addItem("Mid/Side", 2);
    addAndMakeVisible(stereoModeBox_);
    stereoModeAttach_ = std::make_unique<ComboAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamStereoMode, stereoModeBox_);

    soloLButton_.setButtonText("Solo L");
    soloRButton_.setButtonText("Solo R");
    addAndMakeVisible(soloLButton_);
    addAndMakeVisible(soloRButton_);
    soloLAttach_ = std::make_unique<ButtonAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamSoloLeft,  soloLButton_);
    soloRAttach_ = std::make_unique<ButtonAttach>(
        p.apvts, PhuFairKid67AudioProcessor::kParamSoloRight, soloRButton_);

    // ── Meters ────────────────────────────────────────────────────────────────

    setupGroupLabel(metersGroupLabel_, "METERS");
    addAndMakeVisible(metersGroupLabel_);

    auto setupMeterLabel = [&](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(kFontControl)));
        l.setColour(juce::Label::textColourId, kColText);
        l.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(l);
    };
    setupMeterLabel(meterLabelInL_,  "In L");
    setupMeterLabel(meterLabelInR_,  "In R");
    setupMeterLabel(meterLabelGRL_,  "GR L");
    setupMeterLabel(meterLabelGRR_,  "GR R");
    setupMeterLabel(meterLabelOutL_, "Out L");
    setupMeterLabel(meterLabelOutR_, "Out R");

    addAndMakeVisible(inputMeterL_);
    addAndMakeVisible(inputMeterR_);
    addAndMakeVisible(grMeterL_);
    addAndMakeVisible(grMeterR_);
    addAndMakeVisible(outputMeterL_);
    addAndMakeVisible(outputMeterR_);

    // ── Final setup ───────────────────────────────────────────────────────────
    setSize(kWindowWidth, kWindowHeight);
    startTimerHz(20);
}

PhuFairKid67AudioProcessorEditor::~PhuFairKid67AudioProcessorEditor() {
    stopTimer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Paint
// ─────────────────────────────────────────────────────────────────────────────

void PhuFairKid67AudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(kColBackground);

    // Draw a subtle panel background behind each column and the meter section.
    g.setColour(kColPanel);
    const float cornerRadius = 4.0f;
    g.fillRoundedRectangle(leftColBounds_.toFloat(),    cornerRadius);
    g.fillRoundedRectangle(centerColBounds_.toFloat(),  cornerRadius);
    g.fillRoundedRectangle(rightColBounds_.toFloat(),   cornerRadius);
    g.fillRoundedRectangle(meterSectionBounds_.toFloat(), cornerRadius);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resized – all layout computed from named constants; no bare literals
// ─────────────────────────────────────────────────────────────────────────────

void PhuFairKid67AudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(kOuterPad);

    // ── Top bar (remove from top) ─────────────────────────────────────────────
    layoutTopBar(area.removeFromTop(kTopBarH));
    area.removeFromTop(kSectionGap);

    // ── Meter section (remove from bottom) ───────────────────────────────────
    meterSectionBounds_ = area.removeFromBottom(kMeterSectionH);
    layoutMeters(meterSectionBounds_);
    area.removeFromBottom(kSectionGap);

    // ── Three equal columns for the remaining area ────────────────────────────
    const int numGaps = 2;
    const int colW    = (area.getWidth() - numGaps * kSectionGap) / 3;

    leftColBounds_   = area.removeFromLeft(colW);
    area.removeFromLeft(kSectionGap);
    centerColBounds_ = area.removeFromLeft(colW);
    area.removeFromLeft(kSectionGap);
    rightColBounds_  = area; // remainder (may be 1–2 px wider due to integer division)

    layoutLeftColumn(leftColBounds_.reduced(kItemGap));
    layoutCenterColumn(centerColBounds_.reduced(kItemGap));
    layoutRightColumn(rightColBounds_.reduced(kItemGap));
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout helpers
// ─────────────────────────────────────────────────────────────────────────────

void PhuFairKid67AudioProcessorEditor::layoutTopBar(juce::Rectangle<int> area) {
    titleLabel_.setBounds(area.removeFromLeft(kTopTitleW));
    area.removeFromLeft(kItemGap);

    bypassButton_.setBounds(area.removeFromLeft(kTopBypassW)
                                .withSizeKeepingCentre(kTopBypassW, kTopBarH));
    area.removeFromLeft(kSectionGap);

    oversamplingLabel_.setBounds(area.removeFromLeft(kTopComboLabelW));
    oversamplingBox_.setBounds(area.removeFromLeft(kTopComboW));
    area.removeFromLeft(kSectionGap);

    qualityLabel_.setBounds(area.removeFromLeft(kTopComboLabelW));
    qualityBox_.setBounds(area.removeFromLeft(kTopComboW));
    area.removeFromLeft(kSectionGap);

    latencyLabel_.setBounds(area); // remaining space
}

void PhuFairKid67AudioProcessorEditor::layoutLeftColumn(juce::Rectangle<int> area) {
    // ── INPUT group ───────────────────────────────────────────────────────────
    inputGroupLabel_.setBounds(area.removeFromTop(kGroupLabelH));
    area.removeFromTop(kItemGap);

    {
        const int cellH = kControlLabelH + kKnobSize + kTextBoxH;
        auto row = area.removeFromTop(cellH);
        const int halfW = row.getWidth() / 2;
        placeKnob(inputTrimLSlider_, inputTrimLLabel_, row.removeFromLeft(halfW));
        placeKnob(inputTrimRSlider_, inputTrimRLabel_, row);
    }
    area.removeFromTop(kSectionGap);

    // ── OUTPUT group ──────────────────────────────────────────────────────────
    outputGroupLabel_.setBounds(area.removeFromTop(kGroupLabelH));
    area.removeFromTop(kItemGap);

    {
        const int cellH = kControlLabelH + kKnobSize + kTextBoxH;
        auto row = area.removeFromTop(cellH);
        const int halfW = row.getWidth() / 2;
        placeKnob(outputTrimLSlider_, outputTrimLLabel_, row.removeFromLeft(halfW));
        placeKnob(outputTrimRSlider_, outputTrimRLabel_, row);
    }
    area.removeFromTop(kSectionGap);

    // ── Mix (centred in remaining width) ──────────────────────────────────────
    {
        const int cellH = kControlLabelH + kKnobSize + kTextBoxH;
        auto row = area.removeFromTop(cellH);
        // Centre a knob-width cell horizontally
        const int xOff = (row.getWidth() - kKnobSize) / 2;
        placeKnob(mixSlider_, mixLabel_,
                  row.withTrimmedLeft(xOff).withWidth(kKnobSize));
    }
}

void PhuFairKid67AudioProcessorEditor::layoutCenterColumn(juce::Rectangle<int> area) {
    // ── COMPRESSION group ─────────────────────────────────────────────────────
    compressionGroupLabel_.setBounds(area.removeFromTop(kGroupLabelH));
    area.removeFromTop(kItemGap);

    // Threshold knobs (L and R side by side)
    {
        const int cellH = kControlLabelH + kKnobSize + kTextBoxH;
        auto row = area.removeFromTop(cellH);
        const int halfW = row.getWidth() / 2;
        placeKnob(thresholdLSlider_, thresholdLLabel_, row.removeFromLeft(halfW));
        placeKnob(thresholdRSlider_, thresholdRLabel_, row);
    }
    area.removeFromTop(kSectionGap);

    // Timing combo
    placeCombo(timingBox_, timingLabel_,
               area.removeFromTop(kControlLabelH + kComboH));
    area.removeFromTop(kItemGap);

    // Link Mode combo
    placeCombo(linkModeBox_, linkModeLabel_,
               area.removeFromTop(kControlLabelH + kComboH));
    area.removeFromTop(kSectionGap);

    // Ck (centred)
    {
        const int cellH = kControlLabelH + kKnobSize + kTextBoxH;
        auto row = area.removeFromTop(cellH);
        const int xOff = (row.getWidth() - kKnobSize) / 2;
        placeKnob(ckSlider_, ckLabel_,
                  row.withTrimmedLeft(xOff).withWidth(kKnobSize));
    }
}

void PhuFairKid67AudioProcessorEditor::layoutRightColumn(juce::Rectangle<int> area) {
    stereoGroupLabel_.setBounds(area.removeFromTop(kGroupLabelH));
    area.removeFromTop(kItemGap);

    // Stereo Mode combo
    placeCombo(stereoModeBox_, stereoModeLabel_,
               area.removeFromTop(kControlLabelH + kComboH));
    area.removeFromTop(kSectionGap);

    // Solo buttons (side by side)
    {
        auto row = area.removeFromTop(kButtonH);
        const int halfW = row.getWidth() / 2;
        soloLButton_.setBounds(row.removeFromLeft(halfW).reduced(kItemGap, 0));
        soloRButton_.setBounds(row.reduced(kItemGap, 0));
    }
}

void PhuFairKid67AudioProcessorEditor::layoutMeters(juce::Rectangle<int> area) {
    metersGroupLabel_.setBounds(area.removeFromTop(kGroupLabelH));
    area.removeFromTop(kItemGap);

    // Place one row: label on the left, meter bar on the right.
    auto placeRow = [&](juce::Label& rowLabel, LevelMeter& meter) {
        auto row = area.removeFromTop(kMeterBarH);
        area.removeFromTop(kItemGap);
        rowLabel.setBounds(row.removeFromLeft(kMeterLabelW));
        meter.setBounds(row);
    };

    placeRow(meterLabelInL_,  inputMeterL_);
    placeRow(meterLabelInR_,  inputMeterR_);
    placeRow(meterLabelGRL_,  grMeterL_);
    placeRow(meterLabelGRR_,  grMeterR_);
    placeRow(meterLabelOutL_, outputMeterL_);
    placeRow(meterLabelOutR_, outputMeterR_);
}

void PhuFairKid67AudioProcessorEditor::placeKnob(juce::Slider&           slider,
                                                   juce::Label&            label,
                                                   juce::Rectangle<int>    cell) {
    label.setBounds(cell.removeFromTop(kControlLabelH));
    // Centre the rotary knob horizontally within the cell.
    const int x = cell.getX() + (cell.getWidth() - kKnobSize) / 2;
    slider.setBounds(x, cell.getY(), kKnobSize, kKnobSize + kTextBoxH);
}

void PhuFairKid67AudioProcessorEditor::placeCombo(juce::ComboBox&        box,
                                                    juce::Label&           label,
                                                    juce::Rectangle<int>   cell) {
    label.setBounds(cell.removeFromTop(kControlLabelH));
    box.setBounds(cell);
}

// ─────────────────────────────────────────────────────────────────────────────
// Timer callback – runs on the message thread at ~20 Hz
// ─────────────────────────────────────────────────────────────────────────────

void PhuFairKid67AudioProcessorEditor::timerCallback() {
    using P = PhuFairKid67AudioProcessor;

    // ── Update meters (all reads are atomic) ─────────────────────────────────

    inputMeterL_.setLevelDb(audioProcessor.getMeterInputLDb());
    inputMeterR_.setLevelDb(audioProcessor.getMeterInputRDb());

    const float grL = audioProcessor.apvts
                          .getRawParameterValue(P::kParamMeterGainReductionL)->load();
    const float grR = audioProcessor.apvts
                          .getRawParameterValue(P::kParamMeterGainReductionR)->load();
    grMeterL_.setLevelDb(grL);
    grMeterR_.setLevelDb(grR);

    const float outL = audioProcessor.apvts
                           .getRawParameterValue(P::kParamMeterOutputL)->load();
    const float outR = audioProcessor.apvts
                           .getRawParameterValue(P::kParamMeterOutputR)->load();
    outputMeterL_.setLevelDb(outL);
    outputMeterR_.setLevelDb(outR);

    // ── Update latency readout ────────────────────────────────────────────────

    const int latency = audioProcessor.getLatencySamples();
    latencyLabel_.setText("Latency: " + juce::String(latency) + " smp",
                          juce::dontSendNotification);

    // ── Relabel I/O trim controls for Mid/Side stereo mode ───────────────────
    // When stereo mode is Mid/Side, "Left" effectively trims Mid and
    // "Right" trims Side (see PluginProcessor.cpp: stereo encode/decode).

    const bool isMidSide =
        static_cast<int>(
            audioProcessor.apvts.getRawParameterValue(P::kParamStereoMode)->load()) == 1;

    const juce::String lLabel = isMidSide ? "Mid"  : "L";
    const juce::String rLabel = isMidSide ? "Side" : "R";

    inputTrimLLabel_.setText(lLabel,  juce::dontSendNotification);
    inputTrimRLabel_.setText(rLabel,  juce::dontSendNotification);
    outputTrimLLabel_.setText(lLabel, juce::dontSendNotification);
    outputTrimRLabel_.setText(rLabel, juce::dontSendNotification);
}

