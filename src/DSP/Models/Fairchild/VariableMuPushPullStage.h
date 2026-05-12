#pragma once

#include "analog/models/VariableMuStage.h"
#include "analog/nonlinear/NR.h"

namespace Models {

/// A push-pull (balanced differential) variable-mu stage built from two
/// back-to-back VariableMuStage instances.
///
/// Topology:
///   Arm A: processes  +Vin − cvBias  (non-inverting half).
///   Arm B: processes  −Vin − cvBias  (inverting half).
///   Output: (outA − outB) / 2  (differential plate voltage).
///
/// Why push-pull matters:
///   A single-ended common-cathode stage (arm A alone) produces predominantly
///   2nd-harmonic (even) distortion.  Arm B, driven with the inverted signal,
///   generates the same even harmonics at the output — but because the two
///   arms are summed differentially those even harmonics cancel, leaving only
///   the odd harmonics (3rd, 5th, …).  This is the reason the Fairchild 670
///   sounds "clean but warm" rather than "glassy".
///
/// Each arm is a full VariableMuStage with its own NR solver, DC blocker, and
/// gain normalisation.  Both arms are configured identically and receive the
/// same CV bias.
///
/// Interface is intentionally compatible with VariableMuStage so that
/// Fairchild670Core can use either interchangeably.
class VariableMuPushPullStage {
public:
    explicit VariableMuPushPullStage(Analog::Models::VariableMuStageConfig cfg = {}) noexcept;

    /// Prepare both arms for the given sample rate.
    void prepare(double sampleRate) noexcept;

    /// Reset both arms to their quiescent initial conditions.
    void reset() noexcept;

    /// Set the control voltage applied to both arms.
    void setCv(float cv) noexcept;

    /// Override the Newton-Raphson policy for both arms.
    void setNRConfig(Analog::Nonlinear::NRConfig cfg) noexcept;

    /// Set the cathode bypass capacitance for both arms.
    void setCathodeBypassCapacitance(double farads) noexcept;

    /// Process one audio sample through the push-pull pair.
    /// @param sample  Normalised input (±1.0 full-scale).
    /// @return        Normalised differential output (even harmonics cancelled).
    [[nodiscard]] float processSample(float sample) noexcept;

private:
    Analog::Models::VariableMuStage stageA_; ///< Non-inverting (+Vin) arm.
    Analog::Models::VariableMuStage stageB_; ///< Inverting   (−Vin) arm.
};

} // namespace Models
