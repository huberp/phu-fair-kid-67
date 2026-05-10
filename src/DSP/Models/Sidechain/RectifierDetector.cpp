#include "RectifierDetector.h"

#include "../../UnitScaling.h"

#include <algorithm>
#include <cmath>

namespace Models::Sidechain {

RectifierDetector::RectifierDetector(RectifierDetectorConfig cfg) noexcept
    : cfg_(cfg) {}

void RectifierDetector::prepare(double sampleRate) noexcept
{
    const auto& preset = kTimingPresets[static_cast<int>(cfg_.preset)];
    alphaAttack_ = computeAlpha(preset.attackSec, sampleRate);

    if (preset.kind == TimingKind::Fixed) {
        alphaRelease_ = computeAlpha(preset.releaseSec, sampleRate);
    } else {
        // AutoRelease: compute both parallel release branches.
        alphaFast_ = computeAlpha(preset.autoRelease.fastReleaseSec, sampleRate);
        alphaSlow_ = computeAlpha(preset.autoRelease.slowReleaseSec, sampleRate);
    }
}

void RectifierDetector::reset() noexcept
{
    cv_     = 0.0f;
    cvFast_ = 0.0f;
    cvSlow_ = 0.0f;
}

float RectifierDetector::processSample(float sample) noexcept
{
    // 1. Scale to volts and full-wave rectify.
    const float rect = std::abs(UnitScaling::sampleToVolts(sample));

    const auto& preset = kTimingPresets[static_cast<int>(cfg_.preset)];

    if (preset.kind == TimingKind::Fixed) {
        // 2. Choose the appropriate smoothing coefficient.
        const double alpha = (rect > cv_) ? alphaAttack_ : alphaRelease_;

        // 3. Apply one-pole RC smoothing.
        cv_ = static_cast<float>(alpha * cv_ + (1.0 - alpha) * rect);
    } else {
        // AutoRelease: two parallel release branches; output = max(cv_fast, cv_slow).
        //
        // Fast branch: charges with the standard fast attack alpha; releases with
        //   alphaFast_.  Quickly tracks transients and decays rapidly.
        //
        // Slow branch: charges AND releases with alphaSlow_.  Because alphaSlow_ is
        //   very close to 1, the slow branch builds up only when a signal is
        //   sustained for many samples (many × τ_slow).  This is the key to
        //   programme-dependent behaviour:
        //     - Brief transient → slow branch barely charges → output recovers fast
        //       (fast branch decays alone).
        //     - Sustained loud passage → slow branch charges significantly → after the
        //       signal ends, slow branch keeps output elevated for much longer.

        if (rect > cvFast_)
            cvFast_ = static_cast<float>(alphaAttack_ * cvFast_ + (1.0 - alphaAttack_) * rect);
        else
            cvFast_ = static_cast<float>(alphaFast_ * cvFast_);

        // Slow branch uses alphaSlow_ for both charge and decay so that a brief
        // transient cannot fully saturate it (charging time constant = τ_slow).
        cvSlow_ = static_cast<float>(alphaSlow_ * cvSlow_ + (1.0 - alphaSlow_) * rect);

        // Output is the maximum of both branches.
        cv_ = std::max(cvFast_, cvSlow_);
    }

    return cv_;
}

} // namespace Models::Sidechain
