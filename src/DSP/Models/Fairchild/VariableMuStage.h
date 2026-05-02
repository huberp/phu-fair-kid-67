#pragma once

#include "../../Circuit/Elements/Capacitor.h"
#include "../../Circuit/Nonlinear/NR.h"
#include "../../Circuit/Nonlinear/TriodeKoren.h"

#include <vector>

namespace Models {

/// Configuration for the variable-mu triode gain stage.
///
/// Default values model a 6072 triode (used in Fairchild-style levelling
/// amplifiers) with a B+ of 250 V, typical plate and cathode resistors,
/// and no cathode bypass capacitor.
struct VariableMuStageConfig {
    /// Tube model parameters; defaults to 6072 (Fairchild-style variable-mu tube).
    Circuit::Nonlinear::TubeParams tube;

    double Vcc         = 250.0;  ///< B+ supply voltage (V).
    double Rp          = 100e3;  ///< Plate load resistor (Ω).
    double Rk          = 1.5e3;  ///< Cathode resistor (Ω).
    double Ck          = 0.0;    ///< Cathode bypass capacitance (F); 0 = no bypass.

    /// ±V clamp applied to the audio grid input before the solve.
    /// Guards against divergence with extreme input; does not affect normal
    /// audio levels (±1.0 full-scale = ±10 V).
    double inputClampV = 5.0;

    /// Maximum additional negative grid bias (V) that the CV can impose.
    /// CV is clamped to [0, cvMaxV] before being applied.
    /// A value of 6 V covers the full useful attenuation range of small-signal
    /// triodes without driving the tube into deep cut-off (which would destabilise
    /// the NR solver).
    double cvMaxV = 6.0;

    /// Newton-Raphson iteration policy.
    /// Defaults are tuned for tube-stage voltages (unlimited step size; the
    /// input clamp provides the necessary convergence guardrail).
    Circuit::Nonlinear::NRConfig nr;

    /// Default-construct with 6072 tube and NR settings appropriate for a
    /// variable-mu tube stage (unlimited step size, 20 max iterations).
    VariableMuStageConfig();
};

// ─────────────────────────────────────────────────────────────────────────────

/// Variable-mu triode gain stage solved per sample via MNA + Newton-Raphson.
///
/// Topology (identical to TubeStage, but the grid sees an additional DC bias
/// supplied by the detector control voltage):
/// @code
///   Vcc ─── Rp ──── Plate (Vp)
///                     │
///                  [Triode]
///                     │
///               Cathode (Vk) ──── Rk ──── GND
///                             (║ Ck, optional bypass cap)
///
///   Grid voltage = (scaled + clamped audio input Vin) − cvBias_
/// @endcode
///
/// Gain reduction mechanism:
///   A positive CV (control voltage from the sidechain detector) is applied as
///   an additional negative bias on the grid:
///
///     Vgk = Vin − Vk − cvBias_
///
///   Increasing cvBias_ drives the grid more negative, reducing the plate
///   current (Ip) and therefore the voltage gain.  The attenuation is
///   monotonically increasing with CV over the normal operating range.
///
/// CV safety bounds:
///   The CV is clamped to [0, cvMaxV] before use.  Values below 0 are raised
///   to 0 (no negative-going bias would increase gain beyond the quiescent
///   point and could destabilise the solver); values above cvMaxV are clamped
///   to avoid driving the tube into deep cut-off where the Jacobian becomes
///   near-singular.
///
/// The 2×2 NR solve is identical to TubeStage except that Vgk carries the
/// bias offset.  The Jacobian is unchanged because cvBias_ is constant within
/// each sample.
///
/// Per-sample flow:
///   1. Input sample → volts via UnitScaling (±1 full-scale → ±10 V).
///   2. Grid voltage = clamped Vin − cvBias_.
///   3. 2×2 NR solve → Vp, Vk.
///   4. Plate voltage → normalised output sample via UnitScaling.
class VariableMuStage {
public:
    explicit VariableMuStage(VariableMuStageConfig cfg = {}) noexcept;

    /// Recompute capacitor companion conductances for the given sample rate.
    /// Must be called before the first processSample() and on sample-rate changes.
    void prepare(double sampleRate) noexcept;

    /// Reset state to zero initial conditions (clears capacitor history,
    /// returns the operating-point estimate to its start-up value, and sets
    /// the CV bias to zero).
    void reset() noexcept;

    /// Set the control voltage (V).
    ///
    /// The CV is clamped to [0, cfg.cvMaxV] internally.  Pass the raw
    /// sidechain detector output here; no scaling is required.
    ///
    /// @param cv  Control voltage (V); non-negative values produce attenuation.
    void setCv(float cv) noexcept;

    /// Return the current (clamped) CV bias in use (V).
    [[nodiscard]] float cv() const noexcept { return static_cast<float>(cvBias_); }

    /// Process one audio sample through the variable-mu gain stage.
    /// @param sample  Normalised input (±1.0 full-scale).
    /// @return        Normalised output (plate voltage / kVoltsPerSample).
    [[nodiscard]] float processSample(float sample) noexcept;

private:
    VariableMuStageConfig cfg_;

    /// Current (clamped) grid bias imposed by the control voltage (V).
    double cvBias_ = 0.0;

    /// Operating-point state vector: x_[0] = Vp (V), x_[1] = Vk (V).
    /// Warm-starts each NR solve; updated to the converged solution each sample.
    std::vector<double> x_;

    /// Cathode bypass capacitor companion model (active only when cfg_.Ck > 0).
    Circuit::CapacitorCompanion capK_;

    /// Newton-Raphson iteration manager (constructed once from cfg_.nr).
    Circuit::Nonlinear::NRPolicy nr_;
};

} // namespace Models
