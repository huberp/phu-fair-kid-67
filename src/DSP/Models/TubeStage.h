#pragma once

#include "../Circuit/Elements/Capacitor.h"
#include "../Circuit/Nonlinear/NR.h"
#include "../Circuit/Nonlinear/TriodeKoren.h"

#include <array>

namespace Models {

/// Configuration for a common-cathode triode gain stage.
///
/// Default values model a 12AX7 triode with a classic hi-fi / guitar-amp bias
/// (B+ = 250 V, Rp = 100 kΩ, Rk = 1.5 kΩ, no cathode bypass).
struct TubeStageConfig {
    /// Tube model parameters; defaults to 12AX7.
    Circuit::Nonlinear::TubeParams tube;

    double Vcc         = 250.0;  ///< B+ supply voltage (V).
    double Rp          = 100e3;  ///< Plate load resistor (Ω).
    double Rk          = 1.5e3;  ///< Cathode resistor (Ω).
    double Ck          = 0.0;    ///< Cathode bypass capacitance (F); 0 = no bypass.

    /// ±V clamp applied to the grid input before the solve.
    /// Guards against divergence with extreme input; does not affect normal audio levels.
    double inputClampV = 5.0;

    /// Newton-Raphson iteration policy.
    /// Defaults are tuned for tube-stage voltages (unlimited step size; the
    /// input clamp provides the necessary convergence guardrail).
    Circuit::Nonlinear::NRConfig nr;

    /// Default-construct with 12AX7 tube and NR settings appropriate for a
    /// tube-stage (unlimited step size, 20 max iterations).
    TubeStageConfig();
};

// ─────────────────────────────────────────────────────────────────────────────

/// Common-cathode triode gain stage solved per sample via MNA + Newton-Raphson.
///
/// Topology (v1 — no input/output coupling capacitors):
/// @code
///   Vcc ─── Rp ──── Plate (Vp)
///                     │
///                  [Triode]
///                     │
///               Cathode (Vk) ──── Rk ──── GND
///                             (║ Ck, optional bypass cap)
///
///   Grid voltage = scaled + clamped input sample (Vin).
/// @endcode
///
/// The circuit reduces to a 2×2 nonlinear KCL system in the two unknowns
/// Vp (plate) and Vk (cathode), which is solved by Newton-Raphson each sample:
///
///   f1(Vp,Vk) = (Vcc − Vp)/Rp  − Ip(Vp−Vk, Vin−Vk) = 0   [plate KCL]
///   f2(Vp,Vk) = Vk·(1/Rk+Geq)  − Ieq − Ip(…)        = 0   [cathode KCL]
///
/// The previous sample's operating point is used as the NR warm-start, which
/// ensures fast convergence (typically 1–3 iterations) in steady state.
///
/// Per-sample flow:
///   1. Input sample → volts via UnitScaling (±1 full-scale → ±10 V).
///   2. Grid voltage clamped to ±inputClampV for convergence protection.
///   3. 2×2 NR solve → Vp, Vk.
///   4. Plate voltage → normalised output sample via UnitScaling.
class TubeStage {
public:
    explicit TubeStage(TubeStageConfig cfg = {}) noexcept;

    /// Recompute capacitor companion conductances for the given sample rate.
    /// Must be called before the first processSample() and on sample-rate changes.
    void prepare(double sampleRate) noexcept;

    /// Reset state to zero initial conditions (clears capacitor history and
    /// returns the operating-point estimate to its start-up value).
    void reset() noexcept;

    /// Process one audio sample through the triode gain stage.
    /// @param sample  Normalised input (±1.0 full-scale).
    /// @return        Normalised output (plate voltage / kVoltsPerSample).
    [[nodiscard]] float processSample(float sample) noexcept;

private:
    TubeStageConfig cfg_;

    /// Operating-point state vector: x_[0] = Vp (V), x_[1] = Vk (V).
    /// Warm-starts each NR solve; updated to the converged solution each sample.
    std::array<double, 2> x_;

    /// Cathode bypass capacitor companion model (active only when cfg_.Ck > 0).
    Circuit::CapacitorCompanion capK_;

    /// Newton-Raphson iteration manager (constructed once from cfg_.nr).
    Circuit::Nonlinear::NRPolicy<2> nr_;
};

} // namespace Models
