#pragma once

#include <array>

namespace Models::Sidechain {

/// Describes which timing mode a preset position uses.
///
/// - Fixed:       Both attack and release are single-pole RC constants (positions 1–4).
/// - AutoRelease: Attack is still a fast single-pole RC, but release is program-dependent:
///               two parallel release branches (fast and slow) run simultaneously and the
///               output follows the larger of the two, producing quick recovery after short
///               transients and slower recovery after sustained loud passages (positions 5–6).
enum class TimingKind { Fixed, AutoRelease };

/// Parameters for the program-dependent (automatic) release mode.
///
/// Two RC branches decay in parallel after the signal drops below the detector
/// envelope.  The output is max(cv_fast, cv_slow), which gives:
///   - rapid initial recovery after a brief transient (fast branch dominates early)
///   - slower long-tail recovery after sustained heavy limiting (slow branch persists)
struct AutoReleaseParams {
    double fastReleaseSec; ///< Time constant for the fast recovery branch (seconds).
    double slowReleaseSec; ///< Time constant for the slow recovery branch (seconds).
};

/// Attack and release time constants for one Fairchild-style preset position.
///
/// For Fixed presets:       releaseSec is the single release time constant.
/// For AutoRelease presets: releaseSec equals autoRelease.slowReleaseSec (kept for
///                          backward-compatible step-response measurements); the
///                          autoRelease field carries both branch constants.
struct TimingPreset {
    TimingKind       kind       = TimingKind::Fixed; ///< Timing mode for this position.
    double           attackSec  = 0.0002;            ///< Attack time constant (seconds).
    double           releaseSec = 0.0;               ///< Release time constant (Fixed) or slowReleaseSec (AutoRelease).
    AutoReleaseParams autoRelease = {0.0, 0.0};      ///< Valid only when kind == AutoRelease.
};

/// Number of timing presets (six positions, as on the Fairchild 670).
constexpr int kNumTimingPresets = 6;

/// Centralised preset table.  Values are derived from the Fairchild 670 manual.
///
/// Pos | Kind        | Attack  | Release (fixed) or [fast / slow] (auto)
/// ----|-------------|---------|------------------------------------------
///  1  | Fixed       | 0.2 ms  | 0.30 s
///  2  | Fixed       | 0.2 ms  | 0.80 s
///  3  | Fixed       | 0.2 ms  | 2.00 s
///  4  | Fixed       | 0.2 ms  | 5.00 s
///  5  | AutoRelease | 0.2 ms  | fast 0.50 s / slow 10.0 s
///  6  | AutoRelease | 0.2 ms  | fast 1.00 s / slow 25.0 s
///
/// Notes:
/// - Attack is held at ~0.2 ms for all positions, consistent with the Fairchild 670
///   manual which specifies a roughly constant, very fast attack across positions.
///   The earlier code had per-position attack values (0.2–8 ms); this has been
///   corrected to a uniform 0.2 ms.
/// - Positions 5 and 6 are automatic/program-dependent release modes per the manual.
///   The fixed releaseSec field equals the slow branch constant so that step-response
///   tests remain self-consistent.
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
