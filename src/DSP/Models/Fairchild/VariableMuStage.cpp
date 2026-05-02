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
    x_.resize(2);
    // Start-up estimate: plate near the midpoint of the supply rail, cathode
    // at a typical self-bias voltage.  NR will move this to the true quiescent
    // point on the first few samples.
    x_[0] = cfg_.Vcc * 0.5;
    x_[1] = 1.5;

    capK_.farads = cfg_.Ck;
}

void VariableMuStage::prepare(double sampleRate) noexcept
{
    if (cfg_.Ck > 0.0) {
        const double T = 1.0 / sampleRate;
        capK_.prepare(T);
    }
}

void VariableMuStage::reset() noexcept
{
    x_[0]   = cfg_.Vcc * 0.5;
    x_[1]   = 1.5;
    cvBias_ = 0.0;
    capK_.reset();
}

void VariableMuStage::setCv(float cv) noexcept
{
    // Clamp to [0, cvMaxV]: negative CV would increase gain beyond the
    // quiescent bias point and could destabilise the NR solver; values above
    // cvMaxV risk driving the tube into deep cut-off where the Jacobian
    // becomes near-singular.
    cvBias_ = std::clamp(static_cast<double>(cv), 0.0, cfg_.cvMaxV);
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
    auto stepFn = [&](std::vector<double>& x) -> bool {
        const double Vp  = x[0];
        const double Vk  = x[1];
        const double Vpk = Vp - Vk;
        const double Vgk = vinBiased - Vk;

        const double Ip  = triodeIp(Vpk, Vgk, tube);
        const double gds = triodeDIpDVpk(Vpk, Vgk, tube);
        const double gm  = triodeDIpDVgk(Vpk, Vgk, tube);

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

    // 4. Return the plate voltage scaled back to normalised sample units.
    return UnitScaling::voltsToSample(static_cast<float>(x_[0]));
}

} // namespace Models
