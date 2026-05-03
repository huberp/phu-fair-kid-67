#pragma once

namespace Circuit {

/// Trapezoidal (bilinear) companion model for a single inductor.
///
/// Using the bilinear transform approximation for V_L = L·dI_L/dt:
///   I_L[k] = Geq · V_L[k] + Ieq
/// where
///   Geq = T / (2·L)                        (companion conductance)
///   Ieq = I_L[k−1] + Geq · V_L[k−1]       (history current source)
///
/// Update rule after solving for V_L[k]:
///   Ieq_next = 2·Geq·V_L[k] + Ieq_old
///
/// The Geq term is stamped into the MNA A matrix once (per prepare()).
/// The Ieq term is injected into the MNA z vector at every sample (in solve()).
///
/// Sign convention: I_L flows from nodeP to nodeN (conventional current).
/// In MNA the Ieq current source therefore draws current from nodeP and
/// delivers it to nodeN:
///   z[nodeP − 1] −= Ieq
///   z[nodeN − 1] += Ieq
struct InductorCompanion {
    double henries = 0.0;
    double Geq     = 0.0;  ///< Companion conductance; depends on sample rate.
    double Ieq     = 0.0;  ///< History current-source value.

    /// Recompute Geq from inductance and sample period T = 1/sampleRate.
    void prepare(double T) noexcept
    {
        Geq = T / (2.0 * henries);
    }

    /// Reset history to zero initial conditions.
    void reset() noexcept
    {
        Ieq = 0.0;
    }

    /// Update the companion model after the system has been solved for V_L[k].
    /// @param VL  Inductor voltage V_L[k] = V_nodeP − V_nodeN.
    void update(double VL) noexcept
    {
        Ieq = 2.0 * Geq * VL + Ieq;
    }
};

/// Trapezoidal companion model for two magnetically coupled inductors L1, L2
/// with mutual inductance M (= k·√(L1·L2), where |k| < 1).
///
/// The impedance matrix Z = [[L1, M], [M, L2]] is inverted to form companion
/// conductances G11, G22, G12 and history current sources Ieq1, Ieq2.
///
///   I_1[k] = G11·V_L1[k] + G12·V_L2[k] + Ieq1
///   I_2[k] = G12·V_L1[k] + G22·V_L2[k] + Ieq2
///
/// where
///   D   = L1·L2 − M²        (must be > 0; i.e. |k| < 1)
///   G11 = T·L2 / (2·D)
///   G22 = T·L1 / (2·D)
///   G12 = −T·M  / (2·D)     (negative when M > 0)
///
/// Update rules (after solving for V_L1[k] and V_L2[k]):
///   Ieq1_next = 2·G11·V_L1 + 2·G12·V_L2 + Ieq1_old
///   Ieq2_next = 2·G12·V_L1 + 2·G22·V_L2 + Ieq2_old
///
/// MNA injection: I_1 flows from nodeP1 to nodeN1; I_2 from nodeP2 to nodeN2.
///   z[nodeP1−1] −= Ieq1,  z[nodeN1−1] += Ieq1
///   z[nodeP2−1] −= Ieq2,  z[nodeN2−1] += Ieq2
struct CoupledInductorCompanion {
    double L1  = 0.0;
    double L2  = 0.0;
    double M   = 0.0;   ///< Mutual inductance (H).

    double G11 = 0.0;   ///< Companion conductance for port 1.
    double G22 = 0.0;   ///< Companion conductance for port 2.
    double G12 = 0.0;   ///< Mutual companion conductance.
                        ///< Negative when M > 0: a positive primary voltage
                        ///< induces current opposing the build-up in the
                        ///< secondary, consistent with Lenz's law and the
                        ///< sign convention I_2 = G12·V_L1 + G22·V_L2 + Ieq2.

    double Ieq1 = 0.0;  ///< History current source for port 1.
    double Ieq2 = 0.0;  ///< History current source for port 2.

    /// Recompute companion conductances from inductances and sample period T.
    void prepare(double T) noexcept
    {
        const double D = L1 * L2 - M * M;
        const double c = T / (2.0 * D);
        G11 = c * L2;
        G22 = c * L1;
        G12 = -c * M;
    }

    /// Reset history to zero initial conditions.
    void reset() noexcept
    {
        Ieq1 = 0.0;
        Ieq2 = 0.0;
    }

    /// Update the companion model after solving for V_L1[k] and V_L2[k].
    /// @param VL1  Port-1 voltage V_L1[k] = V_nodeP1 − V_nodeN1.
    /// @param VL2  Port-2 voltage V_L2[k] = V_nodeP2 − V_nodeN2.
    void update(double VL1, double VL2) noexcept
    {
        const double newIeq1 = 2.0 * G11 * VL1 + 2.0 * G12 * VL2 + Ieq1;
        const double newIeq2 = 2.0 * G12 * VL1 + 2.0 * G22 * VL2 + Ieq2;
        Ieq1 = newIeq1;
        Ieq2 = newIeq2;
    }
};

/// Stamp the off-diagonal mutual-conductance VCCS terms into a row-major MNA
/// matrix for a coupled-inductor pair.
///
/// Each port's current has a component proportional to the OTHER port's
/// voltage.  For port 1: Gm·(V_P2−V_N2) flows from P1 to N1.
/// For port 2: Gm·(V_P1−V_N1) flows from P2 to N2.
/// Because Gm is equal for both directions the resulting stamp is symmetric.
///
/// @param G_mat          MNA matrix pointer (row-major, size×size).
/// @param size           MNA system dimension (numNodes + numVSrc).
/// @param Gm             Mutual conductance (= G12 from CoupledInductorCompanion).
/// @param nodeP1,nodeN1  Port-1 nodes (1-indexed; 0 = ground).
/// @param nodeP2,nodeN2  Port-2 nodes (1-indexed; 0 = ground).
inline void stampMutualConductance(double* G_mat, int size, double Gm,
                                   int nodeP1, int nodeN1,
                                   int nodeP2, int nodeN2) noexcept
{
    const int p1 = nodeP1 - 1, n1 = nodeN1 - 1;
    const int p2 = nodeP2 - 1, n2 = nodeN2 - 1;

    // Gm*(V_P2 − V_N2) flows from P1 to N1:
    if (nodeP1 > 0 && nodeP2 > 0) G_mat[p1 * size + p2] -= Gm;
    if (nodeP1 > 0 && nodeN2 > 0) G_mat[p1 * size + n2] += Gm;
    if (nodeN1 > 0 && nodeP2 > 0) G_mat[n1 * size + p2] += Gm;
    if (nodeN1 > 0 && nodeN2 > 0) G_mat[n1 * size + n2] -= Gm;

    // Gm*(V_P1 − V_N1) flows from P2 to N2 (symmetric):
    if (nodeP2 > 0 && nodeP1 > 0) G_mat[p2 * size + p1] -= Gm;
    if (nodeP2 > 0 && nodeN1 > 0) G_mat[p2 * size + n1] += Gm;
    if (nodeN2 > 0 && nodeP1 > 0) G_mat[n2 * size + p1] += Gm;
    if (nodeN2 > 0 && nodeN1 > 0) G_mat[n2 * size + n1] -= Gm;
}

} // namespace Circuit
