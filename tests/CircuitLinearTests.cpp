#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/Circuit/Circuit.h"

#include <cmath>

// ── Resistor divider DC ───────────────────────────────────────────────────────

TEST_CASE("Circuit: resistor divider, equal R, midpoint = half supply", "[circuit][resistor]") {
    // V1(10 V) ─ R1(1 kΩ) ─ node2 ─ R2(1 kΩ) ─ GND
    // Expected: V_node1 = 10 V, V_node2 = 5 V
    Circuit::Circuit mna(2, 1);
    mna.stampResistor(1000.0, 1, 2);  // R1: node1 → node2
    mna.stampResistor(1000.0, 2, 0);  // R2: node2 → ground
    mna.stampVoltageSource(0, 1, 0);  // V1: drives node1 from ground
    mna.prepare(44100.0);

    mna.beginStep();
    mna.setVoltageSourceValue(0, 10.0);
    REQUIRE(mna.solve());

    REQUIRE_THAT(mna.nodeVoltage(1), Catch::Matchers::WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(5.0,  1e-9));
    REQUIRE_THAT(mna.nodeVoltage(0), Catch::Matchers::WithinAbs(0.0,  1e-9));
}

TEST_CASE("Circuit: resistor divider, 2:1 ratio", "[circuit][resistor]") {
    // V1(6 V) ─ R1(2 kΩ) ─ node2 ─ R2(1 kΩ) ─ GND
    // V_node2 = 6 * 1000/(2000+1000) = 2 V
    Circuit::Circuit mna(2, 1);
    mna.stampResistor(2000.0, 1, 2);
    mna.stampResistor(1000.0, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(44100.0);

    mna.beginStep();
    mna.setVoltageSourceValue(0, 6.0);
    REQUIRE(mna.solve());

    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(2.0, 1e-9));
}

TEST_CASE("Circuit: resistor divider, vsrc current equals V/R_total", "[circuit][resistor]") {
    // V1(10 V) ─ R1(1 kΩ) ─ node2 ─ R2(1 kΩ) ─ GND
    // I = 10 / 2000 = 5 mA
    Circuit::Circuit mna(2, 1);
    mna.stampResistor(1000.0, 1, 2);
    mna.stampResistor(1000.0, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(44100.0);

    mna.beginStep();
    mna.setVoltageSourceValue(0, 10.0);
    REQUIRE(mna.solve());

    REQUIRE_THAT(mna.vsrcCurrent(0), Catch::Matchers::WithinAbs(-5e-3, 1e-12));
}

TEST_CASE("Circuit: resistor divider, DC result is sample-rate independent", "[circuit][resistor]") {
    // The DC result must not depend on the sample rate.
    for (double sr : {8000.0, 44100.0, 96000.0, 192000.0}) {
        Circuit::Circuit mna(2, 1);
        mna.stampResistor(1000.0, 1, 2);
        mna.stampResistor(1000.0, 2, 0);
        mna.stampVoltageSource(0, 1, 0);
        mna.prepare(sr);

        mna.beginStep();
        mna.setVoltageSourceValue(0, 10.0);
        REQUIRE(mna.solve());
        REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(5.0, 1e-9));
    }
}

// ── RC low-pass step response ─────────────────────────────────────────────────

TEST_CASE("Circuit: RC low-pass step response approaches supply after 5 tau", "[circuit][rc]") {
    // V1(1 V) ─ R(1 kΩ) ─ node2 ─ C(100 µF) ─ GND
    // τ = RC = 0.1 s; sampleRate = 1 kHz → τ = 100 samples
    const double sampleRate = 1000.0;
    const double R = 1000.0;
    const double C = 100e-6;
    const double tau = R * C; // 0.1 s

    Circuit::Circuit mna(2, 1);
    mna.stampResistor(R, 1, 2);
    mna.stampCapacitor(C, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);

    // Apply 1 V step for 5 τ
    const int steps = static_cast<int>(5.0 * tau * sampleRate);
    for (int i = 0; i < steps; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, 1.0);
        REQUIRE(mna.solve());
    }

    // After 5 time-constants V_out must be within 1 % of the supply.
    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(1.0, 0.01));
}

TEST_CASE("Circuit: RC low-pass step response at t = tau", "[circuit][rc]") {
    // After exactly 1 time-constant V_out ≈ (1 − 1/e) ≈ 0.6321 V.
    // High sample rate to keep trapezoidal error small.
    const double sampleRate = 100000.0;
    const double R = 1000.0;
    const double C = 100e-6;
    const double tau = R * C; // 0.1 s

    Circuit::Circuit mna(2, 1);
    mna.stampResistor(R, 1, 2);
    mna.stampCapacitor(C, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);

    const int steps = static_cast<int>(tau * sampleRate); // 10 000 steps
    for (int i = 0; i < steps; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, 1.0);
        REQUIRE(mna.solve());
    }

    const double expected = 1.0 - std::exp(-1.0); // ≈ 0.6321
    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(expected, 0.001));
}

TEST_CASE("Circuit: RC long-run stability, no drift or blow-up", "[circuit][rc][stability]") {
    // Alternating ±1 V input at 44.1 kHz for 44100 samples (1 second).
    // With τ = RC = 10 ms the high-frequency content is heavily attenuated;
    // the output must stay finite and bounded.
    const double sampleRate = 44100.0;
    const double R = 10000.0;
    const double C = 1e-6; // τ ≈ 10 ms

    Circuit::Circuit mna(2, 1);
    mna.stampResistor(R, 1, 2);
    mna.stampCapacitor(C, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);
    mna.reset();

    for (int i = 0; i < 44100; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, (i % 2 == 0) ? 1.0 : -1.0);
        REQUIRE(mna.solve());
    }

    const double v = mna.nodeVoltage(2);
    REQUIRE(std::isfinite(v));
    REQUIRE(std::abs(v) <= 1.1); // must remain bounded
}

TEST_CASE("Circuit: RC reset clears capacitor state", "[circuit][rc]") {
    // Charge up the capacitor, then reset and verify it starts from 0 again.
    const double sampleRate = 1000.0;
    const double R = 1000.0;
    const double C = 100e-6;

    Circuit::Circuit mna(2, 1);
    mna.stampResistor(R, 1, 2);
    mna.stampCapacitor(C, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);

    // Charge up
    for (int i = 0; i < 500; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, 1.0);
        mna.solve();
    }
    REQUIRE(mna.nodeVoltage(2) > 0.5); // partially charged

    // Reset and run one step from zero supply
    mna.reset();
    mna.beginStep();
    mna.setVoltageSourceValue(0, 0.0);
    REQUIRE(mna.solve());

    // After reset, cap starts from zero: with 0 V input V_out should be 0.
    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(0.0, 1e-9));
}
