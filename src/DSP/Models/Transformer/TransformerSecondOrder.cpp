#include "TransformerSecondOrder.h"

#include <algorithm>
#include <cmath>

#define _USE_MATH_DEFINES
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Models {

// ── Coefficient helpers ───────────────────────────────────────────────────────

namespace {

/// Compute 2nd-order HPF biquad coefficients (bilinear transform, pre-warped).
///
/// Analog prototype: H(s) = s² / (s² + (ω₀/Q)·s + ω₀²),  ω₀ = 2π·fc.
///
/// With K = tan(π·fc / fs):
///   denom = 1 + K/Q + K²
///   b0 =  1 / denom
///   b1 = −2 / denom
///   b2 =  1 / denom
///   a1 = 2·(K²−1) / denom     (coefficient for −a1·y[n−1] in recurrence)
///   a2 = (1 − K/Q + K²) / denom
///
/// Stability: for Q=0.7071 and any fc ∈ (0, Nyquist/2) both poles are within
/// the unit circle.
void computeHPFBiquad(double fc, double fs, double Q,
                      float& b0, float& b1, float& b2,
                      float& a1, float& a2) noexcept
{
    const double K     = std::tan(M_PI * fc / fs);
    const double K2    = K * K;
    const double KoverQ = K / Q;
    const double denom = 1.0 + KoverQ + K2;
    const double norm  = 1.0 / denom;

    b0 = static_cast<float>(norm);
    b1 = static_cast<float>(-2.0 * norm);
    b2 = static_cast<float>(norm);
    a1 = static_cast<float>(2.0 * (K2 - 1.0) * norm);
    a2 = static_cast<float>((1.0 - KoverQ + K2) * norm);
}

/// Compute 2nd-order LPF biquad coefficients (bilinear transform, pre-warped).
///
/// Analog prototype: H(s) = ω₀² / (s² + (ω₀/Q)·s + ω₀²).
///
/// With K = tan(π·fc / fs):
///   denom = 1 + K/Q + K²
///   b0 = K² / denom
///   b1 = 2·K² / denom
///   b2 = K² / denom
///   a1 = 2·(K²−1) / denom     (identical to HPF a1/a2)
///   a2 = (1 − K/Q + K²) / denom
void computeLPFBiquad(double fc, double fs, double Q,
                      float& b0, float& b1, float& b2,
                      float& a1, float& a2) noexcept
{
    const double K     = std::tan(M_PI * fc / fs);
    const double K2    = K * K;
    const double KoverQ = K / Q;
    const double denom = 1.0 + KoverQ + K2;
    const double norm  = 1.0 / denom;

    b0 = static_cast<float>(K2 * norm);
    b1 = static_cast<float>(2.0 * K2 * norm);
    b2 = static_cast<float>(K2 * norm);
    a1 = static_cast<float>(2.0 * (K2 - 1.0) * norm);
    a2 = static_cast<float>((1.0 - KoverQ + K2) * norm);
}

/// Memoryless tanh saturator with unity small-signal gain (see TransformerLinear).
inline float saturate(float x, float drive) noexcept
{
    return std::tanh(drive * x) / drive;
}

} // anonymous namespace

// ── TransformerSecondOrder ────────────────────────────────────────────────────

TransformerSecondOrder::TransformerSecondOrder(TransformerSecondOrderConfig cfg) noexcept
    : cfg_(cfg)
{
}

void TransformerSecondOrder::prepare(double sampleRate) noexcept
{
    // Clamp cutoffs to a safe range (same guard as TransformerLinear).
    const double nyquist = sampleRate * 0.5;
    const double safeHPF = std::clamp(cfg_.hpfCutoffHz, 1.0, nyquist * 0.49);
    const double lpfMin  = std::min(safeHPF + 1.0, nyquist * 0.49);
    const double safeLPF = std::clamp(cfg_.lpfCutoffHz, lpfMin, nyquist * 0.49);

    const double safeHPFQ = std::max(static_cast<double>(cfg_.hpfQ), 0.1);
    const double safeLPFQ = std::max(static_cast<double>(cfg_.lpfQ), 0.1);

    computeHPFBiquad(safeHPF, sampleRate, safeHPFQ,
                     hpfB0_, hpfB1_, hpfB2_, hpfA1_, hpfA2_);
    computeLPFBiquad(safeLPF, sampleRate, safeLPFQ,
                     lpfB0_, lpfB1_, lpfB2_, lpfA1_, lpfA2_);
    reset();
}

void TransformerSecondOrder::reset() noexcept
{
    hpfX1_ = hpfX2_ = hpfY1_ = hpfY2_ = 0.0f;
    lpfX1_ = lpfX2_ = lpfY1_ = lpfY2_ = 0.0f;
}

void TransformerSecondOrder::setConfig(TransformerSecondOrderConfig cfg) noexcept
{
    cfg_ = cfg;
}

float TransformerSecondOrder::processSample(float sample) noexcept
{
    // 1. HPF biquad (Direct Form I, negative-feedback convention).
    const float hpfOut = hpfB0_ * sample  + hpfB1_ * hpfX1_ + hpfB2_ * hpfX2_
                       - hpfA1_ * hpfY1_  - hpfA2_ * hpfY2_;
    hpfX2_ = hpfX1_; hpfX1_ = sample;
    hpfY2_ = hpfY1_; hpfY1_ = hpfOut;

    // 2. LPF biquad.
    const float lpfOut = lpfB0_ * hpfOut  + lpfB1_ * lpfX1_ + lpfB2_ * lpfX2_
                       - lpfA1_ * lpfY1_  - lpfA2_ * lpfY2_;
    lpfX2_ = lpfX1_; lpfX1_ = hpfOut;
    lpfY2_ = lpfY1_; lpfY1_ = lpfOut;

    // 3. Saturator — same memoryless tanh as TransformerLinear.
    const float drive = std::max(1.0f, cfg_.drive);
    return saturate(lpfOut, drive);
}

} // namespace Models
