#pragma once

#include "TimingNetwork.h"

namespace Models::Sidechain {

/// Configuration for the RectifierDetector.
struct RectifierDetectorConfig {
    /// Fairchild timing preset (positions 1–6, stored as 0–5 internally).
    TimingPosition preset = TimingPosition::P1;
};

// ─────────────────────────────────────────────────────────────────────────────

/// Full-wave rectifier followed by an attack/release RC envelope follower.
///
/// Signal flow per sample:
///   1. Scale normalized input to volts (±1.0 full-scale → ±10 V).
///   2. Full-wave rectify: cv_in = |Vin|.
///   3. Attack/release RC smoothing:
///        if cv_in > cv_out : use alpha_attack  (fast response to louder signal)
///        else              : use alpha_release (slow decay back to silence)
///   4. Return the smoothed control voltage (V).
///
/// The smoothing coefficients are recomputed from the preset time constants and
/// the sample rate in prepare().
class RectifierDetector {
public:
    explicit RectifierDetector(RectifierDetectorConfig cfg = {}) noexcept;

    /// Recompute attack / release smoothing coefficients for the given sample
    /// rate.  Must be called before the first processSample() and whenever the
    /// sample rate changes.
    void prepare(double sampleRate) noexcept;

    /// Reset internal state (sets control voltage to 0 V).
    void reset() noexcept;

    /// Process one audio sample.
    ///
    /// @param sample  Normalized input (±1.0 full-scale = ±10 V).
    /// @return        Smoothed control voltage (V).
    [[nodiscard]] float processSample(float sample) noexcept;

    /// Return the current control voltage without advancing state (V).
    [[nodiscard]] float controlVoltage() const noexcept { return cv_; }

private:
    RectifierDetectorConfig cfg_;

    double alphaAttack_  = 0.0;  ///< Smoothing coefficient for the attack phase.
    double alphaRelease_ = 0.0;  ///< Smoothing coefficient for the release phase.
    float  cv_           = 0.0f; ///< Current control voltage (V).
};

} // namespace Models::Sidechain
