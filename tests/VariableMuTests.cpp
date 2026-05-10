#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analog/models/VariableMuStage.h"
#include "analog/dsp/UnitScaling.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Process `warmupSamples` silent samples at the given CV, then return the
/// stage ready to measure.
static Analog::Models::VariableMuStage makeWarmedStage(float cv = 0.0f,
                                               double sampleRate = 44100.0,
                                               int warmupSamples = 2000)
{
    Analog::Models::VariableMuStage stage;
    stage.prepare(sampleRate);
    stage.setCv(cv);
    for (int i = 0; i < warmupSamples; ++i)
        (void)stage.processSample(0.0f);
    return stage;
}

/// Measure peak-to-peak output swing of a sine wave over `cycles` full periods.
static float measurePeakToPeak(Analog::Models::VariableMuStage& stage,
                                float amplitude, double freqHz = 100.0,
                                double sampleRate = 44100.0, int cycles = 5)
{
    const int N = static_cast<int>(sampleRate / freqHz * cycles);
    float maxOut = std::numeric_limits<float>::lowest();
    float minOut = std::numeric_limits<float>::max();

    for (int i = 0; i < N; ++i) {
        const float inputSample = amplitude *
            static_cast<float>(std::sin(2.0 * M_PI * freqHz * i / sampleRate));
        const float out = stage.processSample(inputSample);
        REQUIRE(std::isfinite(out));
        maxOut = std::max(maxOut, out);
        minOut = std::min(minOut, out);
    }
    return maxOut - minOut;
}

// ── Stability: no NaN / Inf ───────────────────────────────────────────────────

TEST_CASE("VariableMuStage: no NaN/Inf with moderate input at CV=0",
          "[variablemu][stability]")
{
    Analog::Models::VariableMuStage stage;
    stage.prepare(44100.0);
    stage.setCv(0.0f);

    for (float v : {0.0f, 0.1f, -0.1f, 0.5f, -0.5f, 1.0f, -1.0f}) {
        for (int i = 0; i < 20; ++i) {
            const float out = stage.processSample(v);
            INFO("input=" << v << " iteration=" << i);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("VariableMuStage: no NaN/Inf at maximum CV",
          "[variablemu][stability]")
{
    Analog::Models::VariableMuStageConfig cfg;
    Analog::Models::VariableMuStage stage(cfg);
    stage.prepare(44100.0);
    stage.setCv(static_cast<float>(cfg.cvMaxV));

    for (float v : {0.0f, 0.1f, -0.1f, 0.5f, -0.5f, 1.0f, -1.0f}) {
        for (int i = 0; i < 20; ++i) {
            const float out = stage.processSample(v);
            INFO("input=" << v << " iteration=" << i << " CV=" << cfg.cvMaxV);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("VariableMuStage: no NaN/Inf at extreme input (±10 full-scale)",
          "[variablemu][stability]")
{
    for (float cv : {0.0f, 3.0f, 6.0f}) {
        Analog::Models::VariableMuStage stage;
        stage.prepare(44100.0);
        stage.setCv(cv);

        for (float v : {-10.0f, 10.0f, -5.0f, 5.0f}) {
            for (int i = 0; i < 20; ++i) {
                const float out = stage.processSample(v);
                INFO("input=" << v << " CV=" << cv << " iteration=" << i);
                REQUIRE(std::isfinite(out));
            }
        }
    }
}

// ── CV clamping ───────────────────────────────────────────────────────────────

TEST_CASE("VariableMuStage: negative CV is clamped to zero",
          "[variablemu][cv]")
{
    Analog::Models::VariableMuStage stage;
    stage.prepare(44100.0);
    stage.setCv(-5.0f);
    REQUIRE_THAT(stage.cv(), Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("VariableMuStage: CV above cvMaxV is clamped to cvMaxV",
          "[variablemu][cv]")
{
    Analog::Models::VariableMuStageConfig cfg;
    Analog::Models::VariableMuStage stage(cfg);
    stage.prepare(44100.0);
    stage.setCv(static_cast<float>(cfg.cvMaxV) + 100.0f);
    REQUIRE_THAT(stage.cv(),
                 Catch::Matchers::WithinAbs(static_cast<float>(cfg.cvMaxV), 1e-6f));
}

TEST_CASE("VariableMuStage: CV within range is stored unchanged",
          "[variablemu][cv]")
{
    Analog::Models::VariableMuStageConfig cfg;
    Analog::Models::VariableMuStage stage(cfg);
    stage.prepare(44100.0);

    const float testCv = static_cast<float>(cfg.cvMaxV * 0.5);
    stage.setCv(testCv);
    REQUIRE_THAT(stage.cv(), Catch::Matchers::WithinAbs(testCv, 1e-6f));
}

// ── DC operating point ────────────────────────────────────────────────────────

TEST_CASE("VariableMuStage: quiescent plate voltage is within supply rail at CV=0",
          "[variablemu][bias]")
{
    Analog::Models::VariableMuStageConfig cfg;
    Analog::Models::VariableMuStage stage(cfg);
    stage.prepare(44100.0);
    stage.setCv(0.0f);

    for (int i = 0; i < 2000; ++i)
        (void)stage.processSample(0.0f);

    const float out = stage.processSample(0.0f);
    const float Vp  = out * Analog::kVoltsPerSample;
    INFO("Quiescent Vp (CV=0) = " << Vp << " V");
    REQUIRE(Vp > 0.0f);
    REQUIRE(Vp < static_cast<float>(cfg.Vcc));
}

TEST_CASE("VariableMuStage: quiescent plate voltage is within supply rail at max CV",
          "[variablemu][bias]")
{
    Analog::Models::VariableMuStageConfig cfg;
    Analog::Models::VariableMuStage stage(cfg);
    stage.prepare(44100.0);
    stage.setCv(static_cast<float>(cfg.cvMaxV));

    for (int i = 0; i < 2000; ++i)
        (void)stage.processSample(0.0f);

    const float out = stage.processSample(0.0f);
    const float Vp  = out * Analog::kVoltsPerSample;
    INFO("Quiescent Vp (CV=cvMaxV) = " << Vp << " V");
    REQUIRE(Vp > 0.0f);
    REQUIRE(Vp < static_cast<float>(cfg.Vcc));
}

// ── Static GR curve: attenuation monotonically increases with CV ──────────────

/// Helper: measure the small-signal voltage gain at the given CV.
///
/// Warm up the stage, apply a sine wave at small amplitude, and return the
/// ratio of output to input peak-to-peak swing.  This is the static GR curve
/// test described in the issue.
static float measureGainAtCv(float cv, double sampleRate = 44100.0)
{
    const float amplitude = 0.005f; // small-signal: amplitude where triode operates with minimal distortion

    Analog::Models::VariableMuStage stage;
    stage.prepare(sampleRate);
    stage.setCv(cv);

    // Warm up to steady-state operating point.
    for (int i = 0; i < 3000; ++i)
        (void)stage.processSample(0.0f);

    // Measure peak-to-peak output.
    const float ptp = measurePeakToPeak(stage, amplitude, 100.0, sampleRate, 5);

    // Return normalised gain: ptp_out / ptp_in (both in sample units).
    const float ptpIn = 2.0f * amplitude;
    return ptp / ptpIn;
}

TEST_CASE("VariableMuStage: gain decreases monotonically as CV increases (static GR curve)",
          "[variablemu][gain-reduction][monotonicity]")
{
    // Sweep CV from 0 to cvMaxV in equal steps and verify that the small-signal
    // voltage gain is strictly non-increasing at each step.
    Analog::Models::VariableMuStageConfig cfg;
    const int steps = 7;
    const float cvStep = static_cast<float>(cfg.cvMaxV) / static_cast<float>(steps);

    float prevGain = measureGainAtCv(0.0f);
    INFO("Gain at CV=0: " << prevGain);
    REQUIRE(prevGain > 0.0f); // stage must have gain at CV=0

    for (int i = 1; i <= steps; ++i) {
        const float cv   = static_cast<float>(i) * cvStep;
        const float gain = measureGainAtCv(cv);
        INFO("Gain at CV=" << cv << " V: " << gain << "  (prev=" << prevGain << ")");

        // Each step must reduce (or at most hold) the gain.
        // Allow a tiny tolerance for numeric noise between nearly-equal steps.
        REQUIRE(gain < prevGain + 1e-3f);

        prevGain = gain;
    }
}

TEST_CASE("VariableMuStage: maximum CV produces measurable attenuation vs CV=0",
          "[variablemu][gain-reduction]")
{
    Analog::Models::VariableMuStageConfig cfg;
    const float gainAt0    = measureGainAtCv(0.0f);
    const float gainAtMax  = measureGainAtCv(static_cast<float>(cfg.cvMaxV));

    INFO("Gain at CV=0:    " << gainAt0);
    INFO("Gain at CV=max:  " << gainAtMax);

    // The maximum CV must produce at least 3 dB of gain reduction compared to
    // CV=0 (gain ratio ≥ √2 ≈ 1.41).
    REQUIRE(gainAt0 > gainAtMax * 1.41f);
}

TEST_CASE("VariableMuStage: cathode bypass capacitance can be changed safely at runtime",
          "[variablemu][bypass][runtime]")
{
    constexpr double sr      = 44100.0;
    constexpr float  amplitude = 0.01f;

    Analog::Models::VariableMuStage stage;
    stage.prepare(sr);
    stage.setCv(0.0f);

    const float ptpNoBypass = measurePeakToPeak(stage, amplitude, 1000.0, sr, 5);

    stage.setCathodeBypassCapacitance(47e-6);
    stage.reset();
    stage.setCv(0.0f);
    const float ptpWithBypass = measurePeakToPeak(stage, amplitude, 1000.0, sr, 5);

    INFO("ptp without Ck=" << ptpNoBypass);
    INFO("ptp with Ck=" << ptpWithBypass);
    REQUIRE(std::isfinite(ptpWithBypass));
    REQUIRE(ptpWithBypass > ptpNoBypass);
}

// ── No discontinuities: instantaneous CV change is smooth ─────────────────────

TEST_CASE("VariableMuStage: output has no large jump on CV step change",
          "[variablemu][stability][discontinuity]")
{
    // Apply a CV step change mid-stream and verify that the output does not
    // produce a large discontinuity.  The allowed jump is bounded by the
    // full supply-rail swing (in normalised units).
    Analog::Models::VariableMuStageConfig cfg;
    Analog::Models::VariableMuStage stage(cfg);
    stage.prepare(44100.0);
    stage.setCv(0.0f);

    // Warm up.
    for (int i = 0; i < 2000; ++i)
        (void)stage.processSample(0.0f);

    // Feed a signal and record the sample just before the CV step.
    const float inSample = 0.1f;
    const float outBefore = stage.processSample(inSample);

    // Step CV to maximum.
    stage.setCv(static_cast<float>(cfg.cvMaxV));

    const float outAfter = stage.processSample(inSample);

    // The per-sample change in output must stay within the full supply rail.
    const float maxJump = static_cast<float>(cfg.Vcc) / Analog::kVoltsPerSample;
    const float jump    = std::abs(outAfter - outBefore);
    INFO("Output before CV step: " << outBefore);
    INFO("Output after  CV step: " << outAfter);
    INFO("Jump: " << jump << "  maxAllowed: " << maxJump);
    REQUIRE(jump < maxJump);
}

// ── Lifecycle: prepare / reset ────────────────────────────────────────────────

TEST_CASE("VariableMuStage: reset restores initial conditions",
          "[variablemu][lifecycle]")
{
    Analog::Models::VariableMuStage stage;
    stage.prepare(44100.0);
    stage.setCv(2.0f);

    // Collect output from first run.
    std::vector<float> run1;
    for (int i = 0; i < 200; ++i)
        run1.push_back(stage.processSample(i % 2 == 0 ? 0.01f : -0.01f));

    // Reset, re-apply the same CV, and repeat.
    stage.reset();
    stage.setCv(2.0f);
    for (int i = 0; i < 200; ++i) {
        const float out = stage.processSample(i % 2 == 0 ? 0.01f : -0.01f);
        REQUIRE_THAT(out, Catch::Matchers::WithinAbs(run1[i], 1e-6f));
    }
}

TEST_CASE("VariableMuStage: reset clears CV to zero",
          "[variablemu][lifecycle]")
{
    Analog::Models::VariableMuStage stage;
    stage.prepare(44100.0);
    stage.setCv(4.0f);
    stage.reset();
    REQUIRE_THAT(stage.cv(), Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("VariableMuStage: different sample rates produce finite output",
          "[variablemu][lifecycle]")
{
    for (double sr : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        for (float cv : {0.0f, 3.0f, 6.0f}) {
            Analog::Models::VariableMuStage stage;
            stage.prepare(sr);
            stage.setCv(cv);
            INFO("sampleRate=" << sr << " CV=" << cv);
            for (int i = 0; i < 100; ++i) {
                const float out = stage.processSample(0.1f);
                REQUIRE(std::isfinite(out));
            }
        }
    }
}
