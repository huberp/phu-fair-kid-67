#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "src/DSP/Models/Transformer/TransformerSecondOrder.h"

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

/// Measure the steady-state amplitude for a sine wave through the transformer.
static float measureAmplitude(TransformerSecondOrder& xfmr,
                               double sampleRate,
                               double freqHz,
                               float  inputAmp   = 0.1f,
                               int    settleN    = 8192,
                               int    measureN   = 4096)
{
    for (int i = 0; i < settleN; ++i) {
        const float in = inputAmp * static_cast<float>(
            std::sin(2.0 * M_PI * freqHz * i / sampleRate));
        (void)xfmr.processSample(in);
    }

    double sumSq = 0.0;
    for (int i = 0; i < measureN; ++i) {
        const float in = inputAmp * static_cast<float>(
            std::sin(2.0 * M_PI * freqHz * (settleN + i) / sampleRate));
        const float out = xfmr.processSample(in);
        sumSq += static_cast<double>(out) * static_cast<double>(out);
    }
    return static_cast<float>(std::sqrt(sumSq / measureN));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("TransformerSecondOrder: constructs and prepares without crashing", "[tfmr2]")
{
    TransformerSecondOrder xfmr;
    REQUIRE_NOTHROW(xfmr.prepare(44100.0));
}

TEST_CASE("TransformerSecondOrder: output is finite for typical signals", "[tfmr2]")
{
    TransformerSecondOrder xfmr;
    xfmr.prepare(44100.0);

    for (float amp : { 0.0f, 0.01f, 0.1f, 0.5f, 1.0f }) {
        for (int i = 0; i < 512; ++i) {
            const float out = xfmr.processSample(
                amp * std::sin(static_cast<float>(i) * 0.1f));
            INFO("amp=" << amp << " i=" << i << " out=" << out);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("TransformerSecondOrder: HPF attenuates deep bass", "[tfmr2]")
{
    // Frequencies well below the HPF corner should be significantly attenuated.
    const double sampleRate  = 44100.0;
    const double hpfCutoffHz = 200.0; // deliberately high for testability

    TransformerSecondOrderConfig cfg;
    cfg.hpfCutoffHz = hpfCutoffHz;
    cfg.lpfCutoffHz = 20000.0;
    cfg.drive       = 1.0f;

    TransformerSecondOrder xfmrFar, xfmrPass;
    xfmrFar.setConfig(cfg);   xfmrFar.prepare(sampleRate);
    xfmrPass.setConfig(cfg);  xfmrPass.prepare(sampleRate);

    const float rmsLow  = measureAmplitude(xfmrFar,  sampleRate, hpfCutoffHz / 20.0); // far below
    const float rmsPass = measureAmplitude(xfmrPass, sampleRate, 1000.0);              // mid-band

    INFO("rmsLow=" << rmsLow << " rmsPass=" << rmsPass);
    REQUIRE(rmsPass > 0.001f);  // mid-band should pass
    REQUIRE(rmsLow < rmsPass);  // sub-bass should be attenuated
}

TEST_CASE("TransformerSecondOrder: LPF attenuates high frequencies", "[tfmr2]")
{
    const double sampleRate  = 44100.0;
    const double lpfCutoffHz = 1000.0; // deliberately low for testability

    TransformerSecondOrderConfig cfg;
    cfg.hpfCutoffHz = 20.0;
    cfg.lpfCutoffHz = lpfCutoffHz;
    cfg.drive       = 1.0f;

    TransformerSecondOrder xfmrFar, xfmrPass;
    xfmrFar.setConfig(cfg);  xfmrFar.prepare(sampleRate);
    xfmrPass.setConfig(cfg); xfmrPass.prepare(sampleRate);

    const float rmsHigh = measureAmplitude(xfmrFar,  sampleRate, lpfCutoffHz * 10.0); // far above
    const float rmsPass = measureAmplitude(xfmrPass, sampleRate, lpfCutoffHz / 5.0);  // in band

    INFO("rmsHigh=" << rmsHigh << " rmsPass=" << rmsPass);
    REQUIRE(rmsPass > 0.001f);
    REQUIRE(rmsHigh < rmsPass);
}

TEST_CASE("TransformerSecondOrder: roll-off is steeper than first-order (12 dB/octave)", "[tfmr2]")
{
    // A 2nd-order HPF rolls off at 12 dB/octave, so halving the frequency
    // from the −3 dB point should give ≈12 dB of additional attenuation
    // (compared to ≈6 dB for a first-order filter at the same cutoff).
    const double sampleRate  = 44100.0;
    const double hpfCutoffHz = 500.0;

    TransformerSecondOrderConfig cfg;
    cfg.hpfCutoffHz = hpfCutoffHz;
    cfg.lpfCutoffHz = 20000.0;
    cfg.drive       = 1.0f;
    cfg.hpfQ        = 0.7071f;

    TransformerSecondOrder xfmrA, xfmrB;
    xfmrA.setConfig(cfg); xfmrA.prepare(sampleRate);
    xfmrB.setConfig(cfg); xfmrB.prepare(sampleRate);

    // Measure at fc/2 and fc/4.
    const float rmsHalf    = measureAmplitude(xfmrA, sampleRate, hpfCutoffHz * 0.5, 0.1f, 16384, 8192);
    const float rmsQuarter = measureAmplitude(xfmrB, sampleRate, hpfCutoffHz * 0.25, 0.1f, 16384, 8192);

    INFO("rmsHalf=" << rmsHalf << " rmsQuarter=" << rmsQuarter);
    REQUIRE(rmsHalf > 0.0f);
    REQUIRE(rmsQuarter > 0.0f);

    // Going from fc/2 to fc/4 (one octave further away from corner):
    //   1st-order: extra ~6 dB attenuation.
    //   2nd-order: extra ~12 dB attenuation.
    // We just verify >6 dB extra attenuation to confirm it's steeper than 1st-order.
    const float extraAttenuationDb = 20.0f * std::log10(rmsHalf / rmsQuarter);
    INFO("extra attenuation per octave = " << extraAttenuationDb << " dB");
    REQUIRE(extraAttenuationDb > 6.0f);
}

TEST_CASE("TransformerSecondOrder: saturator limits output amplitude at high drive", "[tfmr2]")
{
    // With high drive the tanh saturator should clip large signals.
    TransformerSecondOrderConfig cfg;
    cfg.hpfCutoffHz = 20.0;
    cfg.lpfCutoffHz = 20000.0;
    cfg.drive       = 10.0f; // strong saturation

    TransformerSecondOrder xfmr;
    xfmr.setConfig(cfg);
    xfmr.prepare(44100.0);

    // Input amplitude well above saturation level (1/drive).
    const float largeAmp = 2.0f;
    float maxOut = 0.0f;
    for (int i = 0; i < 4096; ++i) {
        const float out = std::abs(xfmr.processSample(
            largeAmp * std::sin(static_cast<float>(i) * 0.1f)));
        maxOut = std::max(maxOut, out);
    }

    // tanh saturator: peak output ≤ 1/drive = 0.1, but give a wide tolerance.
    INFO("maxOut=" << maxOut);
    REQUIRE(maxOut < largeAmp * 0.5f); // clearly attenuated
}

TEST_CASE("TransformerSecondOrder: Q parameter changes resonance (higher Q = more peaked)", "[tfmr2]")
{
    // A higher Q on the LPF biquad produces a resonance peak just before
    // the corner frequency.  Measure at 0.9 × lpfCutoff for low-Q vs high-Q.
    const double sampleRate  = 44100.0;
    const double lpfCutoffHz = 4000.0;

    TransformerSecondOrderConfig cfgLow, cfgHigh;
    cfgLow.hpfCutoffHz  = cfgHigh.hpfCutoffHz  = 20.0;
    cfgLow.lpfCutoffHz  = cfgHigh.lpfCutoffHz  = lpfCutoffHz;
    cfgLow.drive        = cfgHigh.drive        = 1.0f;
    cfgLow.lpfQ         = 0.5f;  // under-damped — less pronounced peak
    cfgHigh.lpfQ        = 2.0f;  // over-damped  — pronounced resonance peak

    TransformerSecondOrder xfmrLow, xfmrHigh;
    xfmrLow.setConfig(cfgLow);   xfmrLow.prepare(sampleRate);
    xfmrHigh.setConfig(cfgHigh); xfmrHigh.prepare(sampleRate);

    const float rmsLow  = measureAmplitude(xfmrLow,  sampleRate, lpfCutoffHz * 0.9, 0.05f, 16384, 8192);
    const float rmsHigh = measureAmplitude(xfmrHigh, sampleRate, lpfCutoffHz * 0.9, 0.05f, 16384, 8192);

    INFO("rmsLow(Q=0.5)=" << rmsLow << " rmsHigh(Q=2.0)=" << rmsHigh);
    // High Q produces more gain near the corner frequency.
    REQUIRE(rmsHigh > rmsLow);
}

TEST_CASE("TransformerSecondOrder: setConfig replaces configuration (prepare required after)", "[tfmr2]")
{
    TransformerSecondOrder xfmr;
    xfmr.prepare(44100.0);

    TransformerSecondOrderConfig newCfg;
    newCfg.hpfCutoffHz = 50.0;
    newCfg.lpfCutoffHz = 15000.0;
    newCfg.drive       = 1.5f;

    REQUIRE_NOTHROW(xfmr.setConfig(newCfg));
    // setConfig alone does not recompute biquad coefficients; caller must re-prepare.
    xfmr.prepare(44100.0);
    REQUIRE(std::isfinite(xfmr.processSample(0.1f)));
}

TEST_CASE("TransformerSecondOrder: reset clears state", "[tfmr2]")
{
    TransformerSecondOrder xfmr;
    xfmr.prepare(44100.0);

    // Drive the filter state with a loud signal.
    for (int i = 0; i < 2000; ++i)
        (void)xfmr.processSample(1.0f * std::sin(static_cast<float>(i) * 0.1f));

    xfmr.reset();

    // After reset, the first output should be near zero (filter state cleared).
    const float out = xfmr.processSample(0.0f);
    INFO("output after reset: " << out);
    REQUIRE(std::abs(out) < 1e-6f);
}

TEST_CASE("TransformerSecondOrder: TransformerSecondOrderConfig inherits LinearConfig fields", "[tfmr2]")
{
    // Verify the inheritance: accesses to hpfCutoffHz / lpfCutoffHz / drive
    // on a TransformerSecondOrderConfig object must compile and round-trip.
    TransformerSecondOrderConfig cfg;
    cfg.hpfCutoffHz = 42.0;
    cfg.lpfCutoffHz = 18000.0;
    cfg.drive       = 1.3f;
    cfg.hpfQ        = 0.8f;
    cfg.lpfQ        = 0.9f;

    REQUIRE(cfg.hpfCutoffHz == 42.0);
    REQUIRE(cfg.lpfCutoffHz == 18000.0);
    REQUIRE(cfg.drive       == 1.3f);
    REQUIRE(cfg.hpfQ        == 0.8f);
    REQUIRE(cfg.lpfQ        == 0.9f);
}
