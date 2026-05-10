#include "TimingNetwork.h"

#include <cmath>

namespace Models::Sidechain {

const std::array<TimingPreset, kNumTimingPresets> kTimingPresets = {{
    // Positions 1–4: fixed attack/release (manual-derived).
    // Attack is held at 0.2 ms for all positions to match the Fairchild 670
    // manual, which specifies a roughly constant, very fast attack.
    { TimingKind::Fixed,       0.0002, 0.30, {} },  // Position 1
    { TimingKind::Fixed,       0.0002, 0.80, {} },  // Position 2
    { TimingKind::Fixed,       0.0002, 2.00, {} },  // Position 3
    { TimingKind::Fixed,       0.0002, 5.00, {} },  // Position 4

    // Positions 5–6: automatic / program-dependent release (manual section 4).
    // Two parallel release branches produce quick initial recovery after brief
    // transients, and slower recovery after sustained heavy limiting.
    // releaseSec is set to the slow branch constant for backward-compatible
    // step-response measurements (see TimingPreset docs).
    { TimingKind::AutoRelease, 0.0002, 10.0, { 0.50, 10.0 } },  // Position 5
    { TimingKind::AutoRelease, 0.0002, 25.0, { 1.00, 25.0 } },  // Position 6
}};

double computeAlpha(double tauSec, double sampleRate) noexcept
{
    return std::exp(-1.0 / (tauSec * sampleRate));
}

} // namespace Models::Sidechain
