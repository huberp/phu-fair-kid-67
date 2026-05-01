#pragma once

namespace UnitScaling {

/// Full-scale sample amplitude (±1.0) maps to ±10 V (Eurorack standard).
constexpr float kVoltsPerSample = 10.0f;
constexpr float kSamplesPerVolt = 1.0f / kVoltsPerSample;

/// Convert a normalised sample value (±1.0 full-scale) to volts.
[[nodiscard]] inline float sampleToVolts(float sample) noexcept {
    return sample * kVoltsPerSample;
}

/// Convert a voltage value to a normalised sample value (±1.0 full-scale).
[[nodiscard]] inline float voltsToSample(float volts) noexcept {
    return volts * kSamplesPerVolt;
}

} // namespace UnitScaling
