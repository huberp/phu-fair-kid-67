#pragma once

namespace Circuit {

/// Stamp an independent voltage source into the MNA A matrix.
///
/// The voltage source augments the system by one row/column (vsrcRow).
/// The unknowns x[vsrcRow] represent the current flowing out of the positive
/// terminal (nodeP) and into the external circuit.
///
/// @param A        Pointer to the row-major MNA matrix (size × size).
/// @param size     MNA system dimension (numNodes + numVSrc).
/// @param vsrcRow  Row/column index assigned to this source (= numNodes + vsrcIdx).
/// @param nodeP    Positive terminal node (1-indexed; 0 = ground).
/// @param nodeN    Negative terminal node (1-indexed; 0 = ground).
inline void stampVoltageSourceKCL(double* A, int size,
                                  int vsrcRow, int nodeP, int nodeN) noexcept
{
    const int p = nodeP - 1;
    const int n = nodeN - 1;
    if (nodeP > 0) {
        A[vsrcRow * size + p] += 1.0;
        A[p * size + vsrcRow] += 1.0;
    }
    if (nodeN > 0) {
        A[vsrcRow * size + n] -= 1.0;
        A[n * size + vsrcRow] -= 1.0;
    }
}

} // namespace Circuit
