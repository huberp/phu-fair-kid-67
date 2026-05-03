#pragma once

#include "Elements/Capacitor.h"
#include "Elements/Inductor.h"
#include <vector>

namespace Circuit {

/// Linear MNA (Modified Nodal Analysis) circuit solver.
///
/// Supports resistors, capacitors (trapezoidal companion model), inductors
/// (trapezoidal companion model), coupled inductors (mutual-inductance
/// stamping), and independent voltage sources.  All internal buffers are
/// preallocated in the constructor; no heap allocations occur during
/// per-sample processing.
///
/// Node numbering convention:
///   0            = ground (reference, excluded from the MNA system).
///   1 … numNodes = circuit nodes.
/// Voltage sources are indexed 0 … numVSrc-1.
///
/// Typical usage:
/// @code
///   Circuit::Circuit mna(2, 1);
///   mna.stampResistor(1000.0, 1, 2);
///   mna.stampCapacitor(1e-6, 2, 0);
///   mna.stampVoltageSource(0, 1, 0);
///   mna.prepare(44100.0);
///
///   // Per-sample loop:
///   mna.beginStep();
///   mna.setVoltageSourceValue(0, inputSample * 10.0);
///   mna.solve();
///   double out = mna.nodeVoltage(2);
/// @endcode
class Circuit {
public:
    /// Preallocate all internal buffers.
    /// @param numNodes  Number of non-ground circuit nodes.
    /// @param numVSrc   Number of independent voltage sources.
    Circuit(int numNodes, int numVSrc);

    // ── Element stamping ─────────────────────────────────────────────────────
    // Call before prepare(). nodeP/nodeN use the 1-indexed convention (0 = ground).

    /// Stamp a resistor between nodeP and nodeN.
    void stampResistor(double ohms, int nodeP, int nodeN);

    /// Stamp a capacitor between nodeP and nodeN.
    /// @return Capacitor index (for future reference).
    int  stampCapacitor(double farads, int nodeP, int nodeN);

    /// Stamp an inductor between nodeP and nodeN.
    /// @return Inductor index (for future reference).
    int  stampInductor(double henries, int nodeP, int nodeN);

    /// Stamp two magnetically coupled inductors L1 and L2 with mutual
    /// inductance M.  The coupling coefficient k = M / sqrt(L1·L2) must
    /// satisfy |k| < 1 (strict, to keep the inductance matrix non-singular).
    /// @return Coupled-inductor record index (for future reference).
    int  stampCoupledInductors(double L1, double L2, double M,
                               int nodeP1, int nodeN1,
                               int nodeP2, int nodeN2);

    /// Stamp voltage source vsrcIdx between nodeP (+) and nodeN (−).
    void stampVoltageSource(int vsrcIdx, int nodeP, int nodeN);

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /// Recompute capacitor companion conductances and refactorize the A matrix.
    /// Must be called after all stamps and whenever the sample rate changes.
    void prepare(double sampleRate);

    /// Reset all capacitor state to zero initial conditions.
    void reset();

    // ── Per-sample API ───────────────────────────────────────────────────────

    /// Clear the RHS vector. Call at the start of each sample.
    void beginStep();

    /// Set the voltage (V) for independent voltage source vsrcIdx.
    void setVoltageSourceValue(int vsrcIdx, double volts);

    /// Inject an external current (A) into a node (node must be > 0).
    void injectCurrent(int node, double amps);

    /// Solve the MNA system.
    /// Internally adds capacitor companion currents to the RHS, then solves and
    /// updates capacitor state for the next step.
    /// @return true on success; false if the matrix is (near-)singular.
    bool solve();

    // ── Query ────────────────────────────────────────────────────────────────

    /// Node voltage after solve(). Returns 0.0 for ground (node == 0).
    double nodeVoltage(int node) const noexcept;

    /// Current through voltage source vsrcIdx after solve().
    /// Positive = current flowing out of the + terminal into the circuit.
    double vsrcCurrent(int vsrcIdx) const noexcept;

private:
    int n_;     ///< Number of non-ground nodes.
    int v_;     ///< Number of voltage sources.
    int size_;  ///< MNA system dimension = n_ + v_.

    double T_ = 1.0 / 44100.0; ///< Sample period (seconds).

    /// A_base_: constant part of the MNA A matrix (size_ × size_, row-major).
    /// Contains stamps from resistors, cap companion conductances, vsrc rows/cols.
    std::vector<double> A_base_;

    /// LU_: in-place LU factorisation of A_base_ with partial pivoting.
    std::vector<double> LU_;

    /// piv_: row permutation from partial pivoting; piv_[i] = original row at i.
    std::vector<int> piv_;

    /// z_: RHS vector; cleared each step, filled with vsrc values and currents.
    std::vector<double> z_;

    /// x_: solution vector populated by solve().
    std::vector<double> x_;

    /// tmp_: scratch space for LU forward/back substitution.
    std::vector<double> tmp_;

    struct CapRecord {
        int nodeP, nodeN;
        CapacitorCompanion companion;
    };
    std::vector<CapRecord> caps_;

    struct IndRecord {
        int nodeP, nodeN;
        InductorCompanion companion;
    };
    std::vector<IndRecord> inductors_;

    struct CoupledRecord {
        int nodeP1, nodeN1, nodeP2, nodeN2;
        CoupledInductorCompanion companion;
    };
    std::vector<CoupledRecord> coupled_;

    bool factorized_ = false;

    int mIdx(int r, int c) const noexcept { return r * size_ + c; }

    bool luFactor();
    void luSolve();
};

} // namespace Circuit
