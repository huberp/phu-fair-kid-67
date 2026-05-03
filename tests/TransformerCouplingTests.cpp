#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "../src/DSP/Circuit/Circuit.h"

#include <cmath>

static constexpr double kPi = 3.14159265358979323846;

// ─── Standalone inductor ──────────────────────────────────────────────────────

TEST_CASE("Inductor: RL step response, V_L decays to zero after 5 tau",
          "[inductor][rl]")
{
    // V1(1 V) → R(10 Ω) → node2 → L(10 mH) → GND
    // V_L(t) = V1 · e^(−t/τ)  with  τ = L/R = 1 ms
    // sampleRate = 100 kHz → τ = 100 samples
    const double sampleRate = 100000.0;
    const double R   = 10.0;
    const double L   = 10e-3;  // 10 mH
    const double tau = L / R;  // 1 ms

    Circuit::Circuit mna(2, 1);
    mna.stampResistor(R, 1, 2);
    mna.stampInductor(L, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);

    // Apply 1 V step for 5 τ
    const int steps = static_cast<int>(5.0 * tau * sampleRate);
    for (int i = 0; i < steps; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, 1.0);
        REQUIRE(mna.solve());
    }

    // After 5 time-constants V_L = e^(-5) < 0.007 — within 1 % of zero.
    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(0.0, 0.01));
}

TEST_CASE("Inductor: RL step response at t = tau", "[inductor][rl]")
{
    // After exactly 1 time-constant V_L = V1 · e^(−1) ≈ 0.3679 V.
    // High sample rate keeps the trapezoidal error small.
    const double sampleRate = 100000.0;
    const double R   = 10.0;
    const double L   = 10e-3;
    const double tau = L / R;

    Circuit::Circuit mna(2, 1);
    mna.stampResistor(R, 1, 2);
    mna.stampInductor(L, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);

    const int steps = static_cast<int>(tau * sampleRate); // 10 000 steps
    for (int i = 0; i < steps; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, 1.0);
        REQUIRE(mna.solve());
    }

    const double expected = std::exp(-1.0); // ≈ 0.3679
    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(expected, 0.005));
}

TEST_CASE("Inductor: DC stability, no drift or blow-up", "[inductor][stability]")
{
    // Alternating ±1 V input at 44.1 kHz for 44100 samples; output must stay
    // finite and bounded.
    const double sampleRate = 44100.0;
    const double R = 1000.0;
    const double L = 10e-3;

    Circuit::Circuit mna(2, 1);
    mna.stampResistor(R, 1, 2);
    mna.stampInductor(L, 2, 0);
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
    REQUIRE(std::abs(v) <= 1.1);
}

TEST_CASE("Inductor: reset clears inductor state", "[inductor][lifecycle]")
{
    // Charge up the inductor, then reset and verify it starts from zero.
    const double sampleRate = 1000.0;
    const double R = 10.0;
    const double L = 10e-3; // τ = 1 ms = 1 sample at 1 kHz

    Circuit::Circuit mna(2, 1);
    mna.stampResistor(R, 1, 2);
    mna.stampInductor(L, 2, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);

    // Build up inductor history
    for (int i = 0; i < 200; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, 1.0);
        mna.solve();
    }
    REQUIRE(mna.nodeVoltage(2) < 0.5); // V_L has decayed significantly

    // Reset and run one step with zero supply
    mna.reset();
    mna.beginStep();
    mna.setVoltageSourceValue(0, 0.0);
    REQUIRE(mna.solve());

    // After reset, inductor starts from zero: with 0 V input V_out = 0.
    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(0.0, 1e-9));
}

TEST_CASE("Inductor: DC response is sample-rate independent", "[inductor][dc]")
{
    // After 10 τ (steady-state DC), V_L must be near zero regardless of
    // sample rate.
    for (double sr : {44100.0, 96000.0}) {
        const double R   = 10.0;
        const double L   = 1e-3; // τ = 0.1 ms
        const double tau = L / R;
        const int    N   = static_cast<int>(10.0 * tau * sr);

        Circuit::Circuit mna(2, 1);
        mna.stampResistor(R, 1, 2);
        mna.stampInductor(L, 2, 0);
        mna.stampVoltageSource(0, 1, 0);
        mna.prepare(sr);

        for (int i = 0; i < N; i++) {
            mna.beginStep();
            mna.setVoltageSourceValue(0, 1.0);
            REQUIRE(mna.solve());
        }
        INFO("sampleRate=" << sr);
        REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(0.0, 0.01));
    }
}

// ─── Coupled inductors ────────────────────────────────────────────────────────

TEST_CASE("CoupledInductors: coupling transfers AC signal to secondary",
          "[coupled][transformer]")
{
    // Circuit:  V_src (node 1) → R_prim (1→2) → L1 (2→GND)
    //                          coupled with L2 (3→GND) → R_load (3→GND)
    //
    // With strong coupling (k=0.9) the secondary (node 3) should carry a
    // significant AC signal.  With no coupling (M=0) node 3 is isolated.
    const double sampleRate = 44100.0;
    const double R_prim = 100.0;
    const double R_load = 100.0;
    const double L      = 0.1;   // 100 mH: X_L ≈ 628 Ω at 1 kHz
    const double k      = 0.9;
    const double M      = k * L;

    auto measureSecondaryRMS = [&](double coupling_M) {
        Circuit::Circuit mna(3, 1);
        mna.stampResistor(R_prim, 1, 2);
        mna.stampResistor(R_load, 3, 0);
        mna.stampCoupledInductors(L, L, coupling_M, 2, 0, 3, 0);
        mna.stampVoltageSource(0, 1, 0);
        mna.prepare(sampleRate);

        // Warm up with silence to settle any DC transients.
        for (int i = 0; i < 2048; i++) {
            mna.beginStep();
            mna.setVoltageSourceValue(0, 0.0);
            mna.solve();
        }

        double sumSq = 0.0;
        const int N = 4096;
        for (int i = 0; i < N; i++) {
            const double in = std::sin(2.0 * kPi * 1000.0 * i / sampleRate);
            mna.beginStep();
            mna.setVoltageSourceValue(0, in);
            REQUIRE(mna.solve());
            const double v = mna.nodeVoltage(3);
            REQUIRE(std::isfinite(v));
            sumSq += v * v;
        }
        return std::sqrt(sumSq / N);
    };

    const double rms_coupled   = measureSecondaryRMS(M);
    const double rms_uncoupled = measureSecondaryRMS(0.0);

    INFO("RMS secondary (k=0.9): " << rms_coupled);
    INFO("RMS secondary (k=0.0): " << rms_uncoupled);

    // Coupled secondary must carry substantially more signal than uncoupled.
    REQUIRE(rms_coupled > rms_uncoupled * 5.0);
    REQUIRE(rms_coupled > 0.01); // must be a measurable signal level
}

TEST_CASE("CoupledInductors: stronger coupling increases secondary signal",
          "[coupled][transformer]")
{
    // Increasing the coupling coefficient k should increase the RMS voltage
    // at the secondary output.
    const double sampleRate = 44100.0;
    const double L      = 0.1;
    const double R_prim = 100.0;
    const double R_load = 100.0;

    auto measureRMS = [&](double k_coeff) {
        const double M = k_coeff * L;
        Circuit::Circuit mna(3, 1);
        mna.stampResistor(R_prim, 1, 2);
        mna.stampResistor(R_load, 3, 0);
        mna.stampCoupledInductors(L, L, M, 2, 0, 3, 0);
        mna.stampVoltageSource(0, 1, 0);
        mna.prepare(sampleRate);

        for (int i = 0; i < 2048; i++) {
            mna.beginStep();
            mna.setVoltageSourceValue(0, 0.0);
            mna.solve();
        }

        double sumSq = 0.0;
        const int N = 4096;
        for (int i = 0; i < N; i++) {
            const double in = std::sin(2.0 * kPi * 1000.0 * i / sampleRate);
            mna.beginStep();
            mna.setVoltageSourceValue(0, in);
            mna.solve();
            const double v = mna.nodeVoltage(3);
            sumSq += v * v;
        }
        return std::sqrt(sumSq / N);
    };

    const double rms_low  = measureRMS(0.3);
    const double rms_high = measureRMS(0.9);

    INFO("RMS secondary (k=0.3): " << rms_low);
    INFO("RMS secondary (k=0.9): " << rms_high);

    // Higher coupling → more energy transferred → larger secondary signal.
    REQUIRE(rms_high > rms_low);
}

TEST_CASE("CoupledInductors: turns-ratio 1:4 boosts secondary voltage",
          "[coupled][transformer]")
{
    // L2 = 4·L1  →  ideal turns ratio n = sqrt(L2/L1) = 2.
    // With high coupling (k=0.95) the secondary node voltage should exceed
    // the primary inductor node voltage (step-up behaviour).
    const double sampleRate = 44100.0;
    const double L1 = 0.05;  // 50 mH
    const double L2 = 0.20;  // 200 mH  (n=2 step-up)
    const double k  = 0.95;
    const double M  = k * std::sqrt(L1 * L2);

    Circuit::Circuit mna(3, 1);
    mna.stampResistor(50.0,  1, 2);   // R_primary
    mna.stampResistor(200.0, 3, 0);   // R_load  (≈ n²·R_prim for matched load)
    mna.stampCoupledInductors(L1, L2, M, 2, 0, 3, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);

    // Warm up
    for (int i = 0; i < 2048; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, 0.0);
        mna.solve();
    }

    // Measure RMS at primary inductor node (2) and secondary node (3).
    const int N = 4096;
    double sumSqPrim = 0.0, sumSqSec = 0.0;
    for (int i = 0; i < N; i++) {
        const double in = std::sin(2.0 * kPi * 500.0 * i / sampleRate);
        mna.beginStep();
        mna.setVoltageSourceValue(0, in);
        REQUIRE(mna.solve());
        REQUIRE(std::isfinite(mna.nodeVoltage(2)));
        REQUIRE(std::isfinite(mna.nodeVoltage(3)));
        sumSqPrim += mna.nodeVoltage(2) * mna.nodeVoltage(2);
        sumSqSec  += mna.nodeVoltage(3) * mna.nodeVoltage(3);
    }

    const double rmsPrim = std::sqrt(sumSqPrim / N);
    const double rmsSec  = std::sqrt(sumSqSec  / N);

    INFO("RMS at primary inductor node (L1):  " << rmsPrim);
    INFO("RMS at secondary node       (L2):   " << rmsSec);

    // Step-up transformer: secondary voltage > primary inductor voltage.
    REQUIRE(rmsSec > rmsPrim);
}

TEST_CASE("CoupledInductors: DC stability, no drift or blow-up",
          "[coupled][stability]")
{
    // Alternating ±1 V for 44100 samples; both primary and secondary nodes
    // must stay finite and bounded.
    const double sampleRate = 44100.0;
    const double L = 0.1;
    const double k = 0.8;
    const double M = k * L;

    Circuit::Circuit mna(3, 1);
    mna.stampResistor(100.0, 1, 2);  // R_primary
    mna.stampResistor(100.0, 3, 0);  // R_load
    mna.stampCoupledInductors(L, L, M, 2, 0, 3, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);
    mna.reset();

    for (int i = 0; i < 44100; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, (i % 2 == 0) ? 1.0 : -1.0);
        REQUIRE(mna.solve());
    }

    const double v2 = mna.nodeVoltage(2);
    const double v3 = mna.nodeVoltage(3);
    REQUIRE(std::isfinite(v2));
    REQUIRE(std::isfinite(v3));
    REQUIRE(std::abs(v2) <= 1.1);
    REQUIRE(std::abs(v3) <= 1.1);
}

TEST_CASE("CoupledInductors: reset clears coupled state",
          "[coupled][lifecycle]")
{
    const double sampleRate = 1000.0;
    const double L = 0.1;
    const double M = 0.8 * L;

    Circuit::Circuit mna(3, 1);
    mna.stampResistor(10.0, 1, 2);
    mna.stampResistor(10.0, 3, 0);
    mna.stampCoupledInductors(L, L, M, 2, 0, 3, 0);
    mna.stampVoltageSource(0, 1, 0);
    mna.prepare(sampleRate);

    // Build up inductor history
    for (int i = 0; i < 300; i++) {
        mna.beginStep();
        mna.setVoltageSourceValue(0, 1.0);
        mna.solve();
    }
    REQUIRE(mna.nodeVoltage(2) < 0.5); // inductor has charged

    // Reset and one zero-input step
    mna.reset();
    mna.beginStep();
    mna.setVoltageSourceValue(0, 0.0);
    REQUIRE(mna.solve());

    REQUIRE_THAT(mna.nodeVoltage(2), Catch::Matchers::WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(mna.nodeVoltage(3), Catch::Matchers::WithinAbs(0.0, 1e-9));
}
