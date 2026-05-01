#pragma once

namespace Circuit {

/// Trapezoidal (bilinear) companion model for a single capacitor.
///
/// Using the bilinear transform approximation:
///   I_C[k] = Geq · V_C[k] − Ieq
/// where
///   Geq      = 2·C / T          (companion conductance, constant for a given sample rate)
///   Ieq      = Geq·V_C[k−1] + I_C[k−1]  (companion current source, updated each step)
///
/// Ieq obeys the reflection formula:
///   Ieq_next = 2·Geq·V_C[k] − Ieq_old
///
/// The Geq term is stamped into the MNA A matrix (once per prepare()).
/// The Ieq term is added to the MNA z vector at every sample (in solve()).
struct CapacitorCompanion {
    double farads = 0.0;
    double Geq    = 0.0;  ///< Companion conductance; depends on sample rate.
    double Ieq    = 0.0;  ///< Current companion current-source value.

    /// Recompute Geq from capacitance and sample period T = 1/sampleRate.
    void prepare(double T) noexcept
    {
        Geq = 2.0 * farads / T;
    }

    /// Reset history to zero initial conditions.
    void reset() noexcept
    {
        Ieq = 0.0;
    }

    /// Update the companion model after the system has been solved for V_C[k].
    /// @param Vc  Capacitor voltage V_C[k] = V_nodeP − V_nodeN.
    void update(double Vc) noexcept
    {
        Ieq = 2.0 * Geq * Vc - Ieq;
    }
};

} // namespace Circuit
