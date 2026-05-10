#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "analog/nonlinear/NR.h"

#include <array>
#include <cmath>
#include <limits>

using namespace Analog::Nonlinear;

// ── Convergence: scalar √2 via Newton's method ───────────────────────────────

TEST_CASE("NR: converges to sqrt(2) from x=1", "[nr][convergence]") {
    // Toy nonlinear problem: f(x) = x² − 2 = 0  →  x* = √2 ≈ 1.41421…
    // Newton step: x_new = x − f(x)/f'(x) = (x + 2/x) / 2
    NRPolicy<1> nr;
    std::array<double, 1> x = {1.0};

    auto stepFn = [](std::array<double, 1>& x) -> bool {
        const double v = x[0];
        if (std::abs(v) < 1e-14) return false; // singular Jacobian
        x[0] = 0.5 * (v + 2.0 / v);
        return true;
    };

    NRResult res = nr.solve(x, stepFn);

    REQUIRE(res.converged);
    REQUIRE(res.iterations >= 1);
    REQUIRE(res.iterations <= 10);
    REQUIRE_THAT(x[0], Catch::Matchers::WithinAbs(std::sqrt(2.0), 1e-9));
}

// ── Convergence: vector fixed-point converges in one step ────────────────────

TEST_CASE("NR: converges in one step when starting at the fixed point", "[nr][convergence]") {
    // The step always returns [1.0, 2.0].  Starting from the fixed point itself
    // means delta == 0 on the first call, so the solver detects convergence
    // in exactly one iteration regardless of tolerance.
    NRPolicy<2> nr;
    std::array<double, 2> x = {1.0, 2.0}; // start at the solution

    auto stepFn = [](std::array<double, 2>& x) -> bool {
        x[0] = 1.0;
        x[1] = 2.0;
        return true;
    };

    NRResult res = nr.solve(x, stepFn);

    REQUIRE(res.converged);
    REQUIRE(res.iterations == 1);
    REQUIRE_THAT(x[0], Catch::Matchers::WithinAbs(1.0, 1e-12));
    REQUIRE_THAT(x[1], Catch::Matchers::WithinAbs(2.0, 1e-12));
}

// ── No NaN / Inf when the step produces NaN ──────────────────────────────────

TEST_CASE("NR: no NaN/Inf when step returns NaN — fallback applied", "[nr][robustness]") {
    NRPolicy<1> nr;
    std::array<double, 1> x = {1.5};

    auto nanStep = [](std::array<double, 1>& x) -> bool {
        x[0] = std::numeric_limits<double>::quiet_NaN();
        return true;
    };

    NRResult res = nr.solve(x, nanStep);

    REQUIRE_FALSE(res.converged);
    REQUIRE(std::isfinite(x[0]));                                  // sanitized
    REQUIRE_THAT(x[0], Catch::Matchers::WithinAbs(1.5, 1e-12));   // restored
}

TEST_CASE("NR: no NaN/Inf when step returns Inf — fallback applied", "[nr][robustness]") {
    NRPolicy<1> nr;
    std::array<double, 1> x = {2.0};

    auto infStep = [](std::array<double, 1>& x) -> bool {
        x[0] = std::numeric_limits<double>::infinity();
        return true;
    };

    NRResult res = nr.solve(x, infStep);

    REQUIRE_FALSE(res.converged);
    REQUIRE(std::isfinite(x[0]));
    REQUIRE_THAT(x[0], Catch::Matchers::WithinAbs(2.0, 1e-12));
}

// ── Fallback when maximum iterations exceeded ─────────────────────────────────

TEST_CASE("NR: falls back to initial x when maxIterations exceeded", "[nr][fallback]") {
    // A step that oscillates x → −x forever; will never satisfy any tolerance.
    NRConfig cfg;
    cfg.maxIterations  = 5;
    cfg.convergenceTol = 1e-12;
    cfg.maxDeltaV      = 0.0; // disable step limiting so the full delta is used
    NRPolicy<1> nr(cfg);

    std::array<double, 1> x = {3.0};

    auto divergeStep = [](std::array<double, 1>& x) -> bool {
        x[0] = -x[0]; // flip sign every iteration
        return true;
    };

    NRResult res = nr.solve(x, divergeStep);

    REQUIRE_FALSE(res.converged);
    REQUIRE(res.iterations == 5);                                  // ran all iters
    REQUIRE_THAT(x[0], Catch::Matchers::WithinAbs(3.0, 1e-12));   // restored
}

// ── Fallback when the step callback signals failure ───────────────────────────

TEST_CASE("NR: falls back when step returns false (singular Jacobian)", "[nr][fallback]") {
    NRPolicy<1> nr;
    std::array<double, 1> x = {7.0};

    auto failStep = [](std::array<double, 1>& x) -> bool {
        x[0] = 99.0; // would corrupt x
        return false; // signal linear-solve failure
    };

    NRResult res = nr.solve(x, failStep);

    REQUIRE_FALSE(res.converged);
    REQUIRE(res.iterations == 0);                                  // callback fired but count not incremented before early return
    REQUIRE_THAT(x[0], Catch::Matchers::WithinAbs(7.0, 1e-12));   // restored
}

// ── Per-element step limiting ─────────────────────────────────────────────────

TEST_CASE("NR: step limiting clamps large deltas (scalar)", "[nr][damping]") {
    // The step tries to jump from 0 → 10 V; the limit of 0.5 V clamps it to 0.5.
    // With a generous convergenceTol the solution is accepted after one iteration.
    // L2 convergence: sqrt(0.25) = 0.5 < 1.1·(sqrt(0.25)+1) = 1.65 ✓
    NRConfig cfg;
    cfg.maxIterations  = 5;
    cfg.convergenceTol = 1.1;
    cfg.maxDeltaV      = 0.5;
    NRPolicy<1> nr(cfg);

    std::array<double, 1> x = {0.0};

    auto bigStep = [](std::array<double, 1>& x) -> bool {
        x[0] = 10.0;
        return true;
    };

    NRResult res = nr.solve(x, bigStep);

    REQUIRE(res.converged);
    REQUIRE(x[0] <= 0.5 + 1e-12); // step was clamped, not the full 10 V
}

TEST_CASE("NR: step limiting clamps large deltas (2D)", "[nr][damping]") {
    // Both elements would jump by 5 V; the 0.5 V limit clamps each to ±0.5.
    // L2 convergence: sqrt(0.5) ≈ 0.707 < 1.1·(sqrt(0.5)+1) ≈ 1.878 ✓
    NRConfig cfg;
    cfg.maxIterations  = 5;
    cfg.convergenceTol = 1.1;
    cfg.maxDeltaV      = 0.5;
    NRPolicy<2> nr(cfg);

    std::array<double, 2> x = {0.0, 0.0};

    auto bigStep2D = [](std::array<double, 2>& x) -> bool {
        x[0] = 5.0;
        x[1] = 5.0;
        return true;
    };

    NRResult res = nr.solve(x, bigStep2D);

    REQUIRE(res.converged);
    REQUIRE(x[0] <= 0.5 + 1e-12); // each element clamped
    REQUIRE(x[1] <= 0.5 + 1e-12);
}

// ── Damping factor ────────────────────────────────────────────────────────────

TEST_CASE("NR: damping factor scales the applied step", "[nr][damping]") {
    // Full Newton step from 0 → 2 V; with α = 0.5 the applied step is 1 V.
    // The convergence check uses the step-limited (pre-damping) delta d = 2.0:
    //   sqrt(4) = 2.0 < 1.1·(sqrt(1)+1) = 2.2 ✓  (|x| is after damping = 1.0)
    NRConfig cfg;
    cfg.maxIterations  = 5;
    cfg.convergenceTol = 1.1;
    cfg.dampingFactor  = 0.5;
    cfg.maxDeltaV      = 100.0; // no limiting
    NRPolicy<1> nr(cfg);

    std::array<double, 1> x = {0.0};

    auto stepFn = [](std::array<double, 1>& x) -> bool {
        x[0] = 2.0;
        return true;
    };

    NRResult res = nr.solve(x, stepFn);

    REQUIRE(res.converged);
    REQUIRE_THAT(x[0], Catch::Matchers::WithinAbs(1.0, 1e-12)); // 0.5 × (2−0)
}

// ── Debug counters ────────────────────────────────────────────────────────────

#ifndef NDEBUG
TEST_CASE("NR: debug counters accumulate across calls and reset correctly", "[nr][debug]") {
    NRPolicy<1> nr;
    std::array<double, 1> x = {1.0};

    auto stepFn = [](std::array<double, 1>& x) -> bool {
        const double v = x[0];
        if (std::abs(v) < 1e-14) return false;
        x[0] = 0.5 * (v + 2.0 / v);
        return true;
    };

    nr.resetCounters();
    REQUIRE(nr.totalIterations() == 0);

    NRResult r1 = nr.solve(x, stepFn);
    REQUIRE(nr.totalIterations() == r1.iterations);

    // Second call: x ≈ √2, converges in one extra iteration.
    NRResult r2 = nr.solve(x, stepFn);
    REQUIRE(nr.totalIterations() == r1.iterations + r2.iterations);

    nr.resetCounters();
    REQUIRE(nr.totalIterations() == 0);
}
#endif
