/// Timing conformance tests for the Fairchild 670 sidechain.
///
/// These tests verify:
///   - Positions 1–4 (Fixed mode) exhibit step-response timing within the
///     windows defined by the kTimingPresets table.
///   - Positions 5–6 (AutoRelease) exhibit non-trivial, programme-dependent
///     release behaviour: shorter transients recover faster than sustained
///     loud passages.
///
/// All tests run entirely offline (no DAW, no JUCE, no audio thread).
/// Tests are registered with ctest via catch_discover_tests().

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/Models/Sidechain/RectifierDetector.h"
#include "../src/DSP/Models/Sidechain/TimingNetwork.h"
#include "../src/DSP/UnitScaling.h"

#include <cmath>

using namespace Models::Sidechain;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Measure the time (in samples) for the detector to rise from 0 to
/// `targetFraction` of `finalV` when driven by a constant full-scale signal.
/// Returns the number of samples taken, or -1 if the target was not reached
/// within `maxSamples`.
static int measureAttackSamples(TimingPosition pos,
                                double targetFraction = 0.632,
                                double sampleRate     = 44100.0,
                                int    maxSamples     = 200000)
{
    RectifierDetectorConfig cfg;
    cfg.preset = pos;
    RectifierDetector det(cfg);
    det.prepare(sampleRate);

    const float finalV  = UnitScaling::kVoltsPerSample;
    const float target  = finalV * static_cast<float>(targetFraction);

    for (int n = 0; n < maxSamples; ++n) {
        det.processSample(1.0f);
        if (det.controlVoltage() >= target)
            return n + 1;
    }
    return -1;
}

/// Charge a detector fully, then measure the number of samples for the CV to
/// fall from `finalV` to `targetFraction * finalV` (i.e., one RC time constant
/// worth of decay → ~36.8 %).
/// Returns the number of release samples, or -1 if the target was not reached.
static int measureReleaseSamples(TimingPosition pos,
                                 double targetFraction = 0.368,
                                 double sampleRate     = 44100.0,
                                 int    maxSamples     = 2000000)
{
    RectifierDetectorConfig cfg;
    cfg.preset = pos;
    RectifierDetector det(cfg);
    det.prepare(sampleRate);

    // Charge up until fully settled.
    const auto& preset  = kTimingPresets[static_cast<int>(pos)];
    const int   chargeN = static_cast<int>(20.0 * preset.attackSec * sampleRate) + 5000;
    for (int i = 0; i < chargeN; ++i)
        det.processSample(1.0f);

    const float charged = det.controlVoltage();
    const float target  = charged * static_cast<float>(targetFraction);

    for (int n = 0; n < maxSamples; ++n) {
        det.processSample(0.0f);
        if (det.controlVoltage() <= target)
            return n + 1;
    }
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1–4 fixed-timing conformance
// ─────────────────────────────────────────────────────────────────────────────

/// For a Fixed-mode preset, the detector should reach 63.2 % of the final
/// value in approximately (attackSec * sampleRate) samples.
/// We allow ±20 % tolerance to accommodate discrete-time rounding and the
/// finite charge-up transient.
static void checkFixedAttackConformance(TimingPosition pos,
                                        double sampleRate    = 44100.0,
                                        double toleranceFrac = 0.20)
{
    const auto& preset = kTimingPresets[static_cast<int>(pos)];
    REQUIRE(preset.kind == TimingKind::Fixed);

    const int   expectedN = static_cast<int>(std::round(preset.attackSec * sampleRate));
    const int   lo        = static_cast<int>(expectedN * (1.0 - toleranceFrac));
    const int   hi        = static_cast<int>(expectedN * (1.0 + toleranceFrac)) + 1;
    const int   actual    = measureAttackSamples(pos, 0.632, sampleRate);

    INFO("pos=" << static_cast<int>(pos)
         << " expectedN=" << expectedN << " actual=" << actual
         << " lo=" << lo << " hi=" << hi);

    REQUIRE(actual > 0);
    REQUIRE(actual >= lo);
    REQUIRE(actual <= hi);
}

/// For a Fixed-mode preset, the detector should decay to 36.8 % of its initial
/// (fully charged) value in approximately (releaseSec * sampleRate) samples.
/// We allow ±20 % tolerance.
static void checkFixedReleaseConformance(TimingPosition pos,
                                         double sampleRate    = 44100.0,
                                         double toleranceFrac = 0.20)
{
    const auto& preset = kTimingPresets[static_cast<int>(pos)];
    REQUIRE(preset.kind == TimingKind::Fixed);

    const int expectedN = static_cast<int>(std::round(preset.releaseSec * sampleRate));
    const int lo        = static_cast<int>(expectedN * (1.0 - toleranceFrac));
    const int hi        = static_cast<int>(expectedN * (1.0 + toleranceFrac)) + 1;
    const int actual    = measureReleaseSamples(pos, 0.368, sampleRate);

    INFO("pos=" << static_cast<int>(pos)
         << " expectedN=" << expectedN << " actual=" << actual
         << " lo=" << lo << " hi=" << hi);

    REQUIRE(actual > 0);
    REQUIRE(actual >= lo);
    REQUIRE(actual <= hi);
}

TEST_CASE("TimingConformance: P1 attack within ±20% of 0.2 ms",
          "[timing][conformance][fixed][attack]")
{
    checkFixedAttackConformance(TimingPosition::P1);
}

TEST_CASE("TimingConformance: P2 attack within ±20% of 0.2 ms",
          "[timing][conformance][fixed][attack]")
{
    checkFixedAttackConformance(TimingPosition::P2);
}

TEST_CASE("TimingConformance: P3 attack within ±20% of 0.2 ms",
          "[timing][conformance][fixed][attack]")
{
    checkFixedAttackConformance(TimingPosition::P3);
}

TEST_CASE("TimingConformance: P4 attack within ±20% of 0.2 ms",
          "[timing][conformance][fixed][attack]")
{
    checkFixedAttackConformance(TimingPosition::P4);
}

TEST_CASE("TimingConformance: P1 release within ±20% of 0.30 s",
          "[timing][conformance][fixed][release]")
{
    checkFixedReleaseConformance(TimingPosition::P1);
}

TEST_CASE("TimingConformance: P2 release within ±20% of 0.80 s",
          "[timing][conformance][fixed][release]")
{
    checkFixedReleaseConformance(TimingPosition::P2);
}

TEST_CASE("TimingConformance: P3 release within ±20% of 2.0 s",
          "[timing][conformance][fixed][release]")
{
    checkFixedReleaseConformance(TimingPosition::P3);
}

TEST_CASE("TimingConformance: P4 release within ±20% of 5.0 s",
          "[timing][conformance][fixed][release]")
{
    checkFixedReleaseConformance(TimingPosition::P4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Positions 5 & 6: automatic / programme-dependent release
// ─────────────────────────────────────────────────────────────────────────────

/// Core helper: verifies that after a *short burst* the detector recovers
/// significantly faster than after a *sustained loud passage*.
///
/// Strategy:
///   1. Run a short burst (burstDurationSec) at full scale, then silence.
///      Measure how long until CV falls to `recoveryFrac` of the burst peak.
///   2. Run a long sustained segment (sustainDurationSec) at full scale,
///      then silence.  Measure recovery time to the same fraction.
///   3. Assert sustainedRecoveryN > burstRecoveryN * minRatio, meaning
///      sustained material recovers distinctly more slowly than brief peaks.
static void checkAutoProgramDependentRelease(TimingPosition pos,
                                             double burstDurationSec,
                                             double sustainDurationSec,
                                             double recoveryFrac,
                                             double minRatio,
                                             double sampleRate = 44100.0)
{
    const auto& preset = kTimingPresets[static_cast<int>(pos)];
    REQUIRE(preset.kind == TimingKind::AutoRelease);

    // maxWait: generous 4× the slow release τ.
    const int maxWait = static_cast<int>(preset.autoRelease.slowReleaseSec * sampleRate * 4) + 1;

    RectifierDetectorConfig cfg;
    cfg.preset = pos;

    // --- Short burst recovery ---
    int   burstRecoveryN = 0;
    float burstPeak      = 0.0f;
    {
        RectifierDetector det(cfg);
        det.prepare(sampleRate);

        const int burstN = static_cast<int>(burstDurationSec * sampleRate);
        for (int i = 0; i < burstN; ++i)
            det.processSample(1.0f);

        burstPeak = det.controlVoltage();
        const float target = burstPeak * static_cast<float>(recoveryFrac);

        int n = 0;
        while (det.controlVoltage() > target && n < maxWait) {
            det.processSample(0.0f);
            ++n;
        }
        burstRecoveryN = n;
    }

    // --- Sustained passage recovery ---
    int   sustainedRecoveryN = 0;
    float sustainedPeak      = 0.0f;
    {
        RectifierDetector det(cfg);
        det.prepare(sampleRate);

        const int sustainN = static_cast<int>(sustainDurationSec * sampleRate);
        for (int i = 0; i < sustainN; ++i)
            det.processSample(1.0f);

        sustainedPeak = det.controlVoltage();
        const float target = sustainedPeak * static_cast<float>(recoveryFrac);

        int n = 0;
        while (det.controlVoltage() > target && n < maxWait) {
            det.processSample(0.0f);
            ++n;
        }
        sustainedRecoveryN = n;
    }

    INFO("pos=" << static_cast<int>(pos)
         << "  burst_dur=" << burstDurationSec << "s"
         << "  burst_peak=" << burstPeak
         << "  burst_recovery_n=" << burstRecoveryN
         << "  sustain_dur=" << sustainDurationSec << "s"
         << "  sustained_peak=" << sustainedPeak
         << "  sustained_recovery_n=" << sustainedRecoveryN
         << "  recoveryFrac=" << recoveryFrac
         << "  minRatio=" << minRatio);

    // Both must reach the recovery threshold within maxWait samples.
    REQUIRE(burstRecoveryN     < maxWait);
    REQUIRE(sustainedRecoveryN < maxWait);

    // Sustained material must take distinctly longer to recover.
    REQUIRE(sustainedRecoveryN > static_cast<int>(burstRecoveryN * minRatio));
}

TEST_CASE("TimingConformance: P5 is an AutoRelease preset",
          "[timing][conformance][auto]")
{
    REQUIRE(kTimingPresets[static_cast<int>(TimingPosition::P5)].kind
            == TimingKind::AutoRelease);
}

TEST_CASE("TimingConformance: P6 is an AutoRelease preset",
          "[timing][conformance][auto]")
{
    REQUIRE(kTimingPresets[static_cast<int>(TimingPosition::P6)].kind
            == TimingKind::AutoRelease);
}

TEST_CASE("TimingConformance: P5 exhibits programme-dependent release (burst vs sustained)",
          "[timing][conformance][auto][p5]")
{
    // Burst = 50 ms (≪ τ_slow=10 s → slow branch barely charges).
    // Sustained = 30 s (3 × τ_slow → slow branch charges to ~95 % of peak).
    // Recovery threshold = 20 % of peak.
    // After burst: fast branch (τ=0.5 s) dominates → recovery in ~35 k samples.
    // After 30 s sustain: slow branch (τ=10 s) dominates → recovery in ~688 k samples.
    // Expected ratio ≈ 19×; we require ≥ 3×.
    checkAutoProgramDependentRelease(TimingPosition::P5,
                                     /*burstDurationSec=*/   0.05,
                                     /*sustainDurationSec=*/ 30.0,
                                     /*recoveryFrac=*/       0.20,
                                     /*minRatio=*/           3.0);
}

TEST_CASE("TimingConformance: P6 exhibits programme-dependent release (burst vs sustained)",
          "[timing][conformance][auto][p6]")
{
    // Burst = 50 ms (≪ τ_slow=25 s → slow branch barely charges).
    // Sustained = 60 s (2.4 × τ_slow → slow branch charges to ~91 % of peak).
    // Recovery threshold = 20 % of peak.
    // After burst: fast branch (τ=1 s) dominates → recovery in ~71 k samples.
    // After 60 s sustain: slow branch (τ=25 s) dominates → recovery in ~1.57 M samples.
    // Expected ratio ≈ 22×; we require ≥ 3×.
    checkAutoProgramDependentRelease(TimingPosition::P6,
                                     /*burstDurationSec=*/   0.05,
                                     /*sustainDurationSec=*/ 60.0,
                                     /*recoveryFrac=*/       0.20,
                                     /*minRatio=*/           3.0);
}

/// Verify that P5/P6 fast recovery is faster than P4 fixed release recovery,
/// i.e. a brief transient on P5 or P6 does not "stick" as long as P4 would.
static void checkAutoFastRecoveryFasterThanFixedP4(TimingPosition autoPos,
                                                    double sampleRate = 44100.0)
{
    const auto& autoPreset = kTimingPresets[static_cast<int>(autoPos)];
    REQUIRE(autoPreset.kind == TimingKind::AutoRelease);

    // Brief 50 ms burst, then measure samples to reach 50 % decay.
    const int burstN  = static_cast<int>(0.05 * sampleRate);
    const int maxWait = static_cast<int>(autoPreset.autoRelease.slowReleaseSec * sampleRate * 3);

    auto measure50pct = [&](TimingPosition pos) -> int {
        RectifierDetectorConfig cfg;
        cfg.preset = pos;
        RectifierDetector det(cfg);
        det.prepare(sampleRate);
        for (int i = 0; i < burstN; ++i)
            det.processSample(1.0f);
        const float target = det.controlVoltage() * 0.50f;
        int n = 0;
        while (det.controlVoltage() > target && n < maxWait) {
            det.processSample(0.0f);
            ++n;
        }
        return n;
    };

    const int p4N   = measure50pct(TimingPosition::P4);
    const int autoN = measure50pct(autoPos);

    INFO("P4 50% recovery samples=" << p4N
         << "  autoPos=" << static_cast<int>(autoPos)
         << " recovery samples=" << autoN);

    // After a brief transient, the auto-release mode should recover at least
    // as fast as P4 (which has a 5 s fixed release), and typically faster.
    REQUIRE(autoN <= p4N);
}

TEST_CASE("TimingConformance: P5 brief-transient recovery not slower than P4",
          "[timing][conformance][auto][p5]")
{
    checkAutoFastRecoveryFasterThanFixedP4(TimingPosition::P5);
}

TEST_CASE("TimingConformance: P6 brief-transient recovery not slower than P4",
          "[timing][conformance][auto][p6]")
{
    checkAutoFastRecoveryFasterThanFixedP4(TimingPosition::P6);
}

/// All positions should still have a monotonically fast attack (≤ 0.5 ms).
TEST_CASE("TimingConformance: all positions have fast attack (≤ 0.5 ms)",
          "[timing][conformance][attack]")
{
    constexpr double kMaxAttackSec = 0.0005; // 0.5 ms
    for (int i = 0; i < kNumTimingPresets; ++i) {
        INFO("preset index=" << i
             << "  attackSec=" << kTimingPresets[i].attackSec);
        REQUIRE(kTimingPresets[i].attackSec <= kMaxAttackSec);
        REQUIRE(kTimingPresets[i].attackSec  > 0.0);
    }
}

/// All AutoRelease positions must have strictly positive fast and slow branch τ.
TEST_CASE("TimingConformance: AutoRelease positions have valid branch constants",
          "[timing][conformance][auto]")
{
    for (int i = 0; i < kNumTimingPresets; ++i) {
        const auto& p = kTimingPresets[i];
        if (p.kind == TimingKind::AutoRelease) {
            INFO("preset index=" << i);
            REQUIRE(p.autoRelease.fastReleaseSec > 0.0);
            REQUIRE(p.autoRelease.slowReleaseSec > 0.0);
            // Slow branch must be strictly slower than fast branch.
            REQUIRE(p.autoRelease.slowReleaseSec > p.autoRelease.fastReleaseSec);
        }
    }
}
