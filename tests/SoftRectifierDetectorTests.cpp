#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "src/DSP/Models/Sidechain/SoftRectifierDetector.h"

#include <cmath>

#define _USE_MATH_DEFINES
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Models::Sidechain;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Build a SoftRectifierDetector with the default RectifierDetectorConfig.
static SoftRectifierDetector makeDetector(float forwardVoltageV = 0.8f)
{
    Analog::Models::Sidechain::RectifierDetectorConfig cfg;
    return SoftRectifierDetector(cfg, forwardVoltageV);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("SoftRectifierDetector: constructs without crashing", "[softRect]")
{
    REQUIRE_NOTHROW(SoftRectifierDetector{});
}

TEST_CASE("SoftRectifierDetector: controlVoltage() is zero before any sample", "[softRect]")
{
    auto det = makeDetector(0.8f);
    det.prepare(44100.0);
    REQUIRE_THAT(det.controlVoltage(), Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("SoftRectifierDetector: forwardVoltage=0 matches ideal RectifierDetector", "[softRect]")
{
    // With forwardVoltageV = 0 the soft detector should produce the same CV
    // as the underlying RectifierDetector.
    const double sampleRate  = 44100.0;
    const float  amp         = 0.5f;
    const int    N           = 4096;

    SoftRectifierDetector  soft(Analog::Models::Sidechain::RectifierDetectorConfig{}, 0.0f);
    Analog::Models::Sidechain::RectifierDetector inner{};

    soft.prepare(sampleRate);
    inner.prepare(sampleRate);

    float softCv  = 0.0f;
    float innerCv = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float in = amp * std::sin(2.0f * static_cast<float>(M_PI)
                                        * 1000.0f * i / static_cast<float>(sampleRate));
        softCv  = soft.processSample(in);
        innerCv = inner.processSample(in);
    }

    INFO("softCv=" << softCv << " innerCv=" << innerCv);
    REQUIRE_THAT(softCv, Catch::Matchers::WithinAbs(innerCv, 1e-5f));
}

TEST_CASE("SoftRectifierDetector: CV is zero for signals below forward voltage", "[softRect]")
{
    // A signal whose peak voltage is less than Vf should produce zero CV.
    // Vf = 0.8 V, kVoltsPerSample = 10.  So peak amplitude < 0.08 normalised
    // will be sub-threshold.
    const float Vf = 0.8f;
    const float subThresholdAmp = 0.07f; // peak volts = 0.7 V < 0.8 V

    auto det = makeDetector(Vf);
    det.prepare(44100.0);

    float lastCv = 0.0f;
    for (int i = 0; i < 8000; ++i) {
        const float in = subThresholdAmp * std::sin(2.0f * static_cast<float>(M_PI)
                                                    * 1000.0f * i / 44100.0f);
        lastCv = det.processSample(in);
    }

    INFO("CV after sub-threshold signal: " << lastCv);
    REQUIRE(lastCv < 0.01f); // effectively zero
}

TEST_CASE("SoftRectifierDetector: CV increases for signals above forward voltage", "[softRect]")
{
    // A signal whose peak exceeds Vf should drive the CV above zero.
    const float Vf    = 0.8f;
    const float amp   = 0.5f; // peak volts = 5 V >> Vf

    auto det = makeDetector(Vf);
    det.prepare(44100.0);

    float lastCv = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        const float in = amp * std::sin(2.0f * static_cast<float>(M_PI)
                                        * 1000.0f * i / 44100.0f);
        lastCv = det.processSample(in);
    }

    INFO("CV after supra-threshold signal: " << lastCv);
    REQUIRE(lastCv > 0.1f);
}

TEST_CASE("SoftRectifierDetector: CV is lower than ideal rectifier for the same signal", "[softRect]")
{
    // The forward-voltage dead zone means the soft detector builds up less CV
    // than an ideal rectifier (forwardVoltage = 0) for the same input.
    const double sampleRate = 44100.0;
    const float  amp        = 0.3f; // moderate signal
    const int    N          = 20000;

    SoftRectifierDetector idealDet(Analog::Models::Sidechain::RectifierDetectorConfig{}, 0.0f);
    SoftRectifierDetector softDet(Analog::Models::Sidechain::RectifierDetectorConfig{}, 0.8f);

    idealDet.prepare(sampleRate);
    softDet.prepare(sampleRate);

    float idealCv = 0.0f, softCv = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float in = amp * std::sin(2.0f * static_cast<float>(M_PI)
                                        * 1000.0f * i / static_cast<float>(sampleRate));
        idealCv = idealDet.processSample(in);
        softCv  = softDet.processSample(in);
    }

    INFO("idealCv=" << idealCv << " softCv=" << softCv);
    REQUIRE(softCv < idealCv);
}

TEST_CASE("SoftRectifierDetector: output is finite", "[softRect]")
{
    auto det = makeDetector(0.8f);
    det.prepare(44100.0);

    for (float amp : { 0.0f, 0.01f, 0.1f, 0.5f, 1.0f }) {
        for (int i = 0; i < 1000; ++i) {
            const float in = amp * std::sin(static_cast<float>(i) * 0.1f);
            const float cv = det.processSample(in);
            INFO("amp=" << amp << " i=" << i << " cv=" << cv);
            REQUIRE(std::isfinite(cv));
        }
    }
}

TEST_CASE("SoftRectifierDetector: reset clears state", "[softRect]")
{
    auto det = makeDetector(0.8f);
    det.prepare(44100.0);

    // Run signal to charge the CV.
    for (int i = 0; i < 5000; ++i)
        (void)det.processSample(0.5f);

    REQUIRE(det.controlVoltage() > 0.0f);

    det.reset();
    REQUIRE_THAT(det.controlVoltage(), Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("SoftRectifierDetector: setConfig replaces configuration", "[softRect]")
{
    auto det = makeDetector(0.8f);
    det.prepare(44100.0);

    Analog::Models::Sidechain::RectifierDetectorConfig newCfg;
    REQUIRE_NOTHROW(det.setConfig(newCfg));
    REQUIRE(std::isfinite(det.processSample(0.1f)));
}
