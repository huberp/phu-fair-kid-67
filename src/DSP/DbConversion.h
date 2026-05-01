#pragma once

#include <cmath>

namespace DbConversion {

/// Convert decibels to a linear gain factor.
/// dBToLinear(0)   == 1.0, dBToLinear(-6) ≈ 0.5, dBToLinear(-inf) == 0.
[[nodiscard]] inline float dBToLinear(float dB) noexcept {
    return std::pow(10.0f, dB / 20.0f);
}

/// Convert a linear gain factor to decibels.
/// linearToDb(1.0) == 0, linearToDb(0.5) ≈ -6 dB.
/// Passing zero or a negative value results in -infinity (implementation-defined).
[[nodiscard]] inline float linearToDb(float linear) noexcept {
    return 20.0f * std::log10(linear);
}

} // namespace DbConversion
