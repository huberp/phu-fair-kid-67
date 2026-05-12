#pragma once

#include "TubePresets6386.h"
#include "VariableMuPushPullStage.h"
#include "../Sidechain/SoftRectifierDetector.h"
#include "../Transformer/TransformerSecondOrder.h"

#include "analog/models/VariableMuStage.h"
#include "analog/models/transformer/TransformerLinear.h"
#include "analog/models/sidechain/RectifierDetector.h"
#include "../Sidechain/TimingNetworkAdapter.h"

#include <algorithm>

namespace Models {

/// Stereo link mode for the Fairchild 670 sidechain.
enum class LinkMode {
    Independent, ///< L and R channels detect and compress independently.
    Linked,      ///< Shared sidechain envelope drives both channels.
    MidSide      ///< Lateral/Vertical mode: sidechain driven by the Mid (L+R) signal only.
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
///
/// All default values reflect the enhanced model that includes the following
/// hardware-accurate components (see fairchild670-spec-traceability.md):
///
///   P1  6386 remote-cutoff tube (stageCfg.tube, x=1.0 Koren exponent)
///   P2  Push-pull differential stage (always active — reduces even harmonics)
///   P3  Input transformer coloration (inputTransformerCfg)
///   P4  6AL5 tube-rectifier soft onset (tubeRectifierForwardVoltageV)
///   P5  Interstage transformer (interstageTransformerCfg)
///   P6  Second-order output transformer with biquad filters (transformerCfg)
///   P7  Pre-amplifier tube stage coloration (preampCfg)
struct Fairchild670CoreConfig {
    /// Stereo link mode.
    LinkMode linkMode = LinkMode::Linked;

    /// Envelope combination strategy (used only when linkMode == Linked).
    LinkedEnvelopeStrategy envelopeStrategy = LinkedEnvelopeStrategy::Max;

    /// Sidechain timing preset (same preset applied to L and R detectors).
    Sidechain::TimingPosition timingPreset = Sidechain::TimingPosition::P1;

    /// [P1] Variable-mu gain stage configuration.
    /// Defaults to the 6386 remote-cutoff tube approximation (x=1.0 Koren exponent).
    Analog::Models::VariableMuStageConfig stageCfg = makeStageCfg6386();

    /// [P3] Input transformer coloration (the first stage the signal touches).
    /// Default: HPF=30 Hz, LPF=30 kHz, drive=1.2 — wider bandwidth and slightly
    /// stronger saturation than the output transformer.
    Analog::Models::TransformerLinearConfig inputTransformerCfg = makeInputTransformerCfg();

    /// [P4] 6AL5 twin-diode forward voltage (≈ 0.8 V).
    /// The sidechain rectifier does not respond to signals whose peak amplitude
    /// in volts is below this value.  Set to 0.0 for an ideal rectifier.
    float tubeRectifierForwardVoltageV = 0.8f;

    /// Sidechain amplifier gain — scales the 6AL5 detector output before it
    /// is applied to the 6386 control grids.
    ///
    /// In the hardware Fairchild 670 the sidechain signal is amplified by a
    /// dedicated tube chain (12AX7 → 12BH7 → 6973 → output transformer T104)
    /// before reaching the 6AL5 detector.  The detector then drives the 6386
    /// grids through only a 30 Ω timing resistor (R107/R108) and a 33 Ω stopper
    /// (R111) — no amplifying divider network exists in that path.
    ///
    /// This scalar therefore represents the net level difference introduced by
    /// the sidechain amplifier chain that is not otherwise modelled in software.
    /// At 1.5 the applied CV reaches the 8 V ceiling (cvMaxV) at 0 dBFS input,
    /// producing ≥ 20 dB gain reduction as specified by the hardware.
    float sidechainAmplifierGain = 1.5f;

    /// [P5] Interstage transformer coloration (between variable-mu and output stages).
    /// Default: HPF=25 Hz, LPF=22 kHz, drive=1.1 — slightly tighter than the output
    /// transformer, matching the narrower bandwidth typically seen in interstage
    /// coupling transformers.
    Analog::Models::TransformerLinearConfig interstageTransformerCfg = makeInterstageTransformerCfg();

    /// [P6] Output transformer coloration — 2nd-order biquad HPF + LPF + tanh saturator.
    /// Inherits all TransformerLinearConfig fields (hpfCutoffHz, lpfCutoffHz, drive)
    /// and adds hpfQ / lpfQ (default Q=0.7071, Butterworth — backward compatible).
    TransformerSecondOrderConfig transformerCfg;

    /// [P7] Pre-amplifier tube stage configuration.
    /// Defaults to a 12AU7 triode operating at the same B+ and load as the main stage.
    /// This stage always operates at CV=0 (no compression) and adds harmonic coloration.
    Analog::Models::VariableMuStageConfig preampCfg = makePreampCfg();

    // ── Default-config factories ───────────────────────────────────────────────
    // (Used by the member default initialisers above; public so callers can
    //  build a known-good config as a starting point for customisation.)

    /// Build the default 6386 variable-mu stage config.
    static Analog::Models::VariableMuStageConfig makeStageCfg6386()
    {
        Analog::Models::VariableMuStageConfig cfg;
        cfg.tube   = DSP::tubeParams6386();
        // [P4] Raise the CV ceiling from the library default of 6.0 V to 8.0 V so
        // that the full detector output range (up to ~8.2 V at 0 dBFS) can be
        // applied to the tube.  Without this, the sidechain CV is clipped to 6 V
        // before the Koren model ever sees it, limiting max GR.
        cfg.cvMaxV = 8.0;
        return cfg;
    }

    /// Build the default input transformer config.
    static Analog::Models::TransformerLinearConfig makeInputTransformerCfg()
    {
        Analog::Models::TransformerLinearConfig cfg;
        cfg.hpfCutoffHz = 30.0;
        cfg.lpfCutoffHz = 30000.0;
        cfg.drive       = 1.2f;
        return cfg;
    }

    /// Build the default interstage transformer config.
    static Analog::Models::TransformerLinearConfig makeInterstageTransformerCfg()
    {
        Analog::Models::TransformerLinearConfig cfg;
        cfg.hpfCutoffHz = 25.0;
        cfg.lpfCutoffHz = 22000.0;
        cfg.drive       = 1.1f;
        return cfg;
    }

    /// Build the default 12AU7 pre-amplifier stage config.
    static Analog::Models::VariableMuStageConfig makePreampCfg()
    {
        Analog::Models::VariableMuStageConfig cfg;
        cfg.tube = Analog::Nonlinear::TubeParams::tubeParams12AU7();
        return cfg;
    }
};

// ─────────────────────────────────────────────────────────────────────────────

/// Fairchild 670 stereo compressor core.
///
/// Signal flow per stereo sample (hardware-accurate ordering):
///   1. Input → input transformer (bandwidth shaping + LF saturation).
///   2. Pre-amplifier tube stage (harmonic coloration, CV=0).
///   3. Sidechain detector reads the pre-amplifier output.
///   4. Envelope link logic → per-channel control voltage.
///   5. Push-pull variable-mu stage (6386 remote-cutoff model, even-harmonic
///      cancellation via differential combination).
///   6. Interstage transformer (inter-stage bandwidth shaping + saturation).
///   7. Second-order output transformer (2nd-order biquad HPF+LPF + tanh).
class Fairchild670Core {
public:
    explicit Fairchild670Core(Fairchild670CoreConfig cfg = {}) noexcept;

    /// Prepare all signal-path stages for the given sample rate.
    /// Must be called before the first processStereo() and on any sample-rate change.
    void prepare(double sampleRate) noexcept;

    /// Reset all signal paths to zero initial conditions and clear peak meters.
    void reset() noexcept;

    /// Set the stereo link mode.
    void setLinkMode(LinkMode mode) noexcept { cfg_.linkMode = mode; }

    /// Set the envelope combination strategy (takes effect immediately on the next sample).
    void setEnvelopeStrategy(LinkedEnvelopeStrategy strategy) noexcept {
        cfg_.envelopeStrategy = strategy;
    }

    /// Set the compression threshold as a voltage (V) subtracted from the raw
    /// detector CV before it is applied to the left gain stage.
    ///
    /// effectiveCvL = max(0, detectorCvL − thresholdVoltage)
    ///
    /// A value of 10 V places the threshold above any possible full-scale signal
    /// (no compression); 0 V means the stage always compresses.
    void setThresholdLeft(float thresholdVoltage) noexcept { thresholdVoltageL_ = thresholdVoltage; }

    /// Set the compression threshold as a voltage (V) subtracted from the raw
    /// detector CV before it is applied to the right gain stage.
    ///
    /// effectiveCvR = max(0, detectorCvR − thresholdVoltage)
    void setThresholdRight(float thresholdVoltage) noexcept { thresholdVoltageR_ = thresholdVoltage; }

    /// Adjust the NR iteration budget on the variable-mu and pre-amplifier stages.
    ///
    /// Draft mode reduces the maximum iteration count to 8, saving CPU at
    /// the cost of slightly less accurate nonlinear modelling.  High mode
    /// restores the default 20-iteration budget.  The change takes effect
    /// on the next processStereo() call; no prepare() is required.
    void setQuality(ProcessingQuality quality) noexcept;

    /// Set the cathode bypass capacitance on both variable-mu stages.
    void setCathodeBypassCapacitance(double farads) noexcept;

    /// Change the sidechain timing preset and recompute detector coefficients.
    /// The timing state of both detectors is preserved (attack/release smoothing
    /// continues from the current CV level with the new time constants).
    /// @param pos  New timing position (P1–P6).
    void setTimingPosition(Sidechain::TimingPosition pos) noexcept;

    /// Process one stereo sample pair through the full signal chain.
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
    double sampleRate_        = 44100.0;
    float  thresholdVoltageL_ = 10.0f; ///< Left-channel threshold (V); 10 V = no compression.
    float  thresholdVoltageR_ = 10.0f; ///< Right-channel threshold (V); 10 V = no compression.

    // [P7] Pre-amplifier tube stages (12AU7, CV=0 always).
    Analog::Models::VariableMuStage   preampL_;
    Analog::Models::VariableMuStage   preampR_;

    // [P2] Push-pull variable-mu gain stages (6386 remote-cutoff, even-harmonic cancellation).
    VariableMuPushPullStage           stageL_;
    VariableMuPushPullStage           stageR_;

    // [P3] Input transformer — first in the signal chain.
    Analog::Models::TransformerLinear inputTransformerL_;
    Analog::Models::TransformerLinear inputTransformerR_;

    // [P5] Interstage transformer — between variable-mu and output stages.
    Analog::Models::TransformerLinear interstageTransformerL_;
    Analog::Models::TransformerLinear interstageTransformerR_;

    // [P6] Output transformer — 2nd-order biquad model.
    TransformerSecondOrder            transformerL_;
    TransformerSecondOrder            transformerR_;

    // [P4] Sidechain soft-onset rectifiers (6AL5 forward-voltage model).
    Sidechain::SoftRectifierDetector  detectorL_;
    Sidechain::SoftRectifierDetector  detectorR_;

    Fairchild670Meters meters_;
};

} // namespace Models
