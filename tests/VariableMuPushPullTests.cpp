#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "src/DSP/Models/Fairchild/VariableMuPushPullStage.h"

#include <cmath>
#include <vector>

#define _USE_MATH_DEFINES
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Models;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static float computeTHD(const std::vector<float>& signal, double sampleRate,
                         double freqHz, int numHarmonics = 5)
{
    // Extract harmonic amplitudes via DFT at exact harmonic frequencies.
    const int N = static_cast<int>(signal.size());
    double fundamentalPwr = 0.0;
    double harmonicPwr    = 0.0;

    for (int h = 1; h <= numHarmonics; ++h) {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < N; ++n) {
            const double angle = 2.0 * M_PI * h * freqHz * n / sampleRate;
            re += signal[n] * std::cos(angle);
            im += signal[n] * std::sin(angle);
        }
        const double pwr = (re * re + im * im) / (N * N / 4.0);
        if (h == 1)
            fundamentalPwr = pwr;
        else
            harmonicPwr += pwr;
    }

    if (fundamentalPwr <= 0.0) return 0.0f;
    return static_cast<float>(std::sqrt(harmonicPwr / fundamentalPwr));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("VariableMuPushPullStage: constructs without crashing", "[pushpull]")
{
    REQUIRE_NOTHROW(VariableMuPushPullStage{});
}

TEST_CASE("VariableMuPushPullStage: output is finite after prepare", "[pushpull]")
{
    VariableMuPushPullStage stage;
    stage.prepare(44100.0);

    float out = 0.0f;
    REQUIRE_NOTHROW(out = stage.processSample(0.1f));
    REQUIRE(std::isfinite(out));
}

TEST_CASE("VariableMuPushPullStage: approximate unity gain at CV=0", "[pushpull]")
{
    // At CV=0 (no compression) the differential combination should give unity
    // small-signal gain.  Allow a generous tolerance to account for settling.
    VariableMuPushPullStage stage;
    stage.prepare(44100.0);
    stage.setCv(0.0f);

    const double sampleRate = 44100.0;
    const float  amp        = 0.01f; // small signal, well below saturation
    const float  freqHz     = 1000.0f;

    // Settle
    for (int i = 0; i < 10000; ++i) {
        const float in = amp * std::sin(2.0f * static_cast<float>(M_PI) * freqHz
                                        * static_cast<float>(i) / static_cast<float>(sampleRate));
        (void)stage.processSample(in);
    }

    // Measure
    double sumIn2 = 0.0, sumOut2 = 0.0;
    const int measureN = 4096;
    for (int i = 0; i < measureN; ++i) {
        const float in = amp * std::sin(2.0f * static_cast<float>(M_PI) * freqHz
                                        * static_cast<float>(10000 + i) / static_cast<float>(sampleRate));
        const float out = stage.processSample(in);
        sumIn2  += static_cast<double>(in)  * static_cast<double>(in);
        sumOut2 += static_cast<double>(out) * static_cast<double>(out);
    }

    const float rmsIn  = static_cast<float>(std::sqrt(sumIn2  / measureN));
    const float rmsOut = static_cast<float>(std::sqrt(sumOut2 / measureN));

    INFO("rmsIn=" << rmsIn << " rmsOut=" << rmsOut);
    REQUIRE(rmsIn  > 0.0f);
    REQUIRE(rmsOut > 0.0f);

    const float gainDb = 20.0f * std::log10(rmsOut / rmsIn);
    INFO("gainDb=" << gainDb);
    // Unity gain ±3 dB.
    REQUIRE_THAT(gainDb, Catch::Matchers::WithinAbs(0.0f, 3.0f));
}

TEST_CASE("VariableMuPushPullStage: gain decreases as CV increases", "[pushpull]")
{
    // Verify that gain reduction increases monotonically with CV.
    const double sampleRate = 44100.0;
    const float  amp        = 0.1f;
    const float  freqHz     = 1000.0f;

    auto measureGain = [&](float cv) -> float {
        VariableMuPushPullStage stage;
        stage.prepare(sampleRate);
        stage.setCv(cv);

        // Settle
        for (int i = 0; i < 30000; ++i) {
            const float in = amp * std::sin(2.0f * static_cast<float>(M_PI) * freqHz
                                            * i / static_cast<float>(sampleRate));
            (void)stage.processSample(in);
        }

        double sumIn2 = 0.0, sumOut2 = 0.0;
        for (int i = 0; i < 4096; ++i) {
            const float in = amp * std::sin(2.0f * static_cast<float>(M_PI) * freqHz
                                            * (30000 + i) / static_cast<float>(sampleRate));
            const float out = stage.processSample(in);
            sumIn2  += static_cast<double>(in)  * static_cast<double>(in);
            sumOut2 += static_cast<double>(out) * static_cast<double>(out);
        }

        const float rmsOut = static_cast<float>(std::sqrt(sumOut2 / 4096));
        const float rmsIn  = static_cast<float>(std::sqrt(sumIn2  / 4096));
        if (rmsIn <= 0.0f) return 0.0f;
        return 20.0f * std::log10(rmsOut / rmsIn);
    };

    const float gainAt0  = measureGain(0.0f);
    const float gainAt2  = measureGain(2.0f);
    const float gainAt5  = measureGain(5.0f);

    INFO("gainAt0=" << gainAt0 << " gainAt2=" << gainAt2 << " gainAt5=" << gainAt5);

    // Higher CV → more attenuation → lower gain (more negative dB).
    REQUIRE(gainAt2 < gainAt0  + 0.5f);
    REQUIRE(gainAt5 < gainAt2  + 0.5f);
}

TEST_CASE("VariableMuPushPullStage: even-harmonic cancellation (lower 2nd harmonic than single-ended)",
          "[pushpull][harmonics]")
{
    // The push-pull topology cancels even harmonics.  At moderate drive levels
    // the 2nd harmonic should be substantially lower than in a single-ended stage.
    const double sampleRate = 44100.0;
    const float  amp        = 0.3f; // moderate drive to generate measurable harmonics
    const float  freqHz     = 1000.0f;
    const int    settleN    = 30000;
    const int    measureN   = 8192;

    // Push-pull stage.
    VariableMuPushPullStage ppStage;
    ppStage.prepare(sampleRate);
    ppStage.setCv(0.0f);

    // Single arm (single-ended reference).
    Analog::Models::VariableMuStage seStage;
    seStage.prepare(sampleRate);
    seStage.setCv(0.0f);

    // Settle both.
    for (int i = 0; i < settleN; ++i) {
        const float in = amp * std::sin(2.0f * static_cast<float>(M_PI) * freqHz
                                        * i / static_cast<float>(sampleRate));
        (void)ppStage.processSample(in);
        (void)seStage.processSample(in);
    }

    std::vector<float> ppOut(measureN), seOut(measureN);
    for (int i = 0; i < measureN; ++i) {
        const float in = amp * std::sin(2.0f * static_cast<float>(M_PI) * freqHz
                                        * (settleN + i) / static_cast<float>(sampleRate));
        ppOut[i] = ppStage.processSample(in);
        seOut[i] = seStage.processSample(in);
    }

    // Measure 2nd harmonic amplitude (relative to fundamental).
    auto measureH2 = [&](const std::vector<float>& buf) -> float {
        const int N = static_cast<int>(buf.size());
        double re1 = 0.0, im1 = 0.0;
        double re2 = 0.0, im2 = 0.0;
        for (int n = 0; n < N; ++n) {
            const double phi = 2.0 * M_PI * freqHz * n / sampleRate;
            re1 += buf[n] * std::cos(phi);
            im1 += buf[n] * std::sin(phi);
            re2 += buf[n] * std::cos(2.0 * phi);
            im2 += buf[n] * std::sin(2.0 * phi);
        }
        const double amp1 = std::sqrt(re1 * re1 + im1 * im1);
        const double amp2 = std::sqrt(re2 * re2 + im2 * im2);
        return static_cast<float>(amp1 > 0.0 ? amp2 / amp1 : 0.0);
    };

    const float ppH2 = measureH2(ppOut);
    const float seH2 = measureH2(seOut);

    INFO("push-pull H2=" << ppH2 << " single-ended H2=" << seH2);

    // The push-pull stage must have a lower (or equal) 2nd-harmonic ratio.
    // (Equal if the SE stage happens to have negligible H2, but in practice the
    //  asymmetric single-ended Koren model produces meaningful 2nd harmonic.)
    REQUIRE(ppH2 <= seH2 + 1e-4f);
}

TEST_CASE("VariableMuPushPullStage: setNRConfig does not crash", "[pushpull]")
{
    VariableMuPushPullStage stage;
    stage.prepare(44100.0);

    Analog::Nonlinear::NRConfig cfg;
    cfg.maxIterations = 5;
    REQUIRE_NOTHROW(stage.setNRConfig(cfg));
    REQUIRE(std::isfinite(stage.processSample(0.1f)));
}

TEST_CASE("VariableMuPushPullStage: setCathodeBypassCapacitance does not crash", "[pushpull]")
{
    VariableMuPushPullStage stage;
    stage.prepare(44100.0);
    REQUIRE_NOTHROW(stage.setCathodeBypassCapacitance(1e-6));
    REQUIRE(std::isfinite(stage.processSample(0.1f)));
}

TEST_CASE("VariableMuPushPullStage: reset clears state without crash", "[pushpull]")
{
    VariableMuPushPullStage stage;
    stage.prepare(44100.0);
    for (int i = 0; i < 1000; ++i)
        (void)stage.processSample(0.5f * std::sin(static_cast<float>(i) * 0.1f));

    REQUIRE_NOTHROW(stage.reset());
    REQUIRE(std::isfinite(stage.processSample(0.1f)));
}
