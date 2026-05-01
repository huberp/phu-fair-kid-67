#include "Circuit.h"

#include "Elements/Resistor.h"
#include "Elements/VoltageSource.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>

namespace Circuit {

Circuit::Circuit(int numNodes, int numVSrc)
    : n_(numNodes), v_(numVSrc), size_(numNodes + numVSrc)
{
    assert(numNodes >= 0 && numVSrc >= 0);
    A_base_.assign(size_ * size_, 0.0);
    LU_.resize(size_ * size_);
    piv_.resize(size_);
    z_.resize(size_, 0.0);
    x_.resize(size_, 0.0);
    tmp_.resize(size_);
    caps_.reserve(16);
}

// ── Element stamping ──────────────────────────────────────────────────────────

void Circuit::stampResistor(double ohms, int nodeP, int nodeN)
{
    assert(ohms > 0.0);
    stampConductance(A_base_.data(), size_, 1.0 / ohms, nodeP, nodeN);
    factorized_ = false;
}

int Circuit::stampCapacitor(double farads, int nodeP, int nodeN)
{
    assert(farads > 0.0);
    CapRecord rec;
    rec.nodeP = nodeP;
    rec.nodeN = nodeN;
    rec.companion.farads = farads;
    // Geq will be stamped during prepare(); Ieq starts at 0.
    const int index = static_cast<int>(caps_.size());
    caps_.push_back(rec);
    factorized_ = false;
    return index;
}

void Circuit::stampVoltageSource(int vsrcIdx, int nodeP, int nodeN)
{
    assert(vsrcIdx >= 0 && vsrcIdx < v_);
    stampVoltageSourceKCL(A_base_.data(), size_, n_ + vsrcIdx, nodeP, nodeN);
    factorized_ = false;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void Circuit::prepare(double sampleRate)
{
    assert(sampleRate > 0.0);
    T_ = 1.0 / sampleRate;

    for (auto& rec : caps_) {
        // Remove the previous capacitor companion conductance from A_base_.
        stampConductance(A_base_.data(), size_, -rec.companion.Geq,
                         rec.nodeP, rec.nodeN);
        // Recompute Geq for the new sample rate and stamp it.
        rec.companion.prepare(T_);
        stampConductance(A_base_.data(), size_, rec.companion.Geq,
                         rec.nodeP, rec.nodeN);
    }

    factorized_ = luFactor();
}

void Circuit::reset()
{
    for (auto& rec : caps_)
        rec.companion.reset();
}

// ── Per-sample API ────────────────────────────────────────────────────────────

void Circuit::beginStep()
{
    std::fill(z_.begin(), z_.end(), 0.0);
}

void Circuit::setVoltageSourceValue(int vsrcIdx, double volts)
{
    assert(vsrcIdx >= 0 && vsrcIdx < v_);
    z_[n_ + vsrcIdx] = volts;
}

void Circuit::injectCurrent(int node, double amps)
{
    assert(node > 0 && node <= n_);
    z_[node - 1] += amps;
}

bool Circuit::solve()
{
    if (!factorized_)
        return false;

    // Add capacitor companion current sources to z_.
    // I_C[k] = Geq·Vc − Ieq  →  Ieq is a current source flowing into nodeP.
    for (const auto& rec : caps_) {
        const double Ieq = rec.companion.Ieq;
        if (rec.nodeP > 0) z_[rec.nodeP - 1] += Ieq;
        if (rec.nodeN > 0) z_[rec.nodeN - 1] -= Ieq;
    }

    luSolve();

    // Update each capacitor's companion model for the next step.
    for (auto& rec : caps_) {
        double Vc = 0.0;
        if (rec.nodeP > 0) Vc += x_[rec.nodeP - 1];
        if (rec.nodeN > 0) Vc -= x_[rec.nodeN - 1];
        rec.companion.update(Vc);
    }

    return true;
}

// ── Query ─────────────────────────────────────────────────────────────────────

double Circuit::nodeVoltage(int node) const noexcept
{
    if (node <= 0 || node > n_) return 0.0;
    return x_[node - 1];
}

double Circuit::vsrcCurrent(int vsrcIdx) const noexcept
{
    assert(vsrcIdx >= 0 && vsrcIdx < v_);
    return x_[n_ + vsrcIdx];
}

// ── LU factorisation ──────────────────────────────────────────────────────────

bool Circuit::luFactor()
{
    LU_ = A_base_;
    std::iota(piv_.begin(), piv_.end(), 0);

    for (int k = 0; k < size_; k++) {
        // Partial pivoting: find the row with the largest |value| in column k.
        int    maxRow = k;
        double maxVal = std::abs(LU_[mIdx(k, k)]);
        for (int i = k + 1; i < size_; i++) {
            const double v = std::abs(LU_[mIdx(i, k)]);
            if (v > maxVal) { maxVal = v; maxRow = i; }
        }
        if (maxVal < 1e-14)
            return false; // near-singular

        if (maxRow != k) {
            std::swap(piv_[k], piv_[maxRow]);
            for (int j = 0; j < size_; j++)
                std::swap(LU_[mIdx(k, j)], LU_[mIdx(maxRow, j)]);
        }

        const double inv = 1.0 / LU_[mIdx(k, k)];
        for (int i = k + 1; i < size_; i++) {
            LU_[mIdx(i, k)] *= inv; // store L factor below the diagonal
            for (int j = k + 1; j < size_; j++)
                LU_[mIdx(i, j)] -= LU_[mIdx(i, k)] * LU_[mIdx(k, j)];
        }
    }
    return true;
}

void Circuit::luSolve()
{
    // Apply row permutation: tmp_ = P · z_
    for (int i = 0; i < size_; i++)
        tmp_[i] = z_[piv_[i]];

    // Forward substitution: L · y = tmp_  (L is unit lower triangular)
    for (int i = 1; i < size_; i++)
        for (int j = 0; j < i; j++)
            tmp_[i] -= LU_[mIdx(i, j)] * tmp_[j];

    // Back substitution: U · x_ = tmp_
    for (int i = size_ - 1; i >= 0; i--) {
        for (int j = i + 1; j < size_; j++)
            tmp_[i] -= LU_[mIdx(i, j)] * tmp_[j];
        tmp_[i] /= LU_[mIdx(i, i)];
    }

    x_ = tmp_;
}

} // namespace Circuit
