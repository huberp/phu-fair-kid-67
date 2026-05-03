#pragma once

#include <cmath>

namespace Models {

/// Configuration for the linear transformer coloration model.
///
/// Default values model a vintage-style audio output transformer with a
/// gentle low-end rolloff around 30 Hz (core magnetising inductance) and a
/// bandwidth limit near 18 kHz (leakage inductance / inter-winding capacitance).
struct TransformerLinearConfig {
    /// High-pass filter cutoff (Hz).  Models the transformer low-end rolloff
    /// caused by the finite magnetising inductance.  Signals well below this
    /// frequency are attenuated; signals above pass through.
    double hpfCutoffHz = 30.0;

    /// Low-pass filter cutoff (Hz).  Models the transformer bandwidth limit
    /// caused by leakage inductance and winding capacitance.  Signals well
    /// above this frequency are attenuated; signals below pass through.
    double lpfCutoffHz = 18000.0;

    /// Saturation drive (>= 1.0).
    ///
    /// 1.0  = fully linear (no saturation).
    /// > 1.0 = increasing core saturation character.  The gain is normalised
    ///          so that the small-signal (near-zero) amplitude response is
    ///          unaffected; only the soft-clipping threshold changes.
    ///
    /// Values up to approximately 10 produce audible but musically useful
    /// harmonic distortion.  Very large values approach hard clipping at ±1.
    float drive = 1.0f;
};

// ─────────────────────────────────────────────────────────────────────────────

/// Linear transformer coloration model.
///
/// Models the two main coloration mechanisms of a real audio transformer:
///
///  1. **Frequency shaping** — a first-order high-pass filter (HPF) rolls
///     off frequencies below hpfCutoffHz, and a first-order low-pass filter
///     (LPF) rolls off frequencies above lpfCutoffHz.  Both use the bilinear
///     transform so the −3 dB points are exact at the configured frequencies.
///
///  2. **Core saturation** — a memoryless tanh saturator with unity small-
///     signal gain models the soft-limiting characteristic of an iron core at
///     higher drive levels.  The drive parameter sets the knee sharpness.
///
/// Per-sample signal flow:
/// @code
///   input → HPF → LPF → saturator → output
/// @endcode
///
/// Two independent instances should be used for stereo (one per channel).
///
/// Usage:
/// @code
///   Models::TransformerLinear xfmrL, xfmrR;
///   xfmrL.prepare(sampleRate);
///   xfmrR.prepare(sampleRate);
///
///   // Each sample:
///   float outL = xfmrL.processSample(inL);
///   float outR = xfmrR.processSample(inR);
/// @endcode
class TransformerLinear {
public:
    explicit TransformerLinear(TransformerLinearConfig cfg = {}) noexcept;

    /// Recompute filter coefficients for the given sample rate.
    /// Must be called before the first processSample() and on sample-rate changes.
    void prepare(double sampleRate) noexcept;

    /// Reset filter states to zero initial conditions.
    void reset() noexcept;

    /// Set a new configuration at runtime.
    /// A subsequent prepare() call is required to update the coefficients.
    void setConfig(TransformerLinearConfig cfg) noexcept;

    /// Return the current configuration.
    [[nodiscard]] const TransformerLinearConfig& config() const noexcept
    {
        return cfg_;
    }

    /// Process one audio sample through HPF → LPF → saturator.
    /// @param sample  Normalised input (±1.0 full-scale).
    /// @return        Coloured and (if drive > 1) saturated output sample.
    [[nodiscard]] float processSample(float sample) noexcept;

private:
    TransformerLinearConfig cfg_;

    // ── HPF state (Direct Form I, first-order) ────────────────────────────
    float hpfB0_ = 1.0f;  ///< HPF numerator  b0
    float hpfB1_ = 0.0f;  ///< HPF numerator  b1
    float hpfA1_ = 0.0f;  ///< HPF feedback coefficient (y[n-1] multiplier)
    float hpfX1_ = 0.0f;  ///< HPF delayed input  x[n-1]
    float hpfY1_ = 0.0f;  ///< HPF delayed output y[n-1]

    // ── LPF state (Direct Form I, first-order) ────────────────────────────
    float lpfB0_ = 1.0f;  ///< LPF numerator  b0
    float lpfB1_ = 0.0f;  ///< LPF numerator  b1
    float lpfA1_ = 0.0f;  ///< LPF feedback coefficient (y[n-1] multiplier)
    float lpfX1_ = 0.0f;  ///< LPF delayed input  x[n-1]
    float lpfY1_ = 0.0f;  ///< LPF delayed output y[n-1]
};

} // namespace Models
