#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/Models/Transformer/TransformerLinear.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Measure the RMS output level of a sine burst at `freqHz` after the filter
/// has been given `warmupSamples` of silence to settle.
static float measureRMS(Models::TransformerLinear& xfmr,
                         float amplitude, double freqHz,
                         double sampleRate = 44100.0,
                         int numSamples = 4096)
{
    // Warm up with silence.
    for (int i = 0; i < 512; ++i)
        (void)xfmr.processSample(0.0f);

    double sumSq = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        const float in  = amplitude *
            static_cast<float>(std::sin(2.0 * M_PI * freqHz * i / sampleRate));
        const float out = xfmr.processSample(in);
        REQUIRE(std::isfinite(out));
        sumSq += static_cast<double>(out) * static_cast<double>(out);
    }
    return static_cast<float>(std::sqrt(sumSq / numSamples));
}

/// Measure peak-to-peak output swing of a sine wave over several cycles.
static float measurePeakToPeak(Models::TransformerLinear& xfmr,
                                float amplitude, double freqHz = 1000.0,
                                double sampleRate = 44100.0, int cycles = 5)
{
    const int N = static_cast<int>(sampleRate / freqHz * cycles);
    float maxOut = std::numeric_limits<float>::lowest();
    float minOut = std::numeric_limits<float>::max();

    for (int i = 0; i < N; ++i) {
        const float in  = amplitude *
            static_cast<float>(std::sin(2.0 * M_PI * freqHz * i / sampleRate));
        const float out = xfmr.processSample(in);
        REQUIRE(std::isfinite(out));
        maxOut = std::max(maxOut, out);
        minOut = std::min(minOut, out);
    }
    return maxOut - minOut;
}

// ── Stability: no NaN / Inf ───────────────────────────────────────────────────

TEST_CASE("TransformerLinear: no NaN/Inf with moderate input (drive=1)",
          "[transformer][stability]")
{
    Models::TransformerLinear xfmr;
    xfmr.prepare(44100.0);

    for (float v : {0.0f, 0.1f, -0.1f, 0.5f, -0.5f, 1.0f, -1.0f}) {
        for (int i = 0; i < 20; ++i) {
            const float out = xfmr.processSample(v);
            INFO("input=" << v << " iteration=" << i);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("TransformerLinear: no NaN/Inf with extreme input",
          "[transformer][stability]")
{
    Models::TransformerLinear xfmr;
    xfmr.prepare(44100.0);

    for (float v : {-10.0f, 10.0f, -100.0f, 100.0f}) {
        for (int i = 0; i < 20; ++i) {
            const float out = xfmr.processSample(v);
            INFO("input=" << v << " iteration=" << i);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("TransformerLinear: no NaN/Inf with high drive",
          "[transformer][stability]")
{
    Models::TransformerLinearConfig cfg;
    cfg.drive = 10.0f;
    Models::TransformerLinear xfmr(cfg);
    xfmr.prepare(44100.0);

    for (float v : {0.0f, 0.5f, -0.5f, 1.0f, -1.0f}) {
        for (int i = 0; i < 50; ++i) {
            const float out = xfmr.processSample(v);
            INFO("input=" << v << " iteration=" << i);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("TransformerLinear: silent input stays silent (drive=1)",
          "[transformer][stability]")
{
    Models::TransformerLinear xfmr;
    xfmr.prepare(44100.0);

    for (int i = 0; i < 1000; ++i) {
        const float out = xfmr.processSample(0.0f);
        REQUIRE(std::isfinite(out));
        REQUIRE_THAT(out, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
    }
}

// ── Frequency shaping ─────────────────────────────────────────────────────────

TEST_CASE("TransformerLinear: midband passes with near-unity gain (drive=1)",
          "[transformer][frequency]")
{
    // A 1 kHz sine is well above hpfCutoffHz (30 Hz) and well below
    // lpfCutoffHz (18 kHz); the combined attenuation should be negligible.
    //
    // Use a small amplitude so that the tanh saturator (drive=1) operates in
    // its near-linear regime: tanh(A×sin) / 1 ≈ A×sin for A << 1.
    const double sr = 44100.0;
    const float  A  = 0.01f;  // small signal — saturator is negligible

    Models::TransformerLinear xfmr;
    xfmr.prepare(sr);

    const float rms = measureRMS(xfmr, A, 1000.0, sr);
    // Expected RMS of a full-band sine at amplitude A: A / sqrt(2).
    const float expected = A / std::sqrt(2.0f);

    INFO("midband RMS=" << rms << "  expected≈" << expected);
    REQUIRE_THAT(rms, Catch::Matchers::WithinRel(expected, 0.05f)); // within 5 %
}

TEST_CASE("TransformerLinear: HPF attenuates sub-cutoff frequencies",
          "[transformer][frequency]")
{
    // A 5 Hz sine (well below the 30 Hz HPF cutoff) should be heavily
    // attenuated compared to a 1 kHz sine at the same amplitude.
    const double sr = 44100.0;
    const float  A  = 0.5f;

    Models::TransformerLinear xfmrMid, xfmrLow;
    xfmrMid.prepare(sr);
    xfmrLow.prepare(sr);

    const float rmsMid = measureRMS(xfmrMid, A, 1000.0, sr);
    const float rmsLow = measureRMS(xfmrLow, A, 5.0,    sr);

    INFO("RMS at 1 kHz (midband): " << rmsMid);
    INFO("RMS at 5 Hz  (sub-HPF): " << rmsLow);

    // Sub-cutoff signal must be substantially attenuated.
    REQUIRE(rmsLow < rmsMid * 0.5f);
}

TEST_CASE("TransformerLinear: LPF attenuates supra-cutoff frequencies",
          "[transformer][frequency]")
{
    // A 20 kHz sine (above the 18 kHz LPF cutoff) should be attenuated
    // compared to a 1 kHz sine at the same amplitude.
    const double sr = 96000.0; // Use high sample rate so 20 kHz is not near Nyquist
    const float  A  = 0.5f;

    Models::TransformerLinear xfmrMid, xfmrHigh;
    xfmrMid.prepare(sr);
    xfmrHigh.prepare(sr);

    const float rmsMid  = measureRMS(xfmrMid,  A, 1000.0,  sr);
    const float rmsHigh = measureRMS(xfmrHigh, A, 20000.0, sr);

    INFO("RMS at 1 kHz  (midband):  " << rmsMid);
    INFO("RMS at 20 kHz (supra-LPF): " << rmsHigh);

    // High-frequency signal must be attenuated relative to midband.
    REQUIRE(rmsHigh < rmsMid * 0.9f);
}

TEST_CASE("TransformerLinear: HPF cutoff frequency shift changes low-end response",
          "[transformer][frequency]")
{
    // A higher HPF cutoff should produce greater attenuation at a fixed
    // sub-cutoff frequency.
    const double sr  = 44100.0;
    const float  A   = 0.5f;
    const double testFreq = 100.0; // Hz — between the two cutoffs to test

    Models::TransformerLinearConfig cfgLow, cfgHigh;
    cfgLow.hpfCutoffHz  = 20.0;
    cfgHigh.hpfCutoffHz = 500.0; // Well above testFreq

    Models::TransformerLinear xfmrLow(cfgLow), xfmrHigh(cfgHigh);
    xfmrLow.prepare(sr);
    xfmrHigh.prepare(sr);

    const float rmsLowCutoff  = measureRMS(xfmrLow,  A, testFreq, sr);
    const float rmsHighCutoff = measureRMS(xfmrHigh, A, testFreq, sr);

    INFO("RMS with HPF=20 Hz:  " << rmsLowCutoff);
    INFO("RMS with HPF=500 Hz: " << rmsHighCutoff);

    // Higher HPF cutoff means more attenuation at 100 Hz.
    REQUIRE(rmsHighCutoff < rmsLowCutoff);
}

// ── Saturation ────────────────────────────────────────────────────────────────

TEST_CASE("TransformerLinear: drive=1 is fully linear (no gain compression)",
          "[transformer][saturation]")
{
    // At drive=1 doubling the input amplitude should double the output amplitude.
    const double sr = 44100.0;
    const float  A1 = 0.01f;
    const float  A2 = 0.02f;

    Models::TransformerLinear xfmr1, xfmr2;
    xfmr1.prepare(sr);
    xfmr2.prepare(sr);

    const float ptp1 = measurePeakToPeak(xfmr1, A1, 1000.0, sr, 5);
    const float ptp2 = measurePeakToPeak(xfmr2, A2, 1000.0, sr, 5);

    INFO("ptp at A=" << A1 << ": " << ptp1);
    INFO("ptp at A=" << A2 << ": " << ptp2);

    // Response should be linear: ptp2 ≈ 2·ptp1.
    REQUIRE_THAT(ptp2, Catch::Matchers::WithinRel(2.0f * ptp1, 0.02f));
}

TEST_CASE("TransformerLinear: high drive causes gain compression at large amplitude",
          "[transformer][saturation]")
{
    // With strong drive the gain at large amplitude must be lower than at
    // small amplitude (saturation / gain compression).
    const double sr    = 44100.0;
    const float  A_low  = 0.001f;
    const float  A_high = 0.9f;

    Models::TransformerLinearConfig cfg;
    cfg.drive = 8.0f;

    Models::TransformerLinear xfmrLow(cfg), xfmrHigh(cfg);
    xfmrLow.prepare(sr);
    xfmrHigh.prepare(sr);

    const float ptp_low  = measurePeakToPeak(xfmrLow,  A_low,  1000.0, sr, 5);
    const float ptp_high = measurePeakToPeak(xfmrHigh, A_high, 1000.0, sr, 5);

    const float gain_low  = ptp_low  / (2.0f * A_low);
    const float gain_high = ptp_high / (2.0f * A_high);

    INFO("gain at small amplitude (A=" << A_low  << "): " << gain_low);
    INFO("gain at large amplitude (A=" << A_high << "): " << gain_high);

    // High-drive gain at large amplitude must be measurably compressed.
    REQUIRE(gain_low > gain_high * 1.1f);
}

TEST_CASE("TransformerLinear: increasing drive increases saturation at fixed amplitude",
          "[transformer][saturation]")
{
    // At a fixed large amplitude a higher drive should produce a smaller
    // peak-to-peak output (more limiting).
    const double sr = 44100.0;
    const float  A  = 0.8f;

    Models::TransformerLinearConfig cfgClean, cfgDriven;
    cfgClean.drive  = 1.0f;
    cfgDriven.drive = 8.0f;

    Models::TransformerLinear xfmrClean(cfgClean), xfmrDriven(cfgDriven);
    xfmrClean.prepare(sr);
    xfmrDriven.prepare(sr);

    const float ptpClean  = measurePeakToPeak(xfmrClean,  A, 1000.0, sr, 5);
    const float ptpDriven = measurePeakToPeak(xfmrDriven, A, 1000.0, sr, 5);

    INFO("ptp (drive=1): " << ptpClean);
    INFO("ptp (drive=8): " << ptpDriven);

    // Heavier drive → more saturation → smaller swing.
    REQUIRE(ptpDriven < ptpClean);
}

TEST_CASE("TransformerLinear: saturation output is bounded",
          "[transformer][saturation]")
{
    // tanh saturator output must stay within (−1/drive, +1/drive) → safe bound.
    // In practice we just require it stays within ±1.
    Models::TransformerLinearConfig cfg;
    cfg.drive = 10.0f;
    Models::TransformerLinear xfmr(cfg);
    xfmr.prepare(44100.0);

    for (int cycle = 0; cycle < 10; ++cycle) {
        for (int i = 0; i < 441; ++i) {
            const float in  = 1.0f * static_cast<float>(
                std::sin(2.0 * M_PI * 100.0 * i / 44100.0));
            const float out = xfmr.processSample(in);
            REQUIRE(std::isfinite(out));
            REQUIRE(out >= -1.0f);
            REQUIRE(out <=  1.0f);
        }
    }
}

// ── Lifecycle: prepare / reset / setConfig ────────────────────────────────────

TEST_CASE("TransformerLinear: reset restores initial conditions",
          "[transformer][lifecycle]")
{
    Models::TransformerLinear xfmr;
    xfmr.prepare(44100.0);

    // Run some samples to build up filter state.
    std::vector<float> run1;
    for (int i = 0; i < 200; ++i)
        run1.push_back(xfmr.processSample(static_cast<float>(i % 2 == 0 ? 0.1f : -0.1f)));

    // Reset and repeat.
    xfmr.reset();
    for (int i = 0; i < 200; ++i) {
        const float out = xfmr.processSample(static_cast<float>(i % 2 == 0 ? 0.1f : -0.1f));
        REQUIRE_THAT(out, Catch::Matchers::WithinAbs(run1[i], 1e-6f));
    }
}

TEST_CASE("TransformerLinear: different sample rates produce finite output",
          "[transformer][lifecycle]")
{
    for (double sr : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        Models::TransformerLinear xfmr;
        xfmr.prepare(sr);
        INFO("sampleRate=" << sr);
        for (int i = 0; i < 100; ++i) {
            const float out = xfmr.processSample(0.1f);
            REQUIRE(std::isfinite(out));
        }
    }
}

TEST_CASE("TransformerLinear: setConfig + re-prepare changes coefficients",
          "[transformer][lifecycle]")
{
    // A tighter HPF cutoff (500 Hz vs 30 Hz) should produce more attenuation
    // at 200 Hz after re-preparation.
    const double sr   = 44100.0;
    const float  A    = 0.5f;
    const double freq = 200.0;

    Models::TransformerLinear xfmr;
    xfmr.prepare(sr);
    const float rmsDefault = measureRMS(xfmr, A, freq, sr);

    Models::TransformerLinearConfig newCfg;
    newCfg.hpfCutoffHz = 500.0;
    xfmr.setConfig(newCfg);
    xfmr.prepare(sr);
    const float rmsHighHPF = measureRMS(xfmr, A, freq, sr);

    INFO("RMS at 200 Hz (HPF=30 Hz):  " << rmsDefault);
    INFO("RMS at 200 Hz (HPF=500 Hz): " << rmsHighHPF);

    REQUIRE(rmsHighHPF < rmsDefault);
}

// ── Stereo: independent L/R instances ─────────────────────────────────────────

TEST_CASE("TransformerLinear: independent L/R instances do not share state",
          "[transformer][stereo]")
{
    // Drive the L channel hard; R channel should be unaffected.
    const double sr = 44100.0;

    Models::TransformerLinearConfig cfgL, cfgR;
    cfgL.drive = 1.0f; // clean
    cfgR.drive = 1.0f;

    Models::TransformerLinear xfmrL(cfgL), xfmrR(cfgR);
    xfmrL.prepare(sr);
    xfmrR.prepare(sr);

    // Feed the same sine into both channels alternately.
    for (int i = 0; i < 1000; ++i) {
        const float in = 0.5f * static_cast<float>(
            std::sin(2.0 * M_PI * 1000.0 * i / sr));
        const float outL = xfmrL.processSample(in);
        const float outR = xfmrR.processSample(in);
        REQUIRE(std::isfinite(outL));
        REQUIRE(std::isfinite(outR));
        // Both channels driven identically must produce identical output.
        REQUIRE_THAT(outL, Catch::Matchers::WithinAbs(outR, 1e-6f));
    }
}

TEST_CASE("TransformerLinear: L/R instances can have different configurations",
          "[transformer][stereo]")
{
    const double sr = 44100.0;

    // L: clean, R: saturated.
    Models::TransformerLinearConfig cfgL;
    cfgL.drive = 1.0f;

    Models::TransformerLinearConfig cfgR;
    cfgR.drive = 8.0f;

    Models::TransformerLinear xfmrL(cfgL), xfmrR(cfgR);
    xfmrL.prepare(sr);
    xfmrR.prepare(sr);

    const float ptpL = measurePeakToPeak(xfmrL, 0.8f, 1000.0, sr);
    const float ptpR = measurePeakToPeak(xfmrR, 0.8f, 1000.0, sr);

    INFO("ptp L (drive=1): " << ptpL);
    INFO("ptp R (drive=8): " << ptpR);

    // Saturated channel must have smaller swing.
    REQUIRE(ptpR < ptpL);
}
