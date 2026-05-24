#include "VariableMuPushPullStage.h"

namespace Models {

VariableMuPushPullStage::VariableMuPushPullStage(
    Analog::Models::VariableMuStageConfig cfg) noexcept
    : stageA_(cfg), stageB_(cfg)
{
}

void VariableMuPushPullStage::prepare(double sampleRate) noexcept
{
    stageA_.prepare(sampleRate);
    stageB_.prepare(sampleRate);
}

void VariableMuPushPullStage::reset() noexcept
{
    stageA_.reset();
    stageB_.reset();
}

void VariableMuPushPullStage::setCv(float cv) noexcept
{
    stageA_.setCv(cv);
    stageB_.setCv(cv);
}

float VariableMuPushPullStage::cv() const noexcept
{
    return stageA_.cv();
}

void VariableMuPushPullStage::setNRConfig(Analog::Nonlinear::NRConfig cfg) noexcept
{
    stageA_.setNRConfig(cfg);
    stageB_.setNRConfig(cfg);
}

void VariableMuPushPullStage::setCathodeBypassCapacitance(double farads) noexcept
{
    stageA_.setCathodeBypassCapacitance(farads);
    stageB_.setCathodeBypassCapacitance(farads);
}

float VariableMuPushPullStage::processSample(float sample) noexcept
{
    // Arm A: non-inverting half (+Vin).
    // Arm B: inverting half (−Vin).
    //
    // Each VariableMuStage already applies phase-inversion correction and
    // normalises to unity small-signal gain at CV = 0.  Therefore:
    //   stageA.processSample(+x) ≈ +x   (for small signals at CV = 0)
    //   stageB.processSample(−x) ≈ −x
    //
    // Differential combination:
    //   (outA − outB) / 2 = (x − (−x)) / 2 = x   ✓  (unity gain)
    //
    // Even-order distortion terms (a₂·x², a₄·x⁴, …) are identical in both
    // arms and cancel in the subtraction.  Only odd-order terms survive.
    const float outA = stageA_.processSample( sample);
    const float outB = stageB_.processSample(-sample);
    return (outA - outB) * 0.5f;
}

} // namespace Models
