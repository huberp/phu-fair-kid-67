#pragma once

#include <array>

namespace Models::Sidechain {

/// Attack and release time constants for one Fairchild-style preset position.
struct TimingPreset {
    double attackSec;   ///< Attack time constant (seconds).
    double releaseSec;  ///< Release time constant (seconds).
};

/// Number of timing presets (six positions, as on the Fairchild 670).
constexpr int kNumTimingPresets = 6;

/// Centralized preset table.  Values approximate the Fairchild 670
/// time-constant switch positions 1–6.
///
/// Pos | Attack   | Release
/// ----|----------|--------
///  1  | 0.2 ms  | 0.30 s
///  2  | 0.2 ms  | 0.80 s
///  3  | 0.4 ms  | 2.00 s
///  4  | 0.8 ms  | 5.00 s
///  5  | 2.0 ms  | 10.00 s
///  6  | 8.0 ms  | 25.00 s
extern const std::array<TimingPreset, kNumTimingPresets> kTimingPresets;

/// Index for the six Fairchild-style presets.
/// Stored 0–5 internally; presented as positions 1–6 in the user interface.
enum class TimingPosition : int { P1 = 0, P2, P3, P4, P5, P6 };

/// Compute the one-pole smoothing coefficient for the given RC time constant
/// and sample rate.
///
/// Derived from the continuous-time RC step response:
///   alpha = exp(-1 / (tau * fs))
///
/// @param tauSec     Time constant in seconds (must be > 0).
/// @param sampleRate Sample rate in Hz (must be > 0).
/// @return           Smoothing coefficient in [0, 1).
[[nodiscard]] double computeAlpha(double tauSec, double sampleRate) noexcept;

} // namespace Models::Sidechain
