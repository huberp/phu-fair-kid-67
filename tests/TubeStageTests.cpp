#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/Models/TubeStage.h"
#include "../src/DSP/UnitScaling.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Process `warmupSamples` silent samples then return the stage, ready to
/// measure.  A new stage is created each time to ensure test isolation.
static Models::TubeStage makeWarmedStage(double sampleRate = 44100.0,
                                         int warmupSamples = 2000)
{
    Models::TubeStage stage;
    stage.prepare(sampleRate);
    for (int i = 0; i < warmupSamples; ++i)
        (void)stage.processSample(0.0f);
    return stage;
}

/// Measure the peak-to-peak output swing of a sine wave at the given amplitude
/// over `cycles` complete periods of `freqHz`.
static float measurePeakToPeak(Models::TubeStage& stage,
                                float amplitude, double freqHz = 100.0,
                                double sampleRate = 44100.0, int cycles = 5)
{
    const int N = static_cast<int>(sampleRate / freqHz * cycles);
    float maxOut = std::numeric_limits<float>::lowest();
    float minOut = std::numeric_limits<float>::max();

    for (int i = 0; i < N; ++i) {
        const float in = amplitude *
            static_cast<float>(std::sin(2.0 * M_PI * freqHz * i / sampleRate));
        const float out = stage.processSample(in);
        REQUIRE(std::isfinite(out));
        maxOut = std::max(maxOut, out);
        minOut = std::min(minOut, out);
    }
    return maxOut - minOut;
}

// ── Stability: no NaN / Inf ───────────────────────────────────────────────────

TEST_CASE("TubeStage: no NaN/Inf with moderate input", "[tubestage][stability]") {
    Models::TubeStage stage;
    stage.prepare(44100.0);

    for (float v : {0.0f, 0.1f, -0.1f, 0.5f, -0.5f, 1.0f, -1.0f}) {
        for (int i = 0; i < 20; ++i) {
            const float out = stage.processSample(v);
            INFO("input=" << v << "  iteration=" << i);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("TubeStage: no NaN/Inf with extreme input (±10 full-scale)", "[tubestage][stability]") {
    Models::TubeStage stage;
    stage.prepare(44100.0);

    for (float v : {-10.0f, 10.0f, -5.0f, 5.0f}) {
        for (int i = 0; i < 20; ++i) {
            const float out = stage.processSample(v);
            INFO("input=" << v << "  iteration=" << i);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("TubeStage: no NaN/Inf at silent input", "[tubestage][stability]") {
    Models::TubeStage stage;
    stage.prepare(44100.0);

    for (int i = 0; i < 500; ++i) {
        const float out = stage.processSample(0.0f);
        REQUIRE(std::isfinite(out));
    }
}

// ── DC operating point ────────────────────────────────────────────────────────

TEST_CASE("TubeStage: quiescent plate voltage is within supply rail", "[tubestage][bias]") {
    // After warm-up the plate must sit between ground and B+.
    Models::TubeStageConfig cfg;
    Models::TubeStage stage(cfg);
    stage.prepare(44100.0);

    for (int i = 0; i < 2000; ++i)
        (void)stage.processSample(0.0f);

    // Output is Vp / kVoltsPerSample; Vp ∈ (0, Vcc)
    const float out   = stage.processSample(0.0f);
    const float Vp    = out * UnitScaling::kVoltsPerSample;
    INFO("Quiescent Vp = " << Vp << " V");
    REQUIRE(Vp > 0.0f);
    REQUIRE(Vp < static_cast<float>(cfg.Vcc));
}

TEST_CASE("TubeStage: quiescent output is stable (converges)", "[tubestage][bias]") {
    // After sufficient warm-up, the output for a silent input should not drift.
    Models::TubeStage stage;
    stage.prepare(44100.0);

    // Burn in
    for (int i = 0; i < 3000; ++i)
        (void)stage.processSample(0.0f);

    const float v1 = stage.processSample(0.0f);
    const float v2 = stage.processSample(0.0f);
    const float v3 = stage.processSample(0.0f);

    // The steady-state output must not change between consecutive silent samples.
    REQUIRE_THAT(v2, Catch::Matchers::WithinAbs(v1, 1e-4f));
    REQUIRE_THAT(v3, Catch::Matchers::WithinAbs(v1, 1e-4f));
}

// ── Low-level: approximately linear ──────────────────────────────────────────

TEST_CASE("TubeStage: low-level gain is consistent (linear regime)", "[tubestage][linear]") {
    // At small signal amplitudes the gain (output PTP / input PTP) should be
    // essentially constant — this is the definition of small-signal linearity.
    const float A1 = 0.001f;
    const float A2 = 0.002f; // double amplitude

    auto stage1 = makeWarmedStage();
    auto stage2 = makeWarmedStage();

    const float ptp1 = measurePeakToPeak(stage1, A1);
    const float ptp2 = measurePeakToPeak(stage2, A2);

    // Gain: ptp / (2·A·kVoltsPerSample)  (both in voltage domain)
    const float gain1 = ptp1 / (2.0f * A1);
    const float gain2 = ptp2 / (2.0f * A2);

    INFO("gain at A=" << A1 << ": " << gain1);
    INFO("gain at A=" << A2 << ": " << gain2);

    // Both gains should agree to within 5 % in the linear region.
    REQUIRE_THAT(gain2, Catch::Matchers::WithinRel(gain1, 0.05f));
}

TEST_CASE("TubeStage: low-level output is non-zero (stage has gain)", "[tubestage][linear]") {
    auto stage = makeWarmedStage();
    const float ptp = measurePeakToPeak(stage, 0.001f);
    INFO("PTP output (normalised sample units): " << ptp);
    REQUIRE(ptp > 0.0f);
}

// ── High-level: stable distortion and compression ────────────────────────────

TEST_CASE("TubeStage: high drive shows gain compression vs low drive", "[tubestage][distortion]") {
    // A nonlinear stage must exhibit less voltage gain at high drive than at
    // low drive (compression / saturation).
    const float A_low  = 0.001f; // ≈ 10 mV at grid — well within linear range
    const float A_high = 0.45f;  // ≈ 4.5 V at grid — near / above clamp → clipping

    auto stage_low  = makeWarmedStage();
    auto stage_high = makeWarmedStage();

    const float ptp_low  = measurePeakToPeak(stage_low,  A_low);
    const float ptp_high = measurePeakToPeak(stage_high, A_high);

    // Normalised gain: ptp / (2·A)
    const float gain_low  = ptp_low  / (2.0f * A_low);
    const float gain_high = ptp_high / (2.0f * A_high);

    INFO("gain (low  drive, A=" << A_low  << "): " << gain_low);
    INFO("gain (high drive, A=" << A_high << "): " << gain_high);

    // High-drive gain must be measurably lower than low-drive gain.
    // Even 15 % gain reduction (≈ 1.2 dB compression) is clearly audible and
    // confirms nonlinear operation; the 12AX7 Koren model with these component
    // values produces a gentle, musically pleasant saturation characteristic.
    REQUIRE(gain_low > gain_high * 1.15f);
}

TEST_CASE("TubeStage: high drive output remains bounded (no runaway)", "[tubestage][distortion]") {
    // At maximum drive the output plate voltage must stay within the supply rail.
    Models::TubeStageConfig cfg;
    auto stage = makeWarmedStage();

    const float Vcc_norm = static_cast<float>(cfg.Vcc) / UnitScaling::kVoltsPerSample;

    for (int cycle = 0; cycle < 10; ++cycle) {
        for (int i = 0; i < 441; ++i) {
            const float in = 0.5f *
                static_cast<float>(std::sin(2.0 * M_PI * 100.0 * i / 44100.0));
            const float out = stage.processSample(in);
            REQUIRE(std::isfinite(out));
            // Plate cannot exceed B+ or go below ground.
            REQUIRE(out >= 0.0f);
            REQUIRE(out <= Vcc_norm + 1e-3f); // small tolerance for NR residual
        }
    }
}

// ── Lifecycle: prepare / reset ────────────────────────────────────────────────

TEST_CASE("TubeStage: reset restores initial conditions", "[tubestage][lifecycle]") {
    // Two stages processed identically after a reset must produce identical output.
    Models::TubeStage stage;
    stage.prepare(44100.0);

    // Collect output from first run.
    std::vector<float> run1;
    for (int i = 0; i < 200; ++i)
        run1.push_back(stage.processSample(static_cast<float>(i % 2 == 0 ? 0.01f : -0.01f)));

    // Reset and repeat.
    stage.reset();
    for (int i = 0; i < 200; ++i) {
        const float out = stage.processSample(static_cast<float>(i % 2 == 0 ? 0.01f : -0.01f));
        REQUIRE_THAT(out, Catch::Matchers::WithinAbs(run1[i], 1e-6f));
    }
}

TEST_CASE("TubeStage: different sample rates produce finite output", "[tubestage][lifecycle]") {
    for (double sr : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        Models::TubeStage stage;
        stage.prepare(sr);
        INFO("sampleRate=" << sr);
        for (int i = 0; i < 100; ++i) {
            const float out = stage.processSample(0.1f);
            REQUIRE(std::isfinite(out));
        }
    }
}

// ── Cathode bypass capacitor ──────────────────────────────────────────────────

TEST_CASE("TubeStage: with cathode bypass cap, output is finite", "[tubestage][bypass]") {
    Models::TubeStageConfig cfg;
    cfg.Ck = 47e-6; // 47 µF bypass cap
    Models::TubeStage stage(cfg);
    stage.prepare(44100.0);

    for (int i = 0; i < 500; ++i) {
        const float in = 0.1f * static_cast<float>(
            std::sin(2.0 * M_PI * 1000.0 * i / 44100.0));
        const float out = stage.processSample(in);
        INFO("sample=" << i);
        REQUIRE(std::isfinite(out));
    }
}

TEST_CASE("TubeStage: with cathode bypass cap, high-freq gain >= no-bypass gain",
          "[tubestage][bypass]") {
    // Bypassing Rk with a cap boosts the mid/high-freq voltage gain.
    // After warm-up, a 1 kHz sine should produce a larger output swing with the
    // bypass cap than without it.
    const double sr = 44100.0;
    const int warmup = 3000;
    const float A = 0.01f;

    // No bypass
    Models::TubeStageConfig cfgNoCk;
    cfgNoCk.Ck = 0.0;
    Models::TubeStage stageNoCk(cfgNoCk);
    stageNoCk.prepare(sr);
    for (int i = 0; i < warmup; ++i) (void)stageNoCk.processSample(0.0f);
    const float ptp_noCk = measurePeakToPeak(stageNoCk, A, 1000.0, sr, 5);

    // With 47 µF bypass
    Models::TubeStageConfig cfgCk;
    cfgCk.Ck = 47e-6;
    Models::TubeStage stageCk(cfgCk);
    stageCk.prepare(sr);
    for (int i = 0; i < warmup; ++i) (void)stageCk.processSample(0.0f);
    const float ptp_Ck = measurePeakToPeak(stageCk, A, 1000.0, sr, 5);

    INFO("ptp (no bypass): " << ptp_noCk);
    INFO("ptp (47µF bypass): " << ptp_Ck);

    REQUIRE(std::isfinite(ptp_noCk));
    REQUIRE(std::isfinite(ptp_Ck));
    // At 1 kHz with a 47 µF cap the pole (≈ 2.25 Hz) is well below the signal
    // frequency, so the cathode is AC-shorted and gain must be higher than
    // without the bypass cap.
    REQUIRE(ptp_Ck > ptp_noCk);
}
