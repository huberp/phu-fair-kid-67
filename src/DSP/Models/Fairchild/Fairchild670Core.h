#pragma once

#include "VariableMuStage.h"
#include "../Sidechain/RectifierDetector.h"

#include <algorithm>

namespace Models {

/// Stereo link mode for the Fairchild 670 sidechain.
enum class LinkMode {
    Independent, ///< L and R channels detect and compress independently.
    Linked       ///< Shared sidechain envelope drives both channels.
};

/// Envelope combination strategy used when LinkMode::Linked is active.
enum class LinkedEnvelopeStrategy {
    Max,     ///< The louder channel's CV drives both (harder-knee linked).
    Average  ///< Average of both channel CVs drives both (softer-knee linked).
};

// ─────────────────────────────────────────────────────────────────────────────

/// Snapshot of meter values updated each processed sample.
///
/// Peak accumulators (inPeakL/R, outPeakL/R) hold the highest absolute sample
/// value since the last call to resetPeakMeters().  The CV fields reflect the
/// most recently computed control voltage for each channel.
struct Fairchild670Meters {
    float cvL      = 0.0f; ///< Control voltage applied to the L stage (V).
    float cvR      = 0.0f; ///< Control voltage applied to the R stage (V).
    float inPeakL  = 0.0f; ///< Input peak level, L channel (linear, ±1 full-scale).
    float inPeakR  = 0.0f; ///< Input peak level, R channel (linear, ±1 full-scale).
    float outPeakL = 0.0f; ///< Output peak level, L channel (linear, ±1 full-scale).
    float outPeakR = 0.0f; ///< Output peak level, R channel (linear, ±1 full-scale).
};

// ─────────────────────────────────────────────────────────────────────────────

/// Processing quality preset: controls the Newton-Raphson iteration budget
/// used by the variable-mu gain stages.
enum class ProcessingQuality {
    Draft, ///< Reduced NR iterations (faster CPU, slightly less accurate).
    High,  ///< Full NR iterations (default accuracy).
};

// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the Fairchild 670 stereo core.
struct Fairchild670CoreConfig {
    /// Stereo link mode.
    LinkMode linkMode = LinkMode::Linked;

    /// Envelope combination strategy (used only when linkMode == Linked).
    LinkedEnvelopeStrategy envelopeStrategy = LinkedEnvelopeStrategy::Max;

    /// Sidechain detector configuration (same timing preset applied to L and R).
    Sidechain::RectifierDetectorConfig detectorCfg;

    /// Variable-mu gain stage configuration (same circuit for L and R).
    VariableMuStageConfig stageCfg;
};

// ─────────────────────────────────────────────────────────────────────────────

/// Fairchild 670 stereo compressor core.
///
/// Owns two independent signal paths (L/R), each consisting of a sidechain
/// RectifierDetector followed by a VariableMuStage.  The link mode controls
/// whether the two sidechain envelopes are merged before being applied to the
/// gain stages.
///
/// Signal flow per stereo sample:
///   1. Run detectors: cvL = detectorL(inL), cvR = detectorR(inR).
///   2. Combine envelope according to link mode:
///        Independent → finalCvL = cvL, finalCvR = cvR
///        Linked/Max  → finalCvL = finalCvR = max(cvL, cvR)
///        Linked/Avg  → finalCvL = finalCvR = (cvL + cvR) / 2
///   3. Apply combined CV to both gain stages and process audio.
///   4. Update meter accumulators (CV snapshot + running peaks).
class Fairchild670Core {
public:
    explicit Fairchild670Core(Fairchild670CoreConfig cfg = {}) noexcept;

    /// Prepare both signal paths for the given sample rate.
    /// Must be called before the first processStereo() and on any sample-rate change.
    void prepare(double sampleRate) noexcept;

    /// Reset both signal paths to zero initial conditions and clear peak meters.
    void reset() noexcept;

    /// Set the stereo link mode.
    void setLinkMode(LinkMode mode) noexcept { cfg_.linkMode = mode; }

    /// Set the envelope combination strategy (takes effect immediately on the next sample).
    void setEnvelopeStrategy(LinkedEnvelopeStrategy strategy) noexcept {
        cfg_.envelopeStrategy = strategy;
    }

    /// Adjust the NR iteration budget on both variable-mu gain stages.
    ///
    /// Draft mode reduces the maximum iteration count to 8, saving CPU at
    /// the cost of slightly less accurate nonlinear modelling.  High mode
    /// restores the default 20-iteration budget.  The change takes effect
    /// on the next processStereo() call; no prepare() is required.
    void setQuality(ProcessingQuality quality) noexcept;

    /// Change the sidechain timing preset and recompute detector coefficients.
    /// The timing state of both detectors is preserved (attack/release smoothing
    /// continues from the current CV level with the new time constants).
    /// @param pos  New timing position (P1–P6).
    void setTimingPosition(Sidechain::TimingPosition pos) noexcept;

    /// Process one stereo sample pair through detectors and gain stages.
    ///
    /// @param inL   Left input sample (±1.0 full-scale).
    /// @param inR   Right input sample (±1.0 full-scale).
    /// @param outL  Left output sample (written on return).
    /// @param outR  Right output sample (written on return).
    void processStereo(float inL, float inR, float& outL, float& outR) noexcept;

    /// Reset the peak input/output accumulators to zero.
    /// Call once at the start of each processBlock before accumulating a new window.
    void resetPeakMeters() noexcept;

    /// Return a copy of the current meter snapshot.
    /// CV fields reflect the most recent sample; peak fields are the maximum
    /// since the last resetPeakMeters() call.
    [[nodiscard]] Fairchild670Meters meters() const noexcept { return meters_; }

private:
    Fairchild670CoreConfig cfg_;
    double sampleRate_ = 44100.0;

    VariableMuStage              stageL_;
    VariableMuStage              stageR_;
    Sidechain::RectifierDetector detectorL_;
    Sidechain::RectifierDetector detectorR_;

    Fairchild670Meters meters_;
};

} // namespace Models
