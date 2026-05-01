#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/DbConversion.h"
#include "../src/DSP/UnitScaling.h"

// ─── dB conversion tests ──────────────────────────────────────────────────────

TEST_CASE("DbConversion: 0 dB maps to unity gain", "[dsp][db]") {
    REQUIRE_THAT(DbConversion::dBToLinear(0.0f),
                 Catch::Matchers::WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("DbConversion: -20 dB maps to 0.1 linear", "[dsp][db]") {
    REQUIRE_THAT(DbConversion::dBToLinear(-20.0f),
                 Catch::Matchers::WithinAbs(0.1f, 1e-6f));
}

TEST_CASE("DbConversion: +20 dB maps to 10.0 linear", "[dsp][db]") {
    REQUIRE_THAT(DbConversion::dBToLinear(20.0f),
                 Catch::Matchers::WithinAbs(10.0f, 1e-5f));
}

TEST_CASE("DbConversion: linearToDb(0.1) returns -20 dB", "[dsp][db]") {
    REQUIRE_THAT(DbConversion::linearToDb(0.1f),
                 Catch::Matchers::WithinAbs(-20.0f, 1e-4f));
}

TEST_CASE("DbConversion: linearToDb(10.0) returns +20 dB", "[dsp][db]") {
    REQUIRE_THAT(DbConversion::linearToDb(10.0f),
                 Catch::Matchers::WithinAbs(20.0f, 1e-4f));
}

TEST_CASE("DbConversion: linearToDb(0) returns -infinity", "[dsp][db]") {
    const float result = DbConversion::linearToDb(0.0f);
    REQUIRE(std::isinf(result));
    REQUIRE(result < 0.0f);
}

TEST_CASE("DbConversion: round-trip dBToLinear -> linearToDb", "[dsp][db]") {
    for (float dB : {-60.0f, -20.0f, -6.0f, 0.0f, 6.0f, 20.0f}) {
        REQUIRE_THAT(DbConversion::linearToDb(DbConversion::dBToLinear(dB)),
                     Catch::Matchers::WithinAbs(dB, 1e-4f));
    }
}

// ─── UnitScaling tests ────────────────────────────────────────────────────────

TEST_CASE("UnitScaling: sampleToVolts maps +1 sample to +10 V", "[dsp][unit]") {
    REQUIRE_THAT(UnitScaling::sampleToVolts(1.0f),
                 Catch::Matchers::WithinAbs(10.0f, 1e-6f));
}

TEST_CASE("UnitScaling: sampleToVolts maps -1 sample to -10 V", "[dsp][unit]") {
    REQUIRE_THAT(UnitScaling::sampleToVolts(-1.0f),
                 Catch::Matchers::WithinAbs(-10.0f, 1e-6f));
}

TEST_CASE("UnitScaling: sampleToVolts maps 0 sample to 0 V", "[dsp][unit]") {
    REQUIRE_THAT(UnitScaling::sampleToVolts(0.0f),
                 Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("UnitScaling: round-trip sample -> volts -> sample is identity", "[dsp][unit]") {
    for (float sample : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
        float roundTrip = UnitScaling::voltsToSample(UnitScaling::sampleToVolts(sample));
        REQUIRE_THAT(roundTrip, Catch::Matchers::WithinAbs(sample, 1e-6f));
    }
}
