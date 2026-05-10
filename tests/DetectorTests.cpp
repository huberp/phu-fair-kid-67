#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/Models/Sidechain/RectifierDetector.h"
#include "../src/DSP/Models/Sidechain/TimingNetwork.h"
#include "../src/DSP/UnitScaling.h"

#include <cmath>
#include <limits>

// ── TimingNetwork: preset table sanity ───────────────────────────────────────

TEST_CASE("TimingNetwork: preset table has correct size", "[timing][presets]") {
    REQUIRE(Models::Sidechain::kTimingPresets.size()
            == static_cast<std::size_t>(Models::Sidechain::kNumTimingPresets));
}

TEST_CASE("TimingNetwork: all presets have positive time constants", "[timing][presets]") {
    for (int i = 0; i < Models::Sidechain::kNumTimingPresets; ++i) {
        const auto& p = Models::Sidechain::kTimingPresets[i];
        INFO("preset index " << i);
        REQUIRE(p.attackSec  > 0.0);
        REQUIRE(p.releaseSec > 0.0);
    }
}

TEST_CASE("TimingNetwork: release time constant increases monotonically", "[timing][presets]") {
    for (int i = 1; i < Models::Sidechain::kNumTimingPresets; ++i) {
        INFO("preset index " << i);
        REQUIRE(Models::Sidechain::kTimingPresets[i].releaseSec
                > Models::Sidechain::kTimingPresets[i - 1].releaseSec);
    }
}

TEST_CASE("TimingNetwork: computeAlpha returns value in (0, 1)", "[timing][alpha]") {
    for (int i = 0; i < Models::Sidechain::kNumTimingPresets; ++i) {
        const auto& p = Models::Sidechain::kTimingPresets[i];
        const double alphaA = Models::Sidechain::computeAlpha(p.attackSec,  44100.0);
        const double alphaR = Models::Sidechain::computeAlpha(p.releaseSec, 44100.0);
        INFO("preset " << i << " attack alpha=" << alphaA << " release alpha=" << alphaR);
        REQUIRE(alphaA > 0.0);
        REQUIRE(alphaA < 1.0);
        REQUIRE(alphaR > 0.0);
        REQUIRE(alphaR < 1.0);
    }
}

TEST_CASE("TimingNetwork: longer tau produces larger alpha (slower response)", "[timing][alpha]") {
    const double fs = 44100.0;
    const double alpha1 = Models::Sidechain::computeAlpha(0.001, fs); // 1 ms
    const double alpha2 = Models::Sidechain::computeAlpha(0.010, fs); // 10 ms
    REQUIRE(alpha2 > alpha1);
}

// ── RectifierDetector: stability ─────────────────────────────────────────────

TEST_CASE("RectifierDetector: output is finite for all presets", "[detector][stability]") {
    for (int i = 0; i < Models::Sidechain::kNumTimingPresets; ++i) {
        Models::Sidechain::RectifierDetectorConfig cfg;
        cfg.preset = static_cast<Models::Sidechain::TimingPosition>(i);
        Models::Sidechain::RectifierDetector det(cfg);
        det.prepare(44100.0);

        for (float v : {0.0f, 0.1f, -0.1f, 0.5f, -0.5f, 1.0f, -1.0f}) {
            for (int s = 0; s < 10; ++s) {
                const float out = det.processSample(v);
                INFO("preset=" << i << " input=" << v << " step=" << s);
                REQUIRE(std::isfinite(out));
            }
        }
    }
}

TEST_CASE("RectifierDetector: output is non-negative (rectified)", "[detector][rectification]") {
    Models::Sidechain::RectifierDetector det;
    det.prepare(44100.0);

    for (int i = 0; i < 500; ++i) {
        const float in = static_cast<float>(
            std::sin(2.0 * M_PI * 1000.0 * i / 44100.0));
        const float out = det.processSample(in);
        REQUIRE(out >= 0.0f);
    }
}

TEST_CASE("RectifierDetector: positive and negative inputs produce the same envelope",
          "[detector][rectification]") {
    // Full-wave rectification: +A and -A must produce the same final envelope.
    Models::Sidechain::RectifierDetector detPos;
    Models::Sidechain::RectifierDetector detNeg;
    detPos.prepare(44100.0);
    detNeg.prepare(44100.0);

    const float A = 0.5f;
    const int N = 200;

    float lastPos = 0.0f, lastNeg = 0.0f;
    for (int i = 0; i < N; ++i) {
        lastPos = detPos.processSample( A);
        lastNeg = detNeg.processSample(-A);
    }
    REQUIRE_THAT(lastPos, Catch::Matchers::WithinAbs(lastNeg, 1e-6f));
}

// ── RectifierDetector: step-response timing ───────────────────────────────────

/// Apply a unit step (full-scale) to the detector for exactly one RC time-
/// constant worth of samples, then verify that the control voltage has reached
/// ≈ 63.2 % of the final value (within a ±5 % tolerance to account for
/// discrete-time rounding with small sample counts).
static void checkStepResponseTiming(Models::Sidechain::TimingPosition pos,
                                     bool testAttack,
                                     double sampleRate = 44100.0,
                                     double toleranceFraction = 0.05)
{
    const auto& preset = Models::Sidechain::kTimingPresets[static_cast<int>(pos)];
    const double tauSec  = testAttack ? preset.attackSec : preset.releaseSec;
    const int    N       = static_cast<int>(std::round(tauSec * sampleRate));

    Models::Sidechain::RectifierDetectorConfig cfg;
    cfg.preset = pos;
    Models::Sidechain::RectifierDetector det(cfg);
    det.prepare(sampleRate);

    // Final value in volts = UnitScaling::kVoltsPerSample * 1.0f (full-scale input).
    const float finalV = UnitScaling::kVoltsPerSample;   // 10 V
    // Expected CV after N = tau*fs samples: finalV * (1 - 1/e).
    const float expected63 = finalV * static_cast<float>(1.0 - std::exp(-1.0));

    if (testAttack) {
        // Feed a sustained step to measure attack.
        for (int i = 0; i < N; ++i)
            (void)det.processSample(1.0f);
        const float cv = det.controlVoltage();
        INFO("preset=" << static_cast<int>(pos)
             << " attack tau=" << tauSec << " s  N=" << N
             << "  cv=" << cv << " V  expected~" << expected63 << " V");
        REQUIRE_THAT(cv, Catch::Matchers::WithinRel(expected63, static_cast<float>(toleranceFraction)));
    } else {
        // First, charge up the detector to ~finalV by running the full-scale
        // step for many samples until settled, then apply zero for N samples.
        const int chargeN = static_cast<int>(10.0 * preset.attackSec * sampleRate) + 1000;
        for (int i = 0; i < chargeN; ++i)
            (void)det.processSample(1.0f);

        // Release: apply silence and let CV decay for one release time constant.
        for (int i = 0; i < N; ++i)
            (void)det.processSample(0.0f);

        // After one tau: CV should have decayed to finalV * (1/e) ≈ 36.8 %.
        const float expected_decay = finalV * static_cast<float>(std::exp(-1.0));
        const float cv = det.controlVoltage();
        INFO("preset=" << static_cast<int>(pos)
             << " release tau=" << tauSec << " s  N=" << N
             << "  cv=" << cv << " V  expected~" << expected_decay << " V");
        REQUIRE_THAT(cv, Catch::Matchers::WithinRel(expected_decay, static_cast<float>(toleranceFraction)));
    }
}

TEST_CASE("RectifierDetector: attack step response — preset 1 (0.2 ms)",
          "[detector][timing][attack]") {
    checkStepResponseTiming(Models::Sidechain::TimingPosition::P1, /*attack=*/true);
}

TEST_CASE("RectifierDetector: attack step response — preset 3 (0.2 ms)",
          "[detector][timing][attack]") {
    checkStepResponseTiming(Models::Sidechain::TimingPosition::P3, /*attack=*/true);
}

TEST_CASE("RectifierDetector: attack step response — preset 6 (0.2 ms)",
          "[detector][timing][attack]") {
    checkStepResponseTiming(Models::Sidechain::TimingPosition::P6, /*attack=*/true);
}

TEST_CASE("RectifierDetector: release step response — preset 1 (0.3 s)",
          "[detector][timing][release]") {
    checkStepResponseTiming(Models::Sidechain::TimingPosition::P1, /*attack=*/false);
}

TEST_CASE("RectifierDetector: release step response — preset 3 (2.0 s)",
          "[detector][timing][release]") {
    checkStepResponseTiming(Models::Sidechain::TimingPosition::P3, /*attack=*/false);
}

TEST_CASE("RectifierDetector: release step response — preset 6 (25 s)",
          "[detector][timing][release]") {
    // P6 is now an AutoRelease preset.  The single-time-constant step-response
    // model does not apply to it; programme-dependent release behaviour is
    // verified instead by TimingConformanceTests.cpp.
    // We simply confirm that P6 is an AutoRelease preset and skip the
    // fixed-release timing check.
    using namespace Models::Sidechain;
    const auto& preset = kTimingPresets[static_cast<int>(TimingPosition::P6)];
    if (preset.kind == TimingKind::AutoRelease) {
        SKIP("P6 uses AutoRelease mode: single-RC step-response test not applicable. "
             "See TimingConformanceTests for P6 programme-dependent release tests.");
    }
    checkStepResponseTiming(TimingPosition::P6, /*attack=*/false);
}

// ── RectifierDetector: smoothness (no zippering) ─────────────────────────────

TEST_CASE("RectifierDetector: output is smooth — no large inter-sample jumps",
          "[detector][smoothness]") {
    // For any preset the maximum allowed per-sample change is (1-alpha)*maxCV.
    // We test preset P1 (fastest attack = 0.2 ms) as the worst case, then
    // verify that changes are bounded by the theoretical maximum.
    Models::Sidechain::RectifierDetectorConfig cfg;
    cfg.preset = Models::Sidechain::TimingPosition::P1;
    Models::Sidechain::RectifierDetector det(cfg);
    det.prepare(44100.0);

    const auto& preset = Models::Sidechain::kTimingPresets[0];
    const double alphaAttack = Models::Sidechain::computeAlpha(preset.attackSec,  44100.0);
    const float  maxCV       = UnitScaling::kVoltsPerSample; // 10 V

    // Theoretical maximum per-sample jump during attack.
    const float maxJump = static_cast<float>((1.0 - alphaAttack) * maxCV);

    float prev = 0.0f;
    for (int i = 0; i < 4410; ++i) {
        // Alternate between full-scale sine and silence to stress the detector.
        const float in = (i < 2205)
            ? static_cast<float>(std::sin(2.0 * M_PI * 440.0 * i / 44100.0))
            : 0.0f;
        const float cv = det.processSample(in);
        const float delta = std::abs(cv - prev);
        INFO("sample=" << i << "  cv=" << cv << "  delta=" << delta
             << "  maxJump=" << maxJump);
        REQUIRE(delta <= maxJump + 1e-5f); // small epsilon for float rounding
        REQUIRE(cv >= 0.0f);
        prev = cv;
    }
}

TEST_CASE("RectifierDetector: control voltage is monotonically non-decreasing during attack",
          "[detector][smoothness]") {
    // When a constant full-scale signal is applied, the CV must approach
    // the final value monotonically (no oscillation or overshoot).
    Models::Sidechain::RectifierDetector det;
    det.prepare(44100.0);

    float prev = 0.0f;
    for (int i = 0; i < 500; ++i) {
        const float cv = det.processSample(1.0f);
        REQUIRE(cv >= prev - 1e-6f); // non-decreasing (within float precision)
        prev = cv;
    }
}

TEST_CASE("RectifierDetector: control voltage is monotonically non-increasing during release",
          "[detector][smoothness]") {
    // After a period of full-scale signal, silence must produce a monotonically
    // decaying CV (no oscillation or artefacts).
    Models::Sidechain::RectifierDetector det;
    det.prepare(44100.0);

    // Charge up.
    for (int i = 0; i < 2000; ++i)
        (void)det.processSample(1.0f);

    float prev = det.controlVoltage();
    for (int i = 0; i < 2000; ++i) {
        const float cv = det.processSample(0.0f);
        REQUIRE(cv <= prev + 1e-6f); // non-increasing (within float precision)
        prev = cv;
    }
}

// ── RectifierDetector: lifecycle ─────────────────────────────────────────────

TEST_CASE("RectifierDetector: reset restores initial conditions", "[detector][lifecycle]") {
    Models::Sidechain::RectifierDetector det;
    det.prepare(44100.0);

    // Run for a while, then reset and verify the output matches a fresh instance.
    for (int i = 0; i < 300; ++i)
        (void)det.processSample(0.5f);

    det.reset();

    Models::Sidechain::RectifierDetector fresh;
    fresh.prepare(44100.0);

    for (int i = 0; i < 50; ++i) {
        const float cv1 = det.processSample(0.3f);
        const float cv2 = fresh.processSample(0.3f);
        REQUIRE_THAT(cv1, Catch::Matchers::WithinAbs(cv2, 1e-6f));
    }
}

TEST_CASE("RectifierDetector: all presets produce finite output at different sample rates",
          "[detector][lifecycle]") {
    for (double sr : {8000.0, 44100.0, 48000.0, 96000.0}) {
        for (int p = 0; p < Models::Sidechain::kNumTimingPresets; ++p) {
            Models::Sidechain::RectifierDetectorConfig cfg;
            cfg.preset = static_cast<Models::Sidechain::TimingPosition>(p);
            Models::Sidechain::RectifierDetector det(cfg);
            det.prepare(sr);

            INFO("sr=" << sr << " preset=" << p);
            for (int i = 0; i < 100; ++i) {
                const float out = det.processSample(0.1f);
                REQUIRE(std::isfinite(out));
            }
        }
    }
}
