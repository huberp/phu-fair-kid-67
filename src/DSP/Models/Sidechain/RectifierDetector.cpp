#include "RectifierDetector.h"

#include "../../UnitScaling.h"

#include <cmath>

namespace Models::Sidechain {

RectifierDetector::RectifierDetector(RectifierDetectorConfig cfg) noexcept
    : cfg_(cfg) {}

void RectifierDetector::prepare(double sampleRate) noexcept
{
    const auto& preset = kTimingPresets[static_cast<int>(cfg_.preset)];
    alphaAttack_  = computeAlpha(preset.attackSec,  sampleRate);
    alphaRelease_ = computeAlpha(preset.releaseSec, sampleRate);
}

void RectifierDetector::reset() noexcept
{
    cv_ = 0.0f;
}

float RectifierDetector::processSample(float sample) noexcept
{
    // 1. Scale to volts and full-wave rectify.
    const float rect = std::abs(UnitScaling::sampleToVolts(sample));

    // 2. Choose the appropriate smoothing coefficient.
    const double alpha = (rect > cv_) ? alphaAttack_ : alphaRelease_;

    // 3. Apply one-pole RC smoothing.
    cv_ = static_cast<float>(alpha * cv_ + (1.0 - alpha) * rect);

    return cv_;
}

} // namespace Models::Sidechain
