#include "TriodeKoren.h"

#include <algorithm> // std::max
#include <cmath>

namespace Circuit {
namespace Nonlinear {

// ── Tube parameter presets ────────────────────────────────────────────────────

TubeParams TubeParams::tubeParams12AX7() noexcept
{
    // High-gain dual triode — initial approximation from Koren (1996) / GE data sheet.
    // µ ≈ 100, rp ≈ 62 kΩ, Gm ≈ 1.6 mA/V at typical quiescent point.
    return { /*mu=*/100.0, /*kp=*/600.0, /*kvb=*/300.0, /*kg1=*/1060.0, /*x=*/1.4 };
}

TubeParams TubeParams::tubeParams12AU7() noexcept
{
    // Medium-gain dual triode — initial approximation.
    // µ ≈ 21.5, rp ≈ 6.5 kΩ, Gm ≈ 3.3 mA/V at typical quiescent point.
    // kvb = 0 gives a slightly sharper knee, consistent with published models.
    return { /*mu=*/21.5, /*kp=*/84.5, /*kvb=*/0.0, /*kg1=*/1180.0, /*x=*/1.3 };
}

TubeParams TubeParams::tubeParams6072() noexcept
{
    // Low-noise dual triode (GE / Sylvania 6072 / M8137 equivalent).
    // µ ≈ 70, rp ≈ 38 kΩ, Gm ≈ 1.85 mA/V.
    // These are best-available approximations from published data sheets;
    // measured characterisation is recommended for production use.
    return { /*mu=*/70.0, /*kp=*/300.0, /*kvb=*/300.0, /*kg1=*/1060.0, /*x=*/1.4 };
}

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

/// Threshold above which softplus(u) ≈ u (relative error < 1e-13).
static constexpr double kSoftplusUpperThreshold = 30.0;

/// Threshold below which softplus(u) ≈ exp(u) (relative error < 1e-13).
static constexpr double kSoftplusLowerThreshold = -30.0;

/// Minimum value for V2 = kvb + Vpk² to prevent division by zero.
/// At this floor, sqV2 ≈ 1 µV — well below any meaningful circuit voltage.
static constexpr double kMinV2 = 1e-12;

/// Numerically stable softplus: ln(1 + exp(u)).
/// For large |u| the naive form overflows or loses precision.
inline double softplus(double u) noexcept
{
    if (u >  kSoftplusUpperThreshold) return u;          // ln(1 + exp(u)) ≈ u
    if (u <  kSoftplusLowerThreshold) return std::exp(u);// ln(1 + exp(u)) ≈ exp(u)
    return std::log1p(std::exp(u));
}

/// Numerically stable logistic sigmoid: exp(u) / (1 + exp(u)).
inline double sigmoid(double u) noexcept
{
    if (u >= 0.0) return 1.0 / (1.0 + std::exp(-u));
    const double eu = std::exp(u);
    return eu / (1.0 + eu);
}

/// Compute the shared intermediate quantities used by Ip and its derivatives.
///
/// @param Vpk   Plate-to-cathode voltage (V).
/// @param Vgk   Grid-to-cathode voltage (V).
/// @param p     Tube parameters.
/// @param[out] E1   Control voltage; Ip = max(E1,0)^x / kg1.
/// @param[out] sig  sigmoid(kp * (1/mu + Vgk / sqrt(V2))), used for ∂E1.
/// @param[out] sqV2 sqrt(kvb + Vpk²), clamped to avoid division by zero.
void korenIntermediates(double Vpk, double Vgk, const TubeParams& p,
                        double& E1, double& sig, double& sqV2) noexcept
{
    // kvb + Vpk² is clamped to kMinV2 so that sqV2 is always
    // well-defined (guards against kvb=0 and Vpk=0).
    const double V2 = std::max(p.kvb + Vpk * Vpk, kMinV2);
    sqV2 = std::sqrt(V2);

    const double u = p.kp * (1.0 / p.mu + Vgk / sqV2);
    sig = sigmoid(u);
    E1  = (Vpk / p.kp) * softplus(u);
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

double triodeIp(double Vpk, double Vgk, const TubeParams& p) noexcept
{
    double E1, sig, sqV2;
    korenIntermediates(Vpk, Vgk, p, E1, sig, sqV2);

    if (E1 <= 0.0) return 0.0;
    return std::pow(E1, p.x) / p.kg1;
}

double triodeDIpDVpk(double Vpk, double Vgk, const TubeParams& p) noexcept
{
    double E1, sig, sqV2;
    korenIntermediates(Vpk, Vgk, p, E1, sig, sqV2);

    if (E1 <= 0.0) return 0.0;

    // ∂E1/∂Vpk  =  softplus(u)/kp  −  Vpk² · Vgk · sig / V2^(3/2)
    //           =  E1/Vpk  −  Vpk² · Vgk · sig / (sqV2³)
    //
    // (The first term E1/Vpk is shorthand for softplus(u)/kp when Vpk ≠ 0.
    //  The fallback form avoids a division by Vpk.)
    const double V2        = sqV2 * sqV2;
    const double sp_over_kp = (Vpk != 0.0) ? (E1 / Vpk)
                                             : softplus(p.kp * (1.0 / p.mu + Vgk / sqV2)) / p.kp;
    const double dE1_dVpk = sp_over_kp - Vpk * Vpk * Vgk * sig / (V2 * sqV2);

    // ∂Ip/∂Vpk = x · E1^(x−1) / kg1 · ∂E1/∂Vpk
    return p.x * std::pow(E1, p.x - 1.0) / p.kg1 * dE1_dVpk;
}

double triodeDIpDVgk(double Vpk, double Vgk, const TubeParams& p) noexcept
{
    double E1, sig, sqV2;
    korenIntermediates(Vpk, Vgk, p, E1, sig, sqV2);

    if (E1 <= 0.0) return 0.0;

    // ∂E1/∂Vgk = Vpk · sig / sqV2
    const double dE1_dVgk = Vpk * sig / sqV2;

    // ∂Ip/∂Vgk = x · E1^(x−1) / kg1 · ∂E1/∂Vgk
    return p.x * std::pow(E1, p.x - 1.0) / p.kg1 * dE1_dVgk;
}

} // namespace Nonlinear
} // namespace Circuit
