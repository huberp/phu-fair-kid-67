#pragma once

namespace Circuit {

/// Stamp a conductance G = 1/R into a row-major MNA conductance matrix.
///
/// Node numbering: 0 = ground (excluded from the system).
/// Internal nodes 1…n map to MNA row/column indices 0…n-1.
///
/// @param G_mat  Pointer to the row-major MNA matrix (size × size).
/// @param size   MNA system dimension (numNodes + numVSrc).
/// @param G      Conductance in Siemens (= 1/R).
/// @param nodeP  Positive node (1-indexed; 0 = ground).
/// @param nodeN  Negative node (1-indexed; 0 = ground).
inline void stampConductance(double* G_mat, int size,
                             double G, int nodeP, int nodeN) noexcept
{
    const int p = nodeP - 1;
    const int n = nodeN - 1;
    if (nodeP > 0) G_mat[p * size + p] += G;
    if (nodeN > 0) G_mat[n * size + n] += G;
    if (nodeP > 0 && nodeN > 0) {
        G_mat[p * size + n] -= G;
        G_mat[n * size + p] -= G;
    }
}

} // namespace Circuit
