#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analog/nonlinear/TriodeKoren.h"

#include <cmath>
#include <limits>

using namespace Analog::Nonlinear;

// ── TubeParams presets sanity ─────────────────────────────────────────────────

TEST_CASE("TriodeKoren: TubeParams presets have positive parameters", "[triode][params]") {
    for (auto&& [name, p] : {
             std::pair{"12AX7", TubeParams::tubeParams12AX7()},
             std::pair{"12AU7", TubeParams::tubeParams12AU7()},
             std::pair{"6072",  TubeParams::tubeParams6072()},
         }) {
        INFO("Tube: " << name);
        REQUIRE(p.mu  > 0.0);
        REQUIRE(p.kp  > 0.0);
        REQUIRE(p.kvb >= 0.0);
        REQUIRE(p.kg1 > 0.0);
        REQUIRE(p.x   > 0.0);
    }
}

// ── Cutoff sanity ─────────────────────────────────────────────────────────────

TEST_CASE("TriodeKoren: Ip = 0 when Vpk = 0", "[triode][cutoff]") {
    // A tube with zero plate voltage conducts no current regardless of Vgk.
    for (auto&& p : {TubeParams::tubeParams12AX7(),
                     TubeParams::tubeParams12AU7(),
                     TubeParams::tubeParams6072()}) {
        for (double Vgk : {0.0, -0.5, -1.0, -2.0}) {
            REQUIRE_THAT(triodeIp(0.0, Vgk, p), Catch::Matchers::WithinAbs(0.0, 1e-15));
        }
    }
}

TEST_CASE("TriodeKoren: Ip = 0 when grid is sufficiently negative (cut-off)", "[triode][cutoff]") {
    // With a very negative grid voltage the tube must be fully cut off.
    for (auto&& p : {TubeParams::tubeParams12AX7(),
                     TubeParams::tubeParams12AU7(),
                     TubeParams::tubeParams6072()}) {
        REQUIRE_THAT(triodeIp(200.0, -100.0, p), Catch::Matchers::WithinAbs(0.0, 1e-15));
    }
}

TEST_CASE("TriodeKoren: Ip > 0 for typical operating point", "[triode][conduction]") {
    // At a normal quiescent bias the tube should produce a positive plate current.
    // 12AX7: Vpk ~ 100 V, Vgk ~ −1 V → should be conducting.
    const double Ip = triodeIp(100.0, -1.0, TubeParams::tubeParams12AX7());
    REQUIRE(Ip > 0.0);
    REQUIRE(std::isfinite(Ip));
}

// ── Monotonicity ──────────────────────────────────────────────────────────────

TEST_CASE("TriodeKoren: Ip increases monotonically with Vpk (12AX7)", "[triode][monotonicity]") {
    // For fixed Vgk, the plate current must be non-decreasing as Vpk increases.
    const auto p   = TubeParams::tubeParams12AX7();
    const double Vgk = -1.0;

    // Sweep from 0 V to 300 V in 5 V steps — typical small-signal triode range.
    static constexpr int    kMaxVpkTest  = 300;
    static constexpr int    kVpkTestStep = 5;

    double prevIp = 0.0;
    for (int i = 0; i <= kMaxVpkTest; i += kVpkTestStep) {
        const double Ip = triodeIp(static_cast<double>(i), Vgk, p);
        REQUIRE(std::isfinite(Ip));
        REQUIRE(Ip >= 0.0);
        REQUIRE(Ip >= prevIp - 1e-15); // allow tiny floating-point rounding
        prevIp = Ip;
    }
}

TEST_CASE("TriodeKoren: Ip increases monotonically with Vpk (12AU7)", "[triode][monotonicity]") {
    const auto p   = TubeParams::tubeParams12AU7();
    const double Vgk = -1.0;

    static constexpr int    kMaxVpkTest  = 300;
    static constexpr int    kVpkTestStep = 5;

    double prevIp = 0.0;
    for (int i = 0; i <= kMaxVpkTest; i += kVpkTestStep) {
        const double Ip = triodeIp(static_cast<double>(i), Vgk, p);
        REQUIRE(std::isfinite(Ip));
        REQUIRE(Ip >= 0.0);
        REQUIRE(Ip >= prevIp - 1e-15);
        prevIp = Ip;
    }
}

TEST_CASE("TriodeKoren: Ip decreases (or stays zero) as Vgk decreases", "[triode][monotonicity]") {
    // More negative grid → less current.
    const auto p     = TubeParams::tubeParams12AX7();
    const double Vpk = 150.0;

    double prevIp = triodeIp(Vpk, 0.0, p);
    for (double Vgk = -0.25; Vgk >= -5.0; Vgk -= 0.25) {
        const double Ip = triodeIp(Vpk, Vgk, p);
        REQUIRE(std::isfinite(Ip));
        REQUIRE(Ip >= 0.0);
        REQUIRE(Ip <= prevIp + 1e-15);
        prevIp = Ip;
    }
}

// ── Finite derivatives ────────────────────────────────────────────────────────

TEST_CASE("TriodeKoren: derivatives are finite over operating region", "[triode][derivatives]") {
    for (auto&& [name, p] : {
             std::pair{"12AX7", TubeParams::tubeParams12AX7()},
             std::pair{"12AU7", TubeParams::tubeParams12AU7()},
             std::pair{"6072",  TubeParams::tubeParams6072()},
         }) {
        INFO("Tube: " << name);
        for (double Vpk : {0.0, 50.0, 100.0, 150.0, 200.0, 250.0, 300.0}) {
            for (double Vgk : {0.0, -0.5, -1.0, -2.0, -3.0}) {
                INFO("Vpk=" << Vpk << "  Vgk=" << Vgk);
                const double dIp_dVpk = triodeDIpDVpk(Vpk, Vgk, p);
                const double dIp_dVgk = triodeDIpDVgk(Vpk, Vgk, p);
                REQUIRE(std::isfinite(dIp_dVpk));
                REQUIRE(std::isfinite(dIp_dVgk));
            }
        }
    }
}

TEST_CASE("TriodeKoren: dIp/dVpk >= 0 (plate conductance is non-negative)", "[triode][derivatives]") {
    // For a physical triode ∂Ip/∂Vpk ≥ 0 in the normal operating region.
    const auto p = TubeParams::tubeParams12AX7();
    for (double Vpk : {0.0, 10.0, 50.0, 100.0, 150.0, 250.0}) {
        for (double Vgk : {0.0, -0.5, -1.0, -2.0}) {
            INFO("Vpk=" << Vpk << "  Vgk=" << Vgk);
            REQUIRE(triodeDIpDVpk(Vpk, Vgk, p) >= -1e-15);
        }
    }
}

TEST_CASE("TriodeKoren: dIp/dVgk >= 0 (transconductance is non-negative)", "[triode][derivatives]") {
    // For a physical triode ∂Ip/∂Vgk ≥ 0 (more positive grid → more current).
    const auto p = TubeParams::tubeParams12AX7();
    for (double Vpk : {50.0, 100.0, 150.0, 200.0}) {
        for (double Vgk : {-3.0, -2.0, -1.0, -0.5, 0.0}) {
            INFO("Vpk=" << Vpk << "  Vgk=" << Vgk);
            REQUIRE(triodeDIpDVgk(Vpk, Vgk, p) >= -1e-15);
        }
    }
}

// ── Derivative accuracy (numerical check) ────────────────────────────────────

TEST_CASE("TriodeKoren: analytical dIp/dVpk matches numerical (12AX7)", "[triode][derivatives]") {
    const auto  p    = TubeParams::tubeParams12AX7();
    const double Vpk = 120.0;
    const double Vgk = -1.0;
    const double h   = 1e-5;

    const double numerical = (triodeIp(Vpk + h, Vgk, p) - triodeIp(Vpk - h, Vgk, p)) / (2.0 * h);
    const double analytical = triodeDIpDVpk(Vpk, Vgk, p);

    REQUIRE_THAT(analytical, Catch::Matchers::WithinAbs(numerical, 1e-8));
}

TEST_CASE("TriodeKoren: analytical dIp/dVgk matches numerical (12AX7)", "[triode][derivatives]") {
    const auto  p    = TubeParams::tubeParams12AX7();
    const double Vpk = 120.0;
    const double Vgk = -1.0;
    const double h   = 1e-5;

    const double numerical  = (triodeIp(Vpk, Vgk + h, p) - triodeIp(Vpk, Vgk - h, p)) / (2.0 * h);
    const double analytical = triodeDIpDVgk(Vpk, Vgk, p);

    REQUIRE_THAT(analytical, Catch::Matchers::WithinAbs(numerical, 1e-8));
}

TEST_CASE("TriodeKoren: analytical dIp/dVpk matches numerical (12AU7)", "[triode][derivatives]") {
    const auto  p    = TubeParams::tubeParams12AU7();
    const double Vpk = 80.0;
    const double Vgk = -1.0;
    const double h   = 1e-5;

    const double numerical  = (triodeIp(Vpk + h, Vgk, p) - triodeIp(Vpk - h, Vgk, p)) / (2.0 * h);
    const double analytical = triodeDIpDVpk(Vpk, Vgk, p);

    REQUIRE_THAT(analytical, Catch::Matchers::WithinAbs(numerical, 1e-8));
}

TEST_CASE("TriodeKoren: analytical dIp/dVgk matches numerical (12AU7)", "[triode][derivatives]") {
    const auto  p    = TubeParams::tubeParams12AU7();
    const double Vpk = 80.0;
    const double Vgk = -1.0;
    const double h   = 1e-5;

    const double numerical  = (triodeIp(Vpk, Vgk + h, p) - triodeIp(Vpk, Vgk - h, p)) / (2.0 * h);
    const double analytical = triodeDIpDVgk(Vpk, Vgk, p);

    REQUIRE_THAT(analytical, Catch::Matchers::WithinAbs(numerical, 1e-8));
}

// ── Numerical stability ───────────────────────────────────────────────────────

TEST_CASE("TriodeKoren: no NaN or Inf at boundary conditions", "[triode][stability]") {
    for (auto&& [name, p] : {
             std::pair{"12AX7", TubeParams::tubeParams12AX7()},
             std::pair{"12AU7", TubeParams::tubeParams12AU7()},
             std::pair{"6072",  TubeParams::tubeParams6072()},
         }) {
        INFO("Tube: " << name);
        // Vpk = 0 (plate shorted to cathode)
        REQUIRE(std::isfinite(triodeIp(0.0, 0.0, p)));
        REQUIRE(std::isfinite(triodeDIpDVpk(0.0, 0.0, p)));
        REQUIRE(std::isfinite(triodeDIpDVgk(0.0, 0.0, p)));

        // Deep cut-off
        REQUIRE(std::isfinite(triodeIp(200.0, -100.0, p)));
        REQUIRE(std::isfinite(triodeDIpDVpk(200.0, -100.0, p)));
        REQUIRE(std::isfinite(triodeDIpDVgk(200.0, -100.0, p)));

        // Full conduction (high Vpk, Vgk = 0)
        REQUIRE(std::isfinite(triodeIp(400.0, 0.0, p)));
        REQUIRE(std::isfinite(triodeDIpDVpk(400.0, 0.0, p)));
        REQUIRE(std::isfinite(triodeDIpDVgk(400.0, 0.0, p)));
    }
}
