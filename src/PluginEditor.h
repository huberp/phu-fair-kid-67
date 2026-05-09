#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

// ─────────────────────────────────────────────────────────────────────────────
// LevelMeter
// ─────────────────────────────────────────────────────────────────────────────

/// Simple horizontal bar meter.
///
/// Signal kind:       levelDb in -60..0 dBFS; bar fills left-to-right,
///                    colour transitions green -> amber -> red near 0 dBFS.
/// GainReduction kind: levelDb in -30..0 dB (0 = no reduction, -30 = heavy);
///                    bar fills left-to-right showing reduction amount in red.
class LevelMeter final : public juce::Component {
  public:
    enum class Kind { Signal, GainReduction, CV };

    explicit LevelMeter(Kind kind = Kind::Signal) noexcept : kind_(kind) {}

    void setLevelDb(float db) noexcept {
        levelDb_ = db;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto b = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xFF1A1A1A));
        g.fillRoundedRectangle(b, 2.0f);

        float fraction;
        juce::Colour barColour;

        if (kind_ == Kind::Signal) {
            fraction  = juce::jmap(levelDb_, -60.0f, 0.0f, 0.0f, 1.0f);
            barColour = (levelDb_ >= -6.0f)  ? juce::Colour(0xFFE03020)
                      : (levelDb_ >= -20.0f) ? juce::Colour(0xFFE09020)
                                             : juce::Colour(0xFF30A040);
        } else if (kind_ == Kind::GainReduction) {
            // Gain reduction: levelDb_ is 0..–30; map inverted so more reduction = more fill
            fraction  = juce::jmap(levelDb_, 0.0f, -30.0f, 0.0f, 1.0f);
            barColour = juce::Colour(0xFFD05030);
        } else {
            // CV: levelDb_ is reused as a raw voltage in 0..6 V
            fraction  = juce::jmap(levelDb_, 0.0f, 6.0f, 0.0f, 1.0f);
            barColour = juce::Colour(0xFFCCAA44); // amber — matches group label colour
        }

        fraction = juce::jlimit(0.0f, 1.0f, fraction);
        if (fraction > 0.002f) {
            g.setColour(barColour);
            g.fillRoundedRectangle(b.withWidth(b.getWidth() * fraction), 2.0f);
        }
    }

  private:
    Kind  kind_;
    float levelDb_ = -60.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// PhuFairKid67AudioProcessorEditor
// ─────────────────────────────────────────────────────────────────────────────

class PhuFairKid67AudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer {
  public:
    explicit PhuFairKid67AudioProcessorEditor(PhuFairKid67AudioProcessor&);
    ~PhuFairKid67AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    void timerCallback() override;

    // ── Layout helpers ────────────────────────────────────────────────────────
    void layoutTopBar(juce::Rectangle<int>);
    void layoutLeftColumn(juce::Rectangle<int>);
    void layoutCenterColumn(juce::Rectangle<int>);
    void layoutRightColumn(juce::Rectangle<int>);
    void layoutMeters(juce::Rectangle<int>);

    /// Place a rotary slider and its label inside `cell`.
    /// The label occupies the top, the rotary the remainder (centred horizontally).
    void placeKnob(juce::Slider& slider, juce::Label& label,
                   juce::Rectangle<int> cell);

    /// Place a combo box and its label inside `cell`.
    /// The label occupies the top, the combo box the remainder.
    void placeCombo(juce::ComboBox& box, juce::Label& label,
                    juce::Rectangle<int> cell);

    PhuFairKid67AudioProcessor& audioProcessor;

    // ── Top bar ───────────────────────────────────────────────────────────────
    juce::Label       titleLabel_;
    juce::ToggleButton bypassButton_;
    juce::ComboBox    oversamplingBox_;
    juce::Label       oversamplingLabel_;
    juce::ComboBox    qualityBox_;
    juce::Label       qualityLabel_;
    juce::Label       latencyLabel_;

    // ── Left column: Input / Output trims + Mix ───────────────────────────────
    juce::Label  inputGroupLabel_;
    juce::Slider inputTrimLSlider_, inputTrimRSlider_;
    juce::Label  inputTrimLLabel_,  inputTrimRLabel_;

    juce::Label  outputGroupLabel_;
    juce::Slider outputTrimLSlider_, outputTrimRSlider_;
    juce::Label  outputTrimLLabel_,  outputTrimRLabel_;

    juce::Label  mixLabel_;
    juce::Slider mixSlider_;

    // ── Center column: Compression ────────────────────────────────────────────
    juce::Label  compressionGroupLabel_;
    juce::Slider thresholdLSlider_, thresholdRSlider_;
    juce::Label  thresholdLLabel_,  thresholdRLabel_;
    juce::ComboBox timingBox_;
    juce::Label    timingLabel_;
    juce::ComboBox linkModeBox_;
    juce::Label    linkModeLabel_;
    juce::Slider   ckSlider_;
    juce::Label    ckLabel_;

    // ── Right column: Stereo tools ────────────────────────────────────────────
    juce::Label       stereoGroupLabel_;
    juce::ComboBox    stereoModeBox_;
    juce::Label       stereoModeLabel_;
    juce::ToggleButton soloLButton_, soloRButton_;

    // ── Meters ────────────────────────────────────────────────────────────────
    juce::Label metersGroupLabel_;
    LevelMeter  inputMeterL_  { LevelMeter::Kind::Signal };
    LevelMeter  inputMeterR_  { LevelMeter::Kind::Signal };
    LevelMeter  grMeterL_     { LevelMeter::Kind::GainReduction };
    LevelMeter  grMeterR_     { LevelMeter::Kind::GainReduction };
    LevelMeter  cvMeterL_     { LevelMeter::Kind::CV };
    LevelMeter  cvMeterR_     { LevelMeter::Kind::CV };
    LevelMeter  outputMeterL_ { LevelMeter::Kind::Signal };
    LevelMeter  outputMeterR_ { LevelMeter::Kind::Signal };
    juce::Label meterLabelInL_, meterLabelInR_;
    juce::Label meterLabelGRL_, meterLabelGRR_;
    juce::Label meterLabelOutL_, meterLabelOutR_;
    juce::Label meterLabelCvL_,  meterLabelCvR_;

    /// Smoothed GR display state: attack is instant, release follows the
    /// current timing position's release time constant.  Message-thread only.
    float displayGrL_ = 0.0f;
    float displayGrR_ = 0.0f;

    // Rectangles stored in resized() so paint() can draw panel backgrounds
    // without duplicating layout logic.
    juce::Rectangle<int> leftColBounds_, centerColBounds_, rightColBounds_;
    juce::Rectangle<int> meterSectionBounds_;

    // ── APVTS Attachments ─────────────────────────────────────────────────────
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttach> inputTrimLAttach_,  inputTrimRAttach_;
    std::unique_ptr<SliderAttach> outputTrimLAttach_, outputTrimRAttach_;
    std::unique_ptr<SliderAttach> mixAttach_;
    std::unique_ptr<ComboAttach>  oversamplingAttach_, qualityAttach_;
    std::unique_ptr<ButtonAttach> bypassAttach_;
    std::unique_ptr<SliderAttach> thresholdLAttach_, thresholdRAttach_;
    std::unique_ptr<ComboAttach>  timingAttach_, linkModeAttach_;
    std::unique_ptr<SliderAttach> ckAttach_;
    std::unique_ptr<ComboAttach>  stereoModeAttach_;
    std::unique_ptr<ButtonAttach> soloLAttach_, soloRAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhuFairKid67AudioProcessorEditor)
};

