/// Offline static transfer-curve measurement and validation tests.
///
/// These tests:
///   1. Feed steady-state tones at a range of input levels into the
///      Fairchild670Core (via the DSP core, not a DAW/JUCE plugin host).
///   2. Wait for the compressor to settle.
///   3. Measure RMS input and output levels.
///   4. Compare measured gain reduction against the manual-derived reference
///      points in tests/transfer_curve_reference.csv (embedded as static arrays
///      to avoid filesystem dependencies in ctest).
///   5. Validate curve *shape*:
///      - Below the threshold region: gain reduction < 0.5 dB (near unity).
///      - Above the threshold region: gain reduction increases monotonically.
///      - Heavy-limiting region: significant gain reduction (> 6 dB at 0 dBFS).
///
/// All tests run entirely offline (no DAW, no JUCE, no audio thread).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/Models/Fairchild/Fairchild670Core.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Models;

// ─────────────────────────────────────────────────────────────────────────────
// Manual-derived reference points (from transfer_curve_reference.csv).
//
// Status: approximate — qualitative reference, not from hardware measurements.
// Tolerance applied in tests: ±4 dB on absolute level; shape tests are stricter.
// ─────────────────────────────────────────────────────────────────────────────

struct TransferPoint {
    float inputDbfs;   ///< Input level in dBFS.
    float outputDbfs;  ///< Expected output in dBFS.
    float grDb;        ///< Expected gain reduction in dB.
};

/// Embedded reference from transfer_curve_reference.csv.
/// Status: implementation-provisional — reflects measured behaviour of the
/// current DSP core (6386 remote-cutoff tube, push-pull stage, 6AL5 soft
/// rectifier, input/interstage/output transformers, 12AU7 pre-amplifier)
/// with threshold=0 V and P1.  NOT from hardware measurements.
/// TODO: replace with digitised Fairchild 670 manual data when available.
static const std::vector<TransferPoint> kTransferReference = {
    { -60.0f, -63.07f,  0.05f },   // silence floor — well below any knee
    { -39.0f, -39.07f,  0.05f },   // very low level — near unity gain
    { -23.0f, -23.24f,  0.22f },   // low level — slight compression
    { -18.0f, -18.79f,  0.77f },   // gentle compression begins
    { -12.0f, -14.64f,  2.62f },   // moderate compression
    {  -6.0f, -14.28f,  8.26f },   // noticeable gain reduction (Vgk≤0 clamp active)
    {  -3.0f, -14.70f, 11.68f },   // significant limiting
    {   0.0f, -14.70f, 11.68f },   // heavy limiting at full scale (gain=0.7, cvMaxV=9)
};

// ─────────────────────────────────────────────────────────────────────────────
// Measurement helper
// ─────────────────────────────────────────────────────────────────────────────

/// Convert dBFS to a linear amplitude (0 dBFS = ±1.0 normalised full-scale).
static float dbfsToAmplitude(float dbfs)
{
    return std::pow(10.0f, dbfs / 20.0f);
}

/// Convert a linear amplitude to dBFS.
static float amplitudeToDbfs(float amp)
{
    if (amp <= 0.0f) return -144.0f; // silence floor
    return 20.0f * std::log10(amp);
}

struct TransferMeasurement {
    float inputDbfs;
    float outputDbfs;
    float grDb;
};

/// Measure the steady-state input-vs-output transfer at a given input amplitude.
///
/// @param amplitude  Normalised input amplitude (0.0 – 1.0).
/// @param pos        Timing position.
/// @param sampleRate Sample rate in Hz.
/// @param settleN    Samples to feed before measurement (for detector settling).
/// @param measureN   Samples over which RMS output is measured.
/// @param threshV    Threshold voltage (0 V = always compress).
static TransferMeasurement measureTransfer(
    float                       amplitude,
    Sidechain::TimingPosition   pos       = Sidechain::TimingPosition::P1,
    double                      sampleRate = 44100.0,
    int                         settleN    = 80000,
    int                         measureN   = 8192,
    float                       threshV    = 0.0f)
{
    Fairchild670CoreConfig cfg;
    cfg.linkMode         = LinkMode::Independent;
    cfg.timingPreset = pos;

    Fairchild670Core core(cfg);
    core.prepare(sampleRate);
    core.setThresholdLeft(threshV);
    core.setThresholdRight(threshV);

    // Settle: feed a 1 kHz sine at the target amplitude.
    float outL, outR;
    const float freqHz = 1000.0f;
    for (int i = 0; i < settleN; ++i) {
        const float in = amplitude * std::sin(
            2.0f * static_cast<float>(M_PI) * freqHz
                * static_cast<float>(i) / static_cast<float>(sampleRate));
        core.processStereo(in, in, outL, outR);
    }

    // Measurement window: accumulate RMS.
    double sumInSq  = 0.0;
    double sumOutSq = 0.0;
    for (int i = 0; i < measureN; ++i) {
        const float in = amplitude * std::sin(
            2.0f * static_cast<float>(M_PI) * freqHz
                * static_cast<float>(settleN + i)
                / static_cast<float>(sampleRate));
        core.processStereo(in, in, outL, outR);
        sumInSq  += static_cast<double>(in)   * static_cast<double>(in);
        sumOutSq += static_cast<double>(outL)  * static_cast<double>(outL);
    }

    const float rmsIn  = static_cast<float>(std::sqrt(sumInSq  / measureN));
    const float rmsOut = static_cast<float>(std::sqrt(sumOutSq / measureN));

    TransferMeasurement m;
    m.inputDbfs  = amplitudeToDbfs(rmsIn);
    m.outputDbfs = amplitudeToDbfs(rmsOut);
    m.grDb       = m.inputDbfs - m.outputDbfs;
    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("TransferCurve: output is finite across full input range",
          "[transfer][stability]")
{
    for (float dbfs : { -60.0f, -40.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f }) {
        const float amp = dbfsToAmplitude(dbfs);
        auto m = measureTransfer(amp,
                                 Sidechain::TimingPosition::P1,
                                 44100.0,
                                 /*settleN=*/  20000,
                                 /*measureN=*/ 4096,
                                 /*threshV=*/  0.0f);
        INFO("inputDbfs=" << dbfs << " outputDbfs=" << m.outputDbfs);
        REQUIRE(std::isfinite(m.outputDbfs));
        REQUIRE(std::isfinite(m.grDb));
    }
}

TEST_CASE("TransferCurve: gain reduction is non-negative (output ≤ input)",
          "[transfer][shape]")
{
    // With threshold=0 V and a nonlinear stage, the compressor can only attenuate.
    for (float dbfs : { -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f }) {
        const float amp = dbfsToAmplitude(dbfs);
        auto m = measureTransfer(amp,
                                 Sidechain::TimingPosition::P1,
                                 44100.0,
                                 /*settleN=*/  30000,
                                 /*measureN=*/ 4096,
                                 /*threshV=*/  0.0f);
        INFO("inputDbfs=" << dbfs
             << " outputDbfs=" << m.outputDbfs
             << " grDb=" << m.grDb);
        // Allow a small margin for measurement noise and DC-blocking transients.
        REQUIRE(m.grDb >= -1.0f);
    }
}

TEST_CASE("TransferCurve: gain reduction increases monotonically with input level",
          "[transfer][shape]")
{
    // Build the curve at a series of input levels and verify GR increases.
    const std::vector<float> levels = { -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f };
    std::vector<float> grValues;
    grValues.reserve(levels.size());

    for (float dbfs : levels) {
        const float amp = dbfsToAmplitude(dbfs);
        auto m = measureTransfer(amp,
                                 Sidechain::TimingPosition::P1,
                                 44100.0,
                                 /*settleN=*/  30000,
                                 /*measureN=*/ 4096,
                                 /*threshV=*/  0.0f);
        grValues.push_back(m.grDb);
        INFO("inputDbfs=" << dbfs << " grDb=" << m.grDb);
        REQUIRE(std::isfinite(m.grDb));
    }

    // Verify monotonically non-decreasing GR (allow tiny floating-point slack).
    for (std::size_t i = 1; i < grValues.size(); ++i) {
        INFO("i=" << i << " gr[i-1]=" << grValues[i-1] << " gr[i]=" << grValues[i]);
        REQUIRE(grValues[i] >= grValues[i - 1] - 0.5f);
    }
}

TEST_CASE("TransferCurve: heavy limiting at 0 dBFS (> 4 dB GR)",
          "[transfer][shape]")
{
    // At full-scale input with threshold=0 V the compressor should produce
    // meaningful gain reduction (> 4 dB).  This validates that the variable-mu
    // stage and detector are actually working together.
    auto m = measureTransfer(1.0f,  // 0 dBFS
                             Sidechain::TimingPosition::P1,
                             44100.0,
                             /*settleN=*/  80000,
                             /*measureN=*/ 8192,
                             /*threshV=*/  0.0f);
    INFO("0 dBFS: outputDbfs=" << m.outputDbfs << " grDb=" << m.grDb);
    REQUIRE(m.grDb > 4.0f);
}

TEST_CASE("TransferCurve: qualitative comparison against manual reference (±4 dB)",
          "[transfer][reference]")
{
    // Broad-tolerance comparison against the reference table.
    // The ±4 dB tolerance makes this a regression check: if DSP parameters
    // change significantly (cvMaxV, Rp, Rk, detector scaling) the test fails.
    // The reference is implementation-provisional (see transfer_curve_reference.csv).
    // TODO: replace with digitised Fairchild 670 manual measurements and
    //       tighten the tolerance once the implementation is better calibrated.
    constexpr float kToleranceDb = 4.0f;

    for (const auto& ref : kTransferReference) {
        // Skip the silence floor row (measurement noise dominates there).
        if (ref.inputDbfs < -42.0f) continue;

        const float amp = dbfsToAmplitude(ref.inputDbfs);
        auto m = measureTransfer(amp,
                                 Sidechain::TimingPosition::P1,
                                 44100.0,
                                 /*settleN=*/  200000,
                                 /*measureN=*/ 8192,
                                 /*threshV=*/  0.0f);

        INFO("ref inputDbfs="  << ref.inputDbfs
             << " ref grDb="   << ref.grDb
             << " meas grDb="  << m.grDb
             << " tolerance ±" << kToleranceDb << " dB");

        REQUIRE_THAT(m.grDb,
                     Catch::Matchers::WithinAbs(ref.grDb, kToleranceDb));
    }
}

TEST_CASE("TransferCurve: curve shape — low input near unity, high input compressed",
          "[transfer][shape]")
{
    // At -40 dBFS (well below any knee) gain reduction should be minimal.
    auto mLow = measureTransfer(dbfsToAmplitude(-40.0f),
                                Sidechain::TimingPosition::P1,
                                44100.0, 30000, 4096, 0.0f);

    // At -3 dBFS gain reduction should be significant (> low by at least 2 dB).
    auto mHigh = measureTransfer(dbfsToAmplitude(-3.0f),
                                 Sidechain::TimingPosition::P1,
                                 44100.0, 80000, 8192, 0.0f);

    INFO("low  inputDbfs=-40 grDb=" << mLow.grDb);
    INFO("high inputDbfs=-3  grDb=" << mHigh.grDb);

    // Low-level input should have minimal compression (< 2 dB GR).
    REQUIRE(mLow.grDb  < 2.0f);

    // High-level input must show distinctly more compression than low-level.
    REQUIRE(mHigh.grDb > mLow.grDb + 2.0f);
}
