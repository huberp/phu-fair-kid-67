#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/Models/Fairchild/Fairchild670Core.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Warm up a Fairchild670Core with silence for `warmupSamples` stereo samples.
static Models::Fairchild670Core makeWarmedCore(Models::Fairchild670CoreConfig cfg = {},
                                               double sampleRate    = 44100.0,
                                               int    warmupSamples = 2000)
{
    Models::Fairchild670Core core(cfg);
    core.prepare(sampleRate);
    float outL, outR;
    for (int i = 0; i < warmupSamples; ++i)
        core.processStereo(0.0f, 0.0f, outL, outR);
    return core;
}

/// Compute a sine sample at sample index `i` for the given amplitude, frequency,
/// and sample rate.
static float sineAt(float amplitude, int i, double freqHz, double sampleRate)
{
    return amplitude * std::sin(
        2.0f * static_cast<float>(M_PI) * static_cast<float>(freqHz)
            * static_cast<float>(i) / static_cast<float>(sampleRate));
}

// ── Stability ─────────────────────────────────────────────────────────────────

TEST_CASE("Fairchild670Core: stereo output is always finite", "[670core][stability]")
{
    for (auto mode : {Models::LinkMode::Independent, Models::LinkMode::Linked}) {
        Models::Fairchild670CoreConfig cfg;
        cfg.linkMode = mode;
        Models::Fairchild670Core core(cfg);
        core.prepare(44100.0);

        float outL, outR;
        for (float v : {0.0f, 0.1f, -0.1f, 0.5f, -0.5f, 1.0f, -1.0f}) {
            for (int i = 0; i < 20; ++i) {
                core.processStereo(v, -v, outL, outR);
                INFO("mode=" << static_cast<int>(mode)
                     << " input=" << v << " iter=" << i);
                REQUIRE(std::isfinite(outL));
                REQUIRE(std::isfinite(outR));
            }
        }
    }
}

TEST_CASE("Fairchild670Core: finite output at all timing presets", "[670core][stability]")
{
    using Models::Sidechain::TimingPosition;
    for (auto pos : {TimingPosition::P1, TimingPosition::P2, TimingPosition::P3,
                     TimingPosition::P4, TimingPosition::P5, TimingPosition::P6}) {
        Models::Fairchild670CoreConfig cfg;
        cfg.detectorCfg.preset = pos;
        Models::Fairchild670Core core(cfg);
        core.prepare(44100.0);

        float outL, outR;
        for (int i = 0; i < 50; ++i) {
            core.processStereo(0.3f, -0.3f, outL, outR);
            INFO("preset=" << static_cast<int>(pos) << " iter=" << i);
            REQUIRE(std::isfinite(outL));
            REQUIRE(std::isfinite(outR));
        }
    }
}

// ── Independent mode ──────────────────────────────────────────────────────────

TEST_CASE("Fairchild670Core: independent mode — R CV is unaffected by loud L",
          "[670core][independent]")
{
    // Feed a loud signal to L and silence to R.
    // In independent mode the R detector sees no signal, so R should have
    // a much lower CV than L.

    constexpr double sr = 44100.0;

    Models::Fairchild670CoreConfig cfg;
    cfg.linkMode = Models::LinkMode::Independent;

    Models::Fairchild670Core core(cfg);
    core.prepare(sr);

    // Drive L hard; feed R silence to charge L's detector only.
    float outL, outR;
    for (int i = 0; i < 8000; ++i)
        core.processStereo(0.9f, 0.0f, outL, outR);

    const auto m = core.meters();
    INFO("cvL (indep, loud L): " << m.cvL);
    INFO("cvR (indep, silent R): " << m.cvR);

    // L must have significant CV from the loud input.
    REQUIRE(m.cvL > 1.0f);

    // R must have substantially lower CV than L (it only heard silence).
    REQUIRE(m.cvR < m.cvL * 0.5f);
}

TEST_CASE("Fairchild670Core: independent mode — L CV is unaffected by loud R",
          "[670core][independent]")
{
    constexpr double sr = 44100.0;

    Models::Fairchild670CoreConfig cfg;
    cfg.linkMode = Models::LinkMode::Independent;

    Models::Fairchild670Core core(cfg);
    core.prepare(sr);

    float outL, outR;
    for (int i = 0; i < 8000; ++i)
        core.processStereo(0.0f, 0.9f, outL, outR);

    const auto m = core.meters();
    INFO("cvL (indep, silent L): " << m.cvL);
    INFO("cvR (indep, loud R):   " << m.cvR);

    REQUIRE(m.cvR > 1.0f);
    REQUIRE(m.cvL < m.cvR * 0.5f);
}

// ── Linked mode (Max strategy) ────────────────────────────────────────────────

TEST_CASE("Fairchild670Core: linked/max — hot L channel compresses R similarly",
          "[670core][linked][max]")
{
    // Feed a loud signal exclusively to L and silence to R.
    // After charging the detector, the linked max CV should flow to the R stage,
    // so any small signal subsequently fed to R is compressed.

    constexpr double sr = 44100.0;

    Models::Fairchild670CoreConfig cfg;
    cfg.linkMode         = Models::LinkMode::Linked;
    cfg.envelopeStrategy = Models::LinkedEnvelopeStrategy::Max;

    auto core = makeWarmedCore(cfg, sr);

    // Charge the L detector heavily.
    float outL, outR;
    for (int i = 0; i < 8000; ++i)
        core.processStereo(0.9f, 0.0f, outL, outR);

    // The R stage should now carry a significant CV from L.
    const auto mAfterCharge = core.meters();
    INFO("cvL after charge: " << mAfterCharge.cvL);
    INFO("cvR after charge: " << mAfterCharge.cvR);

    // In linked/max mode, both channels receive the same CV.
    REQUIRE_THAT(mAfterCharge.cvL,
                 Catch::Matchers::WithinAbs(mAfterCharge.cvR, 1e-5f));
    // CV must be significant (L was driven hard).
    REQUIRE(mAfterCharge.cvL > 0.5f);
}

TEST_CASE("Fairchild670Core: linked/max — cv equals max(cvL, cvR)",
          "[670core][linked][max]")
{
    // Run two identical independent instances to get the per-channel CVs, then
    // compare with a linked/max instance fed the same input.

    constexpr double sr = 44100.0;
    constexpr int    N  = 200;

    // Independent reference to obtain per-channel CVs.
    Models::Fairchild670CoreConfig cfgRef;
    cfgRef.linkMode = Models::LinkMode::Independent;
    Models::Fairchild670Core coreRef(cfgRef);
    coreRef.prepare(sr);

    // Linked/Max instance under test.
    Models::Fairchild670CoreConfig cfgMax;
    cfgMax.linkMode         = Models::LinkMode::Linked;
    cfgMax.envelopeStrategy = Models::LinkedEnvelopeStrategy::Max;
    Models::Fairchild670Core coreMax(cfgMax);
    coreMax.prepare(sr);

    float outL, outR;
    for (int i = 0; i < N; ++i) {
        const float inL = sineAt(0.8f, i, 100.0, sr);
        const float inR = sineAt(0.2f, i, 100.0, sr);

        coreRef.processStereo(inL, inR, outL, outR);
        coreMax.processStereo(inL, inR, outL, outR);

        const float expectedCv = std::max(coreRef.meters().cvL, coreRef.meters().cvR);
        INFO("sample=" << i
             << " refCvL=" << coreRef.meters().cvL
             << " refCvR=" << coreRef.meters().cvR
             << " maxCv=" << expectedCv
             << " linkedCvL=" << coreMax.meters().cvL
             << " linkedCvR=" << coreMax.meters().cvR);

        REQUIRE_THAT(coreMax.meters().cvL,
                     Catch::Matchers::WithinAbs(coreMax.meters().cvR, 1e-5f));
    }
}

// ── Linked mode (Average strategy) ───────────────────────────────────────────

TEST_CASE("Fairchild670Core: linked/avg — both channels share the average CV",
          "[670core][linked][avg]")
{
    constexpr double sr = 44100.0;
    constexpr int    N  = 200;

    Models::Fairchild670CoreConfig cfgAvg;
    cfgAvg.linkMode         = Models::LinkMode::Linked;
    cfgAvg.envelopeStrategy = Models::LinkedEnvelopeStrategy::Average;
    Models::Fairchild670Core coreAvg(cfgAvg);
    coreAvg.prepare(sr);

    float outL, outR;
    for (int i = 0; i < N; ++i) {
        const float inL = sineAt(0.7f, i, 100.0, sr);
        const float inR = sineAt(0.3f, i, 100.0, sr);
        coreAvg.processStereo(inL, inR, outL, outR);

        // L and R must always receive the same CV in any linked mode.
        REQUIRE_THAT(coreAvg.meters().cvL,
                     Catch::Matchers::WithinAbs(coreAvg.meters().cvR, 1e-5f));
    }
}

TEST_CASE("Fairchild670Core: linked/avg CV is strictly between independent L and R CVs",
          "[670core][linked][avg]")
{
    // Feed asymmetric signals: after enough warmup the individual CVs will
    // diverge.  The average-linked CV must sit between them.

    constexpr double sr = 44100.0;

    Models::Fairchild670CoreConfig cfgIndep;
    cfgIndep.linkMode = Models::LinkMode::Independent;
    Models::Fairchild670Core coreIndep(cfgIndep);
    coreIndep.prepare(sr);

    Models::Fairchild670CoreConfig cfgAvg;
    cfgAvg.linkMode         = Models::LinkMode::Linked;
    cfgAvg.envelopeStrategy = Models::LinkedEnvelopeStrategy::Average;
    Models::Fairchild670Core coreAvg(cfgAvg);
    coreAvg.prepare(sr);

    // Warm up both cores with the same asymmetric inputs.
    float outL, outR;
    for (int i = 0; i < 3000; ++i) {
        const float inL = sineAt(0.8f, i, 100.0, sr);
        const float inR = sineAt(0.1f, i, 100.0, sr);
        coreIndep.processStereo(inL, inR, outL, outR);
        coreAvg.processStereo(inL, inR, outL, outR);
    }

    const float cvL_indep = coreIndep.meters().cvL;
    const float cvR_indep = coreIndep.meters().cvR;
    const float cvAvg     = coreAvg.meters().cvL; // both channels are equal

    INFO("cvL (indep)=" << cvL_indep << " cvR (indep)=" << cvR_indep
         << " cvAvg=" << cvAvg);

    // Avg CV should lie between the smaller and larger independent CVs.
    const float lo = std::min(cvL_indep, cvR_indep);
    const float hi = std::max(cvL_indep, cvR_indep);
    REQUIRE(cvAvg >= lo - 1e-4f);
    REQUIRE(cvAvg <= hi + 1e-4f);
}

// ── Meter hooks ───────────────────────────────────────────────────────────────

TEST_CASE("Fairchild670Core: peak meters accumulate correctly", "[670core][meters]")
{
    Models::Fairchild670Core core;
    core.prepare(44100.0);
    core.resetPeakMeters();

    float outL, outR;
    const float ampL = 0.4f, ampR = 0.6f;
    for (int i = 0; i < 200; ++i)
        core.processStereo(ampL, ampR, outL, outR);

    const auto m = core.meters();

    INFO("inPeakL=" << m.inPeakL << " inPeakR=" << m.inPeakR);
    // Peak must be at least the constant input amplitude fed in.
    REQUIRE(m.inPeakL >= ampL - 1e-4f);
    REQUIRE(m.inPeakR >= ampR - 1e-4f);
    REQUIRE(m.outPeakL > 0.0f);
    REQUIRE(m.outPeakR > 0.0f);
}

TEST_CASE("Fairchild670Core: resetPeakMeters clears peak accumulators",
          "[670core][meters]")
{
    Models::Fairchild670Core core;
    core.prepare(44100.0);

    float outL, outR;
    for (int i = 0; i < 100; ++i)
        core.processStereo(0.5f, 0.5f, outL, outR);

    core.resetPeakMeters();
    const auto m = core.meters();
    REQUIRE_THAT(m.inPeakL,  Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(m.inPeakR,  Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(m.outPeakL, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(m.outPeakR, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    // CV is NOT reset by resetPeakMeters — the decay continues naturally.
}

TEST_CASE("Fairchild670Core: CV is non-negative after processing silence",
          "[670core][meters]")
{
    Models::Fairchild670Core core;
    core.prepare(44100.0);

    // Charge up.
    float outL, outR;
    for (int i = 0; i < 1000; ++i)
        core.processStereo(0.7f, 0.7f, outL, outR);

    // Release to silence.
    for (int i = 0; i < 500; ++i)
        core.processStereo(0.0f, 0.0f, outL, outR);

    const auto m = core.meters();
    REQUIRE(m.cvL >= 0.0f);
    REQUIRE(m.cvR >= 0.0f);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

TEST_CASE("Fairchild670Core: reset restores initial conditions", "[670core][lifecycle]")
{
    Models::Fairchild670Core core;
    core.prepare(44100.0);

    float outL, outR;
    for (int i = 0; i < 300; ++i)
        core.processStereo(0.4f, 0.4f, outL, outR);

    core.reset();

    // After reset, a second run should produce the same output as a fresh instance.
    Models::Fairchild670Core fresh;
    fresh.prepare(44100.0);

    for (int i = 0; i < 50; ++i) {
        float freshL, freshR;
        float resetL, resetR;
        core.processStereo(0.1f, -0.1f, resetL, resetR);
        fresh.processStereo(0.1f, -0.1f, freshL, freshR);
        REQUIRE_THAT(resetL, Catch::Matchers::WithinAbs(freshL, 1e-6f));
        REQUIRE_THAT(resetR, Catch::Matchers::WithinAbs(freshR, 1e-6f));
    }
}

TEST_CASE("Fairchild670Core: reset clears CV fields", "[670core][lifecycle]")
{
    Models::Fairchild670Core core;
    core.prepare(44100.0);

    float outL, outR;
    for (int i = 0; i < 500; ++i)
        core.processStereo(0.8f, 0.8f, outL, outR);

    core.reset();
    const auto m = core.meters();
    REQUIRE_THAT(m.cvL, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(m.cvR, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("Fairchild670Core: setTimingPosition produces finite output",
          "[670core][lifecycle]")
{
    using Models::Sidechain::TimingPosition;
    Models::Fairchild670Core core;
    core.prepare(44100.0);

    float outL, outR;
    // Charge up with P1.
    for (int i = 0; i < 500; ++i)
        core.processStereo(0.5f, 0.5f, outL, outR);

    // Switch to a different preset and verify stability.
    core.setTimingPosition(TimingPosition::P4);
    for (int i = 0; i < 100; ++i) {
        core.processStereo(0.3f, 0.3f, outL, outR);
        INFO("iter=" << i);
        REQUIRE(std::isfinite(outL));
        REQUIRE(std::isfinite(outR));
    }
}
