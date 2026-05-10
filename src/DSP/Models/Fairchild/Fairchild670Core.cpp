#include "Fairchild670Core.h"

#include "analog/dsp/UnitScaling.h"

#include <algorithm>
#include <cmath>

namespace Models {

// ── Constructor ───────────────────────────────────────────────────────────────

Fairchild670Core::Fairchild670Core(Fairchild670CoreConfig cfg) noexcept
    : cfg_(std::move(cfg))
    , stageL_(cfg_.stageCfg)
    , stageR_(cfg_.stageCfg)
    , transformerL_(cfg_.transformerCfg)
    , transformerR_(cfg_.transformerCfg)
    , detectorL_(Sidechain::toDetectorConfig(cfg_.timingPreset))
    , detectorR_(Sidechain::toDetectorConfig(cfg_.timingPreset))
{}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Fairchild670Core::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    stageL_.prepare(sampleRate);
    stageR_.prepare(sampleRate);
    transformerL_.prepare(sampleRate);
    transformerR_.prepare(sampleRate);
    detectorL_.prepare(sampleRate);
    detectorR_.prepare(sampleRate);
    resetPeakMeters();
}

void Fairchild670Core::reset() noexcept
{
    stageL_.reset();
    stageR_.reset();
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

    detectorL_ = Analog::Models::Sidechain::RectifierDetector(Sidechain::toDetectorConfig(pos));
    detectorR_ = Analog::Models::Sidechain::RectifierDetector(Sidechain::toDetectorConfig(pos));
    detectorL_.prepare(sampleRate_);
    detectorR_.prepare(sampleRate_);

    // Warm the detectors to the saved CV by pushing constant-amplitude
    // samples that produce that level.  This avoids a sudden CV jump.
    // We use the saved CV directly via a short steady-state feed (one sample
    // is enough for a constant level since the detector is deterministic).
    (void)detectorL_.processSample(savedCvL / Analog::kVoltsPerSample);
    (void)detectorR_.processSample(savedCvR / Analog::kVoltsPerSample);
}

// ── Per-sample processing ─────────────────────────────────────────────────────

void Fairchild670Core::processStereo(float inL, float inR,
                                     float& outL, float& outR) noexcept
{
    // 1. Run sidechain detectors on both channels.
    const float rawCvL = detectorL_.processSample(inL);
    const float rawCvR = detectorR_.processSample(inR);

    // 1b. For Mid/Side link mode, derive the sidechain CV from the Mid signal.
    //     In the classic 670 Lat/Vert mode the lateral (sum) component drives
    //     both sidechain detectors, so both channels receive the same CV.
    float effectiveCvL, effectiveCvR;
    if (cfg_.linkMode == LinkMode::MidSide) {
        const float mid = (rawCvL + rawCvR) * 0.5f;
        // Use the average of the two per-channel thresholds so that the single
        // Mid-derived CV is compared against a symmetrically blended threshold.
        const float threshMid = (thresholdVoltageL_ + thresholdVoltageR_) * 0.5f;
        const float cvMid = std::max(0.0f, mid - threshMid);
        effectiveCvL = effectiveCvR = cvMid;
    } else {
        // Apply per-channel thresholds: subtract the threshold voltage and clamp to zero.
        //     Below threshold the effective CV is 0 V (no gain reduction).
        effectiveCvL = std::max(0.0f, rawCvL - thresholdVoltageL_);
        effectiveCvR = std::max(0.0f, rawCvR - thresholdVoltageR_);
    }

    // 2. Compute the final CV per channel based on link mode.
    float finalCvL, finalCvR;
    if (cfg_.linkMode == LinkMode::Independent) {
        finalCvL = effectiveCvL;
        finalCvR = effectiveCvR;
    } else if (cfg_.linkMode == LinkMode::MidSide) {
        // Already merged above; both channels carry the mid CV.
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

    // 3. Apply CV to both gain stages.
    stageL_.setCv(finalCvL);
    stageR_.setCv(finalCvR);

    // 4. Process audio through the variable-mu gain stages and output transformers.
    outL = transformerL_.processSample(stageL_.processSample(inL));
    outR = transformerR_.processSample(stageR_.processSample(inR));

    // 5. Update meter accumulators.
    meters_.cvL = finalCvL;
    meters_.cvR = finalCvR;
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
