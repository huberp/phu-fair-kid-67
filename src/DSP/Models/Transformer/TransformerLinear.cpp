#include "TransformerLinear.h"

#include <algorithm>
#include <cmath>

namespace Models {

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

/// Compute first-order HPF bilinear-transform coefficients.
///
/// Analog prototype: H(s) = s / (s + ω₀),  ω₀ = 2π·fc.
/// Bilinear transform with frequency pre-warping:
///   K = tan(π·fc / fs)
///   b0 =  1 / (1 + K),  b1 = −1 / (1 + K),  a1 = (1 − K) / (1 + K)
///
/// Recurrence: y[n] = b0·x[n] + b1·x[n−1] + a1·y[n−1]
///
/// The −3 dB point is exact at fc (pre-warped).
void computeHPFCoeffs(double fc, double fs,
                      float& b0, float& b1, float& a1) noexcept
{
    const double K  = std::tan(M_PI * fc / fs);
    const double norm = 1.0 / (1.0 + K);
    b0 = static_cast<float>( norm);
    b1 = static_cast<float>(-norm);
    a1 = static_cast<float>((1.0 - K) * norm);
}

/// Compute first-order LPF bilinear-transform coefficients.
///
/// Analog prototype: H(s) = ω₀ / (s + ω₀),  ω₀ = 2π·fc.
/// Bilinear transform with frequency pre-warping:
///   K = tan(π·fc / fs)
///   b0 = K / (1 + K),  b1 = K / (1 + K),  a1 = (1 − K) / (1 + K)
///
/// Recurrence: y[n] = b0·x[n] + b1·x[n−1] + a1·y[n−1]
///
/// The −3 dB point is exact at fc (pre-warped).
void computeLPFCoeffs(double fc, double fs,
                      float& b0, float& b1, float& a1) noexcept
{
    const double K    = std::tan(M_PI * fc / fs);
    const double norm = 1.0 / (1.0 + K);
    b0 = static_cast<float>(K * norm);
    b1 = static_cast<float>(K * norm);
    a1 = static_cast<float>((1.0 - K) * norm);
}

/// Memoryless tanh soft-clip saturator with unity small-signal gain.
///
/// Uses the identity  tanh(k·x) / k  to preserve the slope at x = 0
/// (derivative = 1) regardless of the drive parameter k.
///
/// @param x     Input sample.
/// @param drive Drive >= 1.0.  1 = linear, higher = stronger saturation.
/// @return      Saturated sample with unaffected small-signal gain.
inline float saturate(float x, float drive) noexcept
{
    // drive is guaranteed >= 1.0 at the call site.
    return std::tanh(drive * x) / drive;
}

} // anonymous namespace

// ── TransformerLinear ─────────────────────────────────────────────────────────

TransformerLinear::TransformerLinear(TransformerLinearConfig cfg) noexcept
    : cfg_(cfg)
{
}

void TransformerLinear::prepare(double sampleRate) noexcept
{
    // Clamp cutoffs to a safe range to avoid degenerate coefficients.
    // (Near-zero fc → K → 0 → all-pass / identity; near-Nyquist → K → ∞ →
    // degenerate pole.)  The chosen bounds are well within any audio use case.
    const double nyquist    = sampleRate * 0.5;
    const double safeHPF    = std::clamp(cfg_.hpfCutoffHz, 1.0, nyquist * 0.49);
    // Ensure LPF lower bound doesn't exceed the Nyquist limit to keep the clamp valid.
    const double lpfMin     = std::min(safeHPF + 1.0, nyquist * 0.49);
    const double safeLPF    = std::clamp(cfg_.lpfCutoffHz, lpfMin, nyquist * 0.49);

    computeHPFCoeffs(safeHPF, sampleRate, hpfB0_, hpfB1_, hpfA1_);
    computeLPFCoeffs(safeLPF, sampleRate, lpfB0_, lpfB1_, lpfA1_);

    reset();
}

void TransformerLinear::reset() noexcept
{
    hpfX1_ = hpfY1_ = 0.0f;
    lpfX1_ = lpfY1_ = 0.0f;
}

void TransformerLinear::setConfig(TransformerLinearConfig cfg) noexcept
{
    cfg_ = cfg;
}

float TransformerLinear::processSample(float sample) noexcept
{
    // 1. HPF — rolls off low frequencies below hpfCutoffHz.
    const float hpfOut = hpfB0_ * sample  + hpfB1_ * hpfX1_
                       + hpfA1_ * hpfY1_;
    hpfX1_ = sample;
    hpfY1_ = hpfOut;

    // 2. LPF — rolls off high frequencies above lpfCutoffHz.
    const float lpfOut = lpfB0_ * hpfOut + lpfB1_ * lpfX1_
                       + lpfA1_ * lpfY1_;
    lpfX1_ = hpfOut;
    lpfY1_ = lpfOut;

    // 3. Saturator — memoryless tanh soft-clip with unity small-signal gain.
    const float drive = std::max(1.0f, cfg_.drive);
    return saturate(lpfOut, drive);
}

} // namespace Models
