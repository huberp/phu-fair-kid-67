#pragma once

#include "analog/models/transformer/TransformerLinear.h"

namespace Models {

/// Configuration for the second-order (biquad) transformer model.
///
/// Inherits all fields from TransformerLinearConfig:
///   hpfCutoffHz, lpfCutoffHz, drive
///
/// and adds resonance Q-factors for the biquad sections.  Existing code that
/// writes to those fields continues to work without modification.
struct TransformerSecondOrderConfig : Analog::Models::TransformerLinearConfig
{
    /// HPF resonance Q.  Default = 1/√2 (Butterworth, maximally flat passband).
    float hpfQ = 0.7071f;

    /// LPF resonance Q.  Default = 1/√2 (Butterworth, maximally flat passband).
    /// Increasing Q above ~0.7071 introduces a gentle peak just before the HF
    /// rolloff corner — the "air" resonance characteristic of real transformers.
    float lpfQ = 0.7071f;
};

// ─────────────────────────────────────────────────────────────────────────────

/// Second-order audio transformer model: biquad HPF + biquad LPF + tanh saturator.
///
/// Compared to TransformerLinear (first-order filters), this class:
///   - Provides a steeper (12 dB/octave) roll-off on both flanks.
///   - Models the slight HF resonance peak from leakage inductance interacting
///     with winding capacitance (controlled by lpfQ).
///   - Models the second-order LF roll-off from core magnetising inductance.
///
/// The saturator is identical to TransformerLinear (memoryless tanh, unity
/// small-signal gain) and is applied after the LPF.
///
/// Interface is intentionally compatible with TransformerLinear.
class TransformerSecondOrder {
public:
    explicit TransformerSecondOrder(TransformerSecondOrderConfig cfg = {}) noexcept;

    /// Compute biquad coefficients for the current sample rate.
    /// Must be called before the first processSample() and on sample-rate changes.
    void prepare(double sampleRate) noexcept;

    /// Clear all filter state (sets delay lines to zero).
    void reset() noexcept;

    /// Replace the configuration and recompute coefficients.
    void setConfig(TransformerSecondOrderConfig cfg) noexcept;

    /// Process one audio sample.
    [[nodiscard]] float processSample(float sample) noexcept;

private:
    TransformerSecondOrderConfig cfg_;

    // HPF biquad coefficients.
    // Recurrence (standard Direct Form I, negative-feedback convention):
    //   y[n] = b0·x[n] + b1·x[n−1] + b2·x[n−2] − a1·y[n−1] − a2·y[n−2]
    float hpfB0_ = 1.0f, hpfB1_ = 0.0f, hpfB2_ = 0.0f;
    float hpfA1_ = 0.0f, hpfA2_ = 0.0f;

    // LPF biquad coefficients (same a1/a2 form as HPF).
    float lpfB0_ = 1.0f, lpfB1_ = 0.0f, lpfB2_ = 0.0f;
    float lpfA1_ = 0.0f, lpfA2_ = 0.0f;

    // Filter state (two-sample delay lines).
    float hpfX1_ = 0.0f, hpfX2_ = 0.0f, hpfY1_ = 0.0f, hpfY2_ = 0.0f;
    float lpfX1_ = 0.0f, lpfX2_ = 0.0f, lpfY1_ = 0.0f, lpfY2_ = 0.0f;
};

} // namespace Models
