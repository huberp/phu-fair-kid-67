#include "Fairchild670Core.h"

#include "../../UnitScaling.h"

#include <algorithm>
#include <cmath>

namespace Models {

// ── Constructor ───────────────────────────────────────────────────────────────

Fairchild670Core::Fairchild670Core(Fairchild670CoreConfig cfg) noexcept
    : cfg_(std::move(cfg))
    , stageL_(cfg_.stageCfg)
    , stageR_(cfg_.stageCfg)
    , detectorL_(cfg_.detectorCfg)
    , detectorR_(cfg_.detectorCfg)
{}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Fairchild670Core::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    stageL_.prepare(sampleRate);
    stageR_.prepare(sampleRate);
    detectorL_.prepare(sampleRate);
    detectorR_.prepare(sampleRate);
    resetPeakMeters();
}

void Fairchild670Core::reset() noexcept
{
    stageL_.reset();
    stageR_.reset();
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
    Circuit::Nonlinear::NRConfig nr = cfg_.stageCfg.nr;
    nr.maxIterations = (quality == ProcessingQuality::Draft) ? 8 : 20;
    stageL_.setNRConfig(nr);
    stageR_.setNRConfig(nr);
}

void Fairchild670Core::setTimingPosition(Sidechain::TimingPosition pos) noexcept
{
    cfg_.detectorCfg.preset = pos;

    // Preserve current CV state across the timing change by saving and
    // restoring the control voltage after reinitialising the detectors.
    const float savedCvL = detectorL_.controlVoltage();
    const float savedCvR = detectorR_.controlVoltage();

    detectorL_ = Sidechain::RectifierDetector(cfg_.detectorCfg);
    detectorR_ = Sidechain::RectifierDetector(cfg_.detectorCfg);
    detectorL_.prepare(sampleRate_);
    detectorR_.prepare(sampleRate_);

    // Warm the detectors to the saved CV by pushing constant-amplitude
    // samples that produce that level.  This avoids a sudden CV jump.
    // We use the saved CV directly via a short steady-state feed (one sample
    // is enough for a constant level since the detector is deterministic).
    (void)detectorL_.processSample(savedCvL / UnitScaling::kVoltsPerSample);
    (void)detectorR_.processSample(savedCvR / UnitScaling::kVoltsPerSample);
}

// ── Per-sample processing ─────────────────────────────────────────────────────

void Fairchild670Core::processStereo(float inL, float inR,
                                     float& outL, float& outR) noexcept
{
    // 1. Run sidechain detectors on both channels.
    const float rawCvL = detectorL_.processSample(inL);
    const float rawCvR = detectorR_.processSample(inR);

    // 1b. Apply threshold: subtract the threshold voltage and clamp to zero.
    //     Below threshold the effective CV is 0 V (no gain reduction).
    const float cvL = std::max(0.0f, rawCvL - thresholdVoltage_);
    const float cvR = std::max(0.0f, rawCvR - thresholdVoltage_);

    // 2. Compute the final CV per channel based on link mode.
    float finalCvL, finalCvR;
    if (cfg_.linkMode == LinkMode::Independent) {
        finalCvL = cvL;
        finalCvR = cvR;
    } else {
        float linked;
        if (cfg_.envelopeStrategy == LinkedEnvelopeStrategy::Max) {
            linked = std::max(cvL, cvR);
        } else { // Average
            linked = (cvL + cvR) * 0.5f;
        }
        finalCvL = finalCvR = linked;
    }

    // 3. Apply CV to both gain stages.
    stageL_.setCv(finalCvL);
    stageR_.setCv(finalCvR);

    // 4. Process audio through the variable-mu gain stages.
    outL = stageL_.processSample(inL);
    outR = stageR_.processSample(inR);

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
