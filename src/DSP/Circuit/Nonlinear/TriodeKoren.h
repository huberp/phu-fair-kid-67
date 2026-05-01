#pragma once

#include <cmath>

namespace Circuit {
namespace Nonlinear {

/// Parameters for the Koren triode model.
///
/// All values are dimensionless or carry the units noted in the field comments.
/// The preset factories below provide initial approximations for common tubes;
/// they are documented as starting points and should be tuned to measurements.
///
/// Reference: Norman L. Koren, "Improved Vacuum-Tube Models for SPICE", 1996.
struct TubeParams {
    double mu;   ///< Amplification factor (dimensionless).
    double kp;   ///< Geometry / compression parameter (dimensionless).
    double kvb;  ///< Knee-shaping parameter (V²). Use a small positive value
                 ///< (e.g. 300) for smooth knee; 0 yields a sharper knee.
    double kg1;  ///< Plate-resistance / gain scaling factor (dimensionless).
    double x;    ///< Characteristic exponent (typically ~1.4 for most triodes).

    /// 12AX7 — high-gain dual triode (µ ≈ 100).
    /// Initial approximation; suitable for gain-stage modelling.
    static TubeParams tubeParams12AX7() noexcept;

    /// 12AU7 — medium-gain dual triode (µ ≈ 21.5).
    /// Initial approximation; suitable for buffer / cathode-follower stages.
    static TubeParams tubeParams12AU7() noexcept;

    /// 6072 — low-noise dual triode (GE/Sylvania, µ ≈ 70; lower gain than 12AX7).
    /// Initial approximation; best available values from published data sheets.
    static TubeParams tubeParams6072() noexcept;
};

// ── Koren triode functions ────────────────────────────────────────────────────

/// Compute the plate current of a triode using the Koren model.
///
/// The Koren triode model is defined as:
/// @code
///   V2  = kvb + Vpk²
///   E1  = (Vpk / kp) · ln(1 + exp(kp · (1/µ + Vgk / √V2)))
///   Ip  = max(E1, 0)^x / kg1
/// @endcode
///
/// The soft-clipped inner exponential provides a smooth transition between
/// the cut-off and conduction regions, while the max(…, 0) ensures the current
/// never goes negative.
///
/// Typical operating voltages for small-signal triodes:
///   Vpk ∈ [0 V … 300 V],  Vgk ∈ [−3 V … 0 V].
///
/// @param Vpk  Plate-to-cathode voltage (V).  Must be ≥ 0 for physical operation.
/// @param Vgk  Grid-to-cathode voltage (V).
/// @param p    Tube parameters.
/// @return     Plate current Ip (A), always ≥ 0.
double triodeIp(double Vpk, double Vgk, const TubeParams& p) noexcept;

/// Analytical partial derivative ∂Ip/∂Vpk.
///
/// Returns 0 when the tube is cut off (E1 ≤ 0).
///
/// @param Vpk  Plate-to-cathode voltage (V).
/// @param Vgk  Grid-to-cathode voltage (V).
/// @param p    Tube parameters.
/// @return     ∂Ip/∂Vpk (S — amperes per volt).
double triodeDIpDVpk(double Vpk, double Vgk, const TubeParams& p) noexcept;

/// Analytical partial derivative ∂Ip/∂Vgk.
///
/// Returns 0 when the tube is cut off (E1 ≤ 0).
///
/// @param Vpk  Plate-to-cathode voltage (V).
/// @param Vgk  Grid-to-cathode voltage (V).
/// @param p    Tube parameters.
/// @return     ∂Ip/∂Vgk (S — amperes per volt).
double triodeDIpDVgk(double Vpk, double Vgk, const TubeParams& p) noexcept;

} // namespace Nonlinear
} // namespace Circuit
