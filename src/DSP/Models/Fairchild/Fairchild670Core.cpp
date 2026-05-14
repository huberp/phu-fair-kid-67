#include "Fairchild670Core.h"

#include "analog/dsp/UnitScaling.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kCvClampEpsilon = 1.0e-5f;

float softLimitCv(const float cv,
                  const float cvMax,
                  const float softKneeWidth) noexcept
{
    if (cvMax <= 0.0f)
        return 0.0f;

    const float knee = std::clamp(softKneeWidth, 0.0f, cvMax);
    if (knee <= 0.0f)
        return std::clamp(cv, 0.0f, cvMax);

    const float clampedCv = std::max(0.0f, cv);
    const float kneeStart = cvMax - knee;
    if (clampedCv <= kneeStart)
        return clampedCv;
    if (clampedCv >= cvMax)
        return cvMax;

    // Cubic Hermite segment with unit slope at knee start and zero slope at cvMax.
    const float t = (clampedCv - kneeStart) / knee;
    const float p = t + t * t - t * t * t;
    return kneeStart + knee * p;
}
}

namespace Models {

// ── Constructor ───────────────────────────────────────────────────────────────

Fairchild670Core::Fairchild670Core(Fairchild670CoreConfig cfg) noexcept
    : cfg_(std::move(cfg))
    , preampL_(cfg_.preampCfg)
    , preampR_(cfg_.preampCfg)
    , stageL_(cfg_.stageCfg)
    , stageR_(cfg_.stageCfg)
    , inputTransformerL_(cfg_.inputTransformerCfg)
    , inputTransformerR_(cfg_.inputTransformerCfg)
    , interstageTransformerL_(cfg_.interstageTransformerCfg)
    , interstageTransformerR_(cfg_.interstageTransformerCfg)
    , transformerL_(cfg_.transformerCfg)
    , transformerR_(cfg_.transformerCfg)
    , detectorL_(Sidechain::toDetectorConfig(cfg_.timingPreset),
                 cfg_.tubeRectifierForwardVoltageV)
    , detectorR_(Sidechain::toDetectorConfig(cfg_.timingPreset),
                 cfg_.tubeRectifierForwardVoltageV)
{}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Fairchild670Core::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    preampL_.prepare(sampleRate);
    preampR_.prepare(sampleRate);
    stageL_.prepare(sampleRate);
    stageR_.prepare(sampleRate);
    inputTransformerL_.prepare(sampleRate);
    inputTransformerR_.prepare(sampleRate);
    interstageTransformerL_.prepare(sampleRate);
    interstageTransformerR_.prepare(sampleRate);
    transformerL_.prepare(sampleRate);
    transformerR_.prepare(sampleRate);
    detectorL_.prepare(sampleRate);
    detectorR_.prepare(sampleRate);
    resetPeakMeters();
}

void Fairchild670Core::reset() noexcept
{
    preampL_.reset();
    preampR_.reset();
    stageL_.reset();
    stageR_.reset();
    inputTransformerL_.reset();
    inputTransformerR_.reset();
    interstageTransformerL_.reset();
    interstageTransformerR_.reset();
    transformerL_.reset();
    transformerR_.reset();
    detectorL_.reset();
    detectorR_.reset();
    resetPeakMeters();
    meters_.cvL = 0.0f;
    meters_.cvR = 0.0f;
}

void Fairchild670Core::setQuality(ProcessingQuality quality) noexcept
{
    // Build an NR config derived from the stage's original template, but with
    // the iteration limit adjusted for the requested quality level.
    Analog::Nonlinear::NRConfig nr = cfg_.stageCfg.nr;
    nr.maxIterations = (quality == ProcessingQuality::Draft) ? 8 : 20;
    stageL_.setNRConfig(nr);
    stageR_.setNRConfig(nr);

    // Apply the same quality setting to the pre-amplifier stages.
    Analog::Nonlinear::NRConfig nrPreamp = cfg_.preampCfg.nr;
    nrPreamp.maxIterations = nr.maxIterations;
    preampL_.setNRConfig(nrPreamp);
    preampR_.setNRConfig(nrPreamp);
}

void Fairchild670Core::setCathodeBypassCapacitance(double farads) noexcept
{
    cfg_.stageCfg.Ck = std::max(0.0, farads);
    stageL_.setCathodeBypassCapacitance(cfg_.stageCfg.Ck);
    stageR_.setCathodeBypassCapacitance(cfg_.stageCfg.Ck);
}

void Fairchild670Core::setTimingPosition(Sidechain::TimingPosition pos) noexcept
{
    cfg_.timingPreset = pos;

    // Preserve current CV state across the timing change by saving and
    // restoring the control voltage after reinitialising the detectors.
    const float savedCvL = detectorL_.controlVoltage();
    const float savedCvR = detectorR_.controlVoltage();

    detectorL_ = Sidechain::SoftRectifierDetector(
        Sidechain::toDetectorConfig(pos), cfg_.tubeRectifierForwardVoltageV);
    detectorR_ = Sidechain::SoftRectifierDetector(
        Sidechain::toDetectorConfig(pos), cfg_.tubeRectifierForwardVoltageV);
    detectorL_.prepare(sampleRate_);
    detectorR_.prepare(sampleRate_);

    // Warm the detectors to the saved CV by pushing constant-amplitude
    // samples that produce that level.  This avoids a sudden CV jump.
    (void)detectorL_.processSample(savedCvL / Analog::kVoltsPerSample);
    (void)detectorR_.processSample(savedCvR / Analog::kVoltsPerSample);
}

// ── Per-sample processing ─────────────────────────────────────────────────────

void Fairchild670Core::processStereo(float inL, float inR,
                                     float& outL, float& outR) noexcept
{
    // 1. [P3] Input transformer — bandwidth shaping + LF saturation.
    //    Matches the hardware topology where the input transformer feeds both
    //    the signal path and the sidechain.
    const float xfmrInL = inputTransformerL_.processSample(inL);
    const float xfmrInR = inputTransformerR_.processSample(inR);

    // 2. [P7] Pre-amplifier tube stage — fixed gain (CV=0), harmonic coloration.
    //    Operates at quiescent bias with no compression.
    const float preampOutL = preampL_.processSample(xfmrInL);
    const float preampOutR = preampR_.processSample(xfmrInR);

    // 3. [P4] Sidechain detectors tap directly from the input-transformer output,
    //    matching the hardware topology where the sidechain feeds from the same
    //    point as the signal path (before the pre-amplifier tube stage).  Using
    //    the pre-amplifier output here was incorrect: the pre-amp clamps its grid
    //    to ±inputClampV, which compresses the detector drive and makes the
    //    threshold control almost ineffective.  The 6AL5 forward-voltage dead
    //    zone is handled inside the SoftRectifierDetector.
    const float rawCvL = detectorL_.processSample(xfmrInL);
    const float rawCvR = detectorR_.processSample(xfmrInR);

    // 3b. Compose AC/DC threshold contributions before link logic.
    // AC threshold gates programme-dependent detector CV. DC bias adds a fixed
    // floor independent of instantaneous detector drive.
    float effectiveCvL, effectiveCvR;
    if (cfg_.linkMode == LinkMode::MidSide) {
        const float mid = (rawCvL + rawCvR) * 0.5f;
        const float acThreshMid = (acThresholdVoltageL_ + acThresholdVoltageR_) * 0.5f;
        const float dcBiasMid = (dcBiasVoltageL_ + dcBiasVoltageR_) * 0.5f;
        const float cvMid = std::max(0.0f, mid - acThreshMid) + dcBiasMid;
        effectiveCvL = effectiveCvR = cvMid;
    } else {
        effectiveCvL = std::max(0.0f, rawCvL - acThresholdVoltageL_) + dcBiasVoltageL_;
        effectiveCvR = std::max(0.0f, rawCvR - acThresholdVoltageR_) + dcBiasVoltageR_;
    }

    // 4. Compute the final CV per channel based on link mode.
    float finalCvL, finalCvR;
    if (cfg_.linkMode == LinkMode::Independent) {
        finalCvL = effectiveCvL;
        finalCvR = effectiveCvR;
    } else if (cfg_.linkMode == LinkMode::MidSide) {
        finalCvL = finalCvR = effectiveCvL;
    } else {
        float linked;
        if (cfg_.envelopeStrategy == LinkedEnvelopeStrategy::Max) {
            linked = std::max(effectiveCvL, effectiveCvR);
        } else { // Average
            linked = (effectiveCvL + effectiveCvR) * 0.5f;
        }
        finalCvL = finalCvR = linked;
    }

    // 5. [P2] Apply CV to both push-pull variable-mu stages.
    //    Scale by cfg_.sidechainAmplifierGain to account for the level added by
    //    the hardware sidechain tube chain (12AX7 → 12BH7 → 6973 → T104) which
    //    is not otherwise modelled.  In hardware the 6AL5 output connects to the
    //    6386 grids through only R107/R108 (30 Ω) and R111 (33 Ω stopper).
    //    The stage's own cvMaxV clamp limits the maximum applied bias.
    const float appliedCvL = finalCvL * cfg_.sidechainAmplifierGain;
    const float appliedCvR = finalCvR * cfg_.sidechainAmplifierGain;
    const float cvMax = static_cast<float>(cfg_.stageCfg.cvMaxV);
    const float shapedCvL = softLimitCv(appliedCvL, cvMax, cfg_.sidechainCvSoftKneeV);
    const float shapedCvR = softLimitCv(appliedCvR, cvMax, cfg_.sidechainCvSoftKneeV);
    stageL_.setCv(shapedCvL);
    stageR_.setCv(shapedCvR);
    const float stageCvL = stageL_.cv();
    const float stageCvR = stageR_.cv();

    // 6. Audio signal chain:
    //   [P2] Push-pull stage → [P5] interstage transformer → [P6] output transformer.
    const float gainOutL = stageL_.processSample(preampOutL);
    const float gainOutR = stageR_.processSample(preampOutR);
    const float interstageL = interstageTransformerL_.processSample(gainOutL);
    const float interstageR = interstageTransformerR_.processSample(gainOutR);
    outL = transformerL_.processSample(interstageL);
    outR = transformerR_.processSample(interstageR);

    // 7. Update meter accumulators.
    meters_.cvL = finalCvL;
    meters_.cvR = finalCvR;
    meters_.rawCvL = rawCvL;
    meters_.rawCvR = rawCvR;
    meters_.effectiveCvL = effectiveCvL;
    meters_.effectiveCvR = effectiveCvR;
    meters_.appliedCvL = appliedCvL;
    meters_.appliedCvR = appliedCvR;
    meters_.stageCvL = stageCvL;
    meters_.stageCvR = stageCvR;
    meters_.cvClampedL = (appliedCvL - stageCvL > kCvClampEpsilon) ? 1.0f : 0.0f;
    meters_.cvClampedR = (appliedCvR - stageCvR > kCvClampEpsilon) ? 1.0f : 0.0f;
    meters_.inPeakL  = std::max(meters_.inPeakL,  std::abs(inL));
    meters_.inPeakR  = std::max(meters_.inPeakR,  std::abs(inR));
    meters_.outPeakL = std::max(meters_.outPeakL, std::abs(outL));
    meters_.outPeakR = std::max(meters_.outPeakR, std::abs(outR));
}

void Fairchild670Core::resetPeakMeters() noexcept
{
    meters_.inPeakL  = 0.0f;
    meters_.inPeakR  = 0.0f;
    meters_.outPeakL = 0.0f;
    meters_.outPeakR = 0.0f;
    // CV fields are intentionally left unchanged (they decay via the detector).
}

} // namespace Models
