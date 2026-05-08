#include "VariableMuStage.h"

#include "../../UnitScaling.h"

#include <algorithm>
#include <cmath>

namespace Models {

// ── VariableMuStageConfig ─────────────────────────────────────────────────────

VariableMuStageConfig::VariableMuStageConfig()
    : tube(Circuit::Nonlinear::TubeParams::tubeParams6072())
{
    // Unlimited per-element step size: the input clamp (inputClampV) provides
    // the necessary convergence guardrail, so clamping individual NR steps adds
    // no extra safety while impeding convergence from cold-start conditions.
    nr.maxIterations = 20;
    nr.maxDeltaV     = 0.0; // ≤ 0 → unlimited
}

// ── VariableMuStage ───────────────────────────────────────────────────────────

VariableMuStage::VariableMuStage(VariableMuStageConfig cfg) noexcept
    : cfg_(std::move(cfg)), nr_(cfg_.nr)
{
    // Start-up estimate: plate near the midpoint of the supply rail, cathode
    // at a typical self-bias voltage.  NR will move this to the true quiescent
    // point on the first few samples.
    x_[0] = cfg_.Vcc * 0.5;
    x_[1] = 1.5;

    capK_.farads = cfg_.Ck;
}

void VariableMuStage::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;

    // DC-blocking HPF coefficient: R = 1 - 2π·fc/fs, fc ≈ 10 Hz.
    // Models the output coupling capacitor that blocks the large plate-voltage
    // DC offset (~150–200 V) present in any triode common-cathode stage.
    // Using 10 Hz preserves all audible content while tracking slow CV-driven
    // shifts in the quiescent operating point during compression.
    constexpr double kDcBlockHz = 10.0;
    constexpr double kTwoPi     = 6.283185307179586;
    dcBlockCoeff_ = std::clamp(1.0 - kTwoPi * kDcBlockHz / sampleRate, 0.0, 1.0);

    // Pre-compute the quiescent operating point (Vin=0, cv=0) via Newton-Raphson.
    // This serves two purposes:
    //  1. Initialise the DC blocker to the quiescent plate voltage so it
    //     removes DC from sample 0 instead of needing ~1 s to settle.
    //  2. Provide a perfect warm-start for the NR solver in reset().
    {
        using namespace Circuit::Nonlinear;
        std::array<double, 2> xQ = {cfg_.Vcc * 0.5, 1.5};
        const double invRp = 1.0 / cfg_.Rp;
        const double invRk = 1.0 / cfg_.Rk;
        const auto& tube   = cfg_.tube;

        for (int iter = 0; iter < 200; ++iter) {
            const double Vp  = xQ[0], Vk = xQ[1];
            const double Vpk = Vp - Vk;
            const double Vgk = -Vk; // Vin = 0, cv = 0

            double Ip, gds, gm;
            triodeIpAndPartials(Vpk, Vgk, tube, Ip, gds, gm);

            const double f1 = (cfg_.Vcc - Vp) * invRp - Ip;
            const double f2 = Vk * invRk - Ip; // steady-state: no capacitor current

            const double J00 = -invRp - gds;
            const double J01 =  gds + gm;
            const double J10 = -gds;
            const double J11 =  invRk + gds + gm;

            const double det = J00 * J11 - J01 * J10;
            if (std::abs(det) < 1e-30) break;

            const double inv = 1.0 / det;
            const double dVp = (-f1 * J11 + f2 * J01) * inv;
            const double dVk = ( f1 * J10 - f2 * J00) * inv;
            xQ[0] += dVp;
            xQ[1] += dVk;

            if (std::sqrt(dVp * dVp + dVk * dVk)
                    < 1e-9 * (std::sqrt(xQ[0] * xQ[0] + xQ[1] * xQ[1]) + 1.0))
                break;
        }

        xQp_ = xQ[0];
        xQk_ = xQ[1];
        Vp_quiescent_norm_ = static_cast<double>(
            UnitScaling::voltsToSample(static_cast<float>(xQp_)));

        // Quiescent small-signal gain Av = dVp/dVin (always negative — inverting).
        // Derivation: Vin enters Vgk = Vin − Vk.  Sensitivity from the Jacobian:
        //   J·[dVp; dVk] = [gm; gm]  (both residuals have ∂/∂Vin = gm)
        //   Cramer: dVp = gm*(J11 − J01)/det = gm*invRk/det
        double Ip_q, gds_q, gm_q;
        triodeIpAndPartials(xQp_ - xQk_, -xQk_, tube, Ip_q, gds_q, gm_q);
        const double J00q = -invRp - gds_q;
        const double J01q =  gds_q + gm_q;
        const double J10q = -gds_q;
        const double J11q =  invRk + gds_q + gm_q;
        const double detQ = J00q * J11q - J01q * J10q;
        const double Av   = (std::abs(detQ) > 1e-30) ? gm_q * invRk / detQ : -1.0;
        invGainMag_ = (std::abs(Av) > 1e-10) ? 1.0 / std::abs(Av) : 1.0;
    }

    // Warm-start NR from the true quiescent point.
    x_[0] = xQp_;
    x_[1] = xQk_;
    updateCathodeBypassCompanion(xQk_);

    // Pre-charge the DC blocker so first-sample output is zero (no startup transient).
    dcBlockX1_ = Vp_quiescent_norm_;
    dcBlockY1_ = 0.0;
}

void VariableMuStage::reset() noexcept
{
    // Warm-start NR from the true quiescent (known after prepare()),
    // falling back to the cold-start estimate if prepare() hasn't run yet.
    x_[0]   = (xQp_ > 0.0) ? xQp_ : cfg_.Vcc * 0.5;
    x_[1]   = xQk_;
    cvBias_ = 0.0;
    updateCathodeBypassCompanion(x_[1]);
    // Pre-charge the DC blocker to the quiescent plate voltage so reset()
    // also produces zero output on the first sample (no transient).
    dcBlockX1_ = Vp_quiescent_norm_;
    dcBlockY1_ = 0.0;
}

void VariableMuStage::setCv(float cv) noexcept
{
    // Clamp to [0, cvMaxV]: negative CV would increase gain beyond the
    // quiescent bias point and could destabilise the NR solver; values above
    // cvMaxV risk driving the tube into deep cut-off where the Jacobian
    // becomes near-singular.
    cvBias_ = std::clamp(static_cast<double>(cv), 0.0, cfg_.cvMaxV);
}

void VariableMuStage::setNRConfig(Circuit::Nonlinear::NRConfig cfg) noexcept
{
    nr_.setConfig(std::move(cfg));
}

void VariableMuStage::setCathodeBypassCapacitance(double farads) noexcept
{
    cfg_.Ck = std::max(0.0, farads);
    updateCathodeBypassCompanion(x_[1]);
}

// ── Per-sample processing ─────────────────────────────────────────────────────

float VariableMuStage::processSample(float sample) noexcept
{
    using namespace Circuit::Nonlinear;

    // 1. Scale input sample to grid voltage; clamp for convergence protection.
    double Vin = static_cast<double>(UnitScaling::sampleToVolts(sample));
    Vin = std::clamp(Vin, -cfg_.inputClampV, cfg_.inputClampV);

    // Apply CV bias: shift the effective grid voltage more negative.
    // vinBiased is the grid-to-ground voltage seen by the stage;
    // Vgk = vinBiased − Vk = (Vin − cvBias_) − Vk.
    const double vinBiased = Vin - cvBias_;

    const double Vcc   = cfg_.Vcc;
    const double invRp = 1.0 / cfg_.Rp;
    const double invRk = 1.0 / cfg_.Rk;
    const auto&  tube  = cfg_.tube;

    // Cathode bypass capacitor companion terms (zero when Ck is not fitted).
    const double Geq_k = (cfg_.Ck > 0.0) ? capK_.Geq : 0.0;
    const double Ieq_k = (cfg_.Ck > 0.0) ? capK_.Ieq : 0.0;

    // 2. Newton-Raphson solve for x_[0] = Vp, x_[1] = Vk.
    //
    // KCL at plate:   (Vcc − Vp)/Rp − Ip(Vpk, Vgk) = 0
    // KCL at cathode: Vk·(1/Rk + Geq_k) − Ieq_k − Ip(Vpk, Vgk) = 0
    //
    // where Vpk = Vp − Vk,  Vgk = vinBiased − Vk.
    //
    // The Jacobian is identical to TubeStage because cvBias_ is constant
    // within a sample (∂Vgk/∂Vk = −1, same as without the CV offset):
    //
    //   J = [[ −1/Rp − gds,       gds + gm ],
    //        [       −gds,   1/Rk + Geq_k + gds + gm ]]
    //
    // Newton step Δ = −J⁻¹·f(x) solved via Cramer's rule.
    auto stepFn = [&](std::array<double, 2>& x) -> bool {
        const double Vp  = x[0];
        const double Vk  = x[1];
        const double Vpk = Vp - Vk;
        const double Vgk = vinBiased - Vk;

        double Ip, gds, gm;
        triodeIpAndPartials(Vpk, Vgk, tube, Ip, gds, gm);

        // Residuals.
        const double f1 = (Vcc - Vp) * invRp - Ip;
        const double f2 = Vk * (invRk + Geq_k) - Ieq_k - Ip;

        // Jacobian entries.
        const double J00 = -invRp - gds;
        const double J01 =  gds + gm;
        const double J10 = -gds;
        const double J11 =  invRk + Geq_k + gds + gm;

        const double det = J00 * J11 - J01 * J10;
        if (std::abs(det) < 1e-30)
            return false; // near-singular Jacobian — trigger NR fallback

        // Newton step via Cramer's rule: Δ = −J⁻¹ · f
        const double inv = 1.0 / det;
        x[0] = Vp + (-f1 * J11 + f2 * J01) * inv;
        x[1] = Vk + ( f1 * J10 - f2 * J00) * inv;
        return true;
    };

    nr_.solve(x_, stepFn);

    // 3. Update cathode bypass capacitor state for the next sample.
    if (cfg_.Ck > 0.0)
        capK_.update(x_[1]);

    // 4. DC-block the plate voltage to remove the quiescent operating-point
    //    offset (models the output coupling capacitor in the real circuit).
    //    y[n] = x[n] - x[n-1] + R*y[n-1]  (first-order HPF)
    const double rawOut = UnitScaling::voltsToSample(static_cast<float>(x_[0]));
    const double dcY    = rawOut - dcBlockX1_ + dcBlockCoeff_ * dcBlockY1_;
    dcBlockX1_ = rawOut;
    dcBlockY1_ = dcY;

    // 5. Normalise to unity small-signal gain and correct the common-cathode
    //    phase inversion.  Negation flips -180° → 0° so wet and dry are in
    //    phase at all mix values.  invGainMag_ = 1/|Av_quiescent| scales the
    //    output so CV=0 → unity gain, higher CV → gain < 1 (compression).
    return static_cast<float>(-invGainMag_ * dcY);
}

void VariableMuStage::updateCathodeBypassCompanion(double cathodeVoltage) noexcept
{
    capK_.farads = cfg_.Ck;

    if (cfg_.Ck <= 0.0 || sampleRate_ <= 0.0) {
        capK_.Geq = 0.0;
        capK_.Ieq = 0.0;
        return;
    }

    capK_.prepare(1.0 / sampleRate_);
    // Preserve the current cathode DC operating point with zero instantaneous
    // capacitor current so runtime parameter changes do not inject a large step.
    capK_.Ieq = capK_.Geq * cathodeVoltage;
}

} // namespace Models
