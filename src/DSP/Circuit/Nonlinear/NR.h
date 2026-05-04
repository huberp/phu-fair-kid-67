#pragma once

#include <array>
#include <cmath>
#include <limits>

namespace Circuit {
namespace Nonlinear {

/// Configuration parameters for the Newton-Raphson iteration policy.
struct NRConfig {
    int    maxIterations  = 10;    ///< Maximum NR iterations per sample.
    double convergenceTol = 1e-9;  ///< Convergence: |Δx|₂ < tol · (|x|₂ + 1).
    double dampingFactor  = 1.0;   ///< Step scale α ∈ (0, 1]; 1 = full Newton step.
    double maxDeltaV      = 1.0;   ///< Per-element step limit (≤ 0 = unlimited).
};

/// Outcome of a single Newton-Raphson solve call.
struct NRResult {
    bool converged  = false; ///< True if the convergence criterion was satisfied.
    int  iterations = 0;     ///< Number of iterations taken this call.
};

/// Newton-Raphson iteration manager.
///
/// Drives an iterative nonlinear solve. The caller provides a "step" callback
/// that, given the current iterate x, computes one undamped Newton step and
/// writes the new estimate back into x. This class then handles:
///   - per-element step limiting,
///   - damped updates (x ← x_prev + α · Δx),
///   - convergence checking (|Δx|₂ / (|x|₂ + 1) < tol),
///   - NaN / Inf sanitization, and
///   - fallback to the last-known-good solution on non-convergence.
///
/// The N template parameter fixes the state-vector size at compile time,
/// enabling stack allocation of all scratch buffers and eliminating heap
/// indirection in the hot path.
///
/// The step callback signature must be compatible with
/// `bool(std::array<double, N>&)`:
///   - Receives the current iterate by reference and overwrites it with the
///     next Newton estimate.
///   - Returns `true` on success, `false` if the underlying linear solve
///     failed (e.g. singular Jacobian) — triggers an immediate fallback.
///
/// Typical usage:
/// @code
///   NRPolicy<2> nr;
///   std::array<double, 2> x = {initialGuess0, initialGuess1};
///   auto result = nr.solve(x, [&](std::array<double, 2>& x) {
///       // 1. Stamp nonlinear elements based on current x
///       // 2. Solve the linearised MNA system, write result into x
///       return true; // false if the linear solve failed
///   });
///   if (!result.converged) { /* x was restored to the initial guess */ }
/// @endcode
template <std::size_t N>
class NRPolicy {
public:
    explicit NRPolicy(NRConfig cfg = {}) noexcept : cfg_(cfg) {}

    /// Replace the iteration policy configuration at runtime.
    /// Safe to call between samples; the new settings take effect on the
    /// next solve() call.
    void setConfig(NRConfig cfg) noexcept { cfg_ = std::move(cfg); }

    /// Read-only access to the current iteration policy configuration.
    [[nodiscard]] const NRConfig& config() const noexcept { return cfg_; }

    /// Run the NR iteration loop.
    ///
    /// @param x     On entry: initial guess (typically the previous sample's
    ///              solution). On exit: converged solution, or the original
    ///              value of x restored as a fallback if convergence failed.
    /// @param step  Callable `bool(std::array<double, N>& x)` — see class doc.
    /// @return      NRResult with convergence flag and iteration count.
    template <typename StepFn>
    NRResult solve(std::array<double, N>& x, StepFn step);

#ifndef NDEBUG
    /// Cumulative iteration count since the last resetCounters() call.
    /// Only available in debug builds.
    int  totalIterations() const noexcept { return totalIter_; }

    /// Reset the cumulative iteration counter to zero.
    void resetCounters() noexcept { totalIter_ = 0; }
#endif

private:
    NRConfig              cfg_;
    std::array<double, N> xPrev_;     ///< Scratch: iterate before each step.
    std::array<double, N> xFallback_; ///< Scratch: entry state used for fallback.

#ifndef NDEBUG
    int totalIter_ = 0;
#endif
};

// ── Template implementation ───────────────────────────────────────────────────

template <std::size_t N>
template <typename StepFn>
NRResult NRPolicy<N>::solve(std::array<double, N>& x, StepFn step)
{
    // Record the entry state as the fallback solution.
    xFallback_ = x;

    NRResult result;

    for (int iter = 0; iter < cfg_.maxIterations; ++iter) {
        // Save the current iterate before the step.
        xPrev_ = x;

        // Compute one Newton step (callback updates x in place).
        const bool ok = step(x);

        if (!ok) {
            // The linear solve failed (e.g. singular Jacobian) — fall back.
            x = xFallback_;
            return result;
        }

        // Sanitize: replace any NaN / Inf with the fallback and abort.
        for (std::size_t i = 0; i < N; ++i) {
            if (!std::isfinite(x[i])) {
                x = xFallback_;
                return result;
            }
        }

        // Compute delta, apply per-element step limiting and damping.
        double deltaL2 = 0.0;
        double xL2     = 0.0;
        for (std::size_t i = 0; i < N; ++i) {
            double d = x[i] - xPrev_[i];

            // Clamp per-element step magnitude if a limit is configured.
            if (cfg_.maxDeltaV > 0.0 && std::abs(d) > cfg_.maxDeltaV)
                d = std::copysign(cfg_.maxDeltaV, d);

            // Apply damping: x ← x_prev + α · d
            x[i] = xPrev_[i] + cfg_.dampingFactor * d;

            deltaL2 += d * d;
            xL2     += x[i] * x[i];
        }

        ++result.iterations;
#ifndef NDEBUG
        ++totalIter_;
#endif

        // Convergence check: |Δx|₂ / (|x|₂ + 1) < tol
        if (std::sqrt(deltaL2) < cfg_.convergenceTol * (std::sqrt(xL2) + 1.0)) {
            result.converged = true;
            return result;
        }
    }

    // Exceeded the maximum iteration count without convergence — restore fallback.
    x = xFallback_;
    return result;
}

} // namespace Nonlinear
} // namespace Circuit
