#include "TimingNetwork.h"

#include <cmath>

namespace Models::Sidechain {

// Schematic-derived component references (Fairchild 670 circuit diagram):
//   Attack path  : R107 / R108 = 30 Ω (timing resistor), C101 / C102 = 4 µF
//                  Theoretical τ_min = 30 Ω × 4 µF = 0.12 ms; effective attack
//                  includes 6AL5 diode source resistance → ~0.2 ms (manual).
//   Release path : S102 switch selects R140 (100 kΩ) or R138 (470 kΩ) in
//                  combination with C110 (8 µF) or C112 (20 µF).
//                  Positions 2 & 3 are schematic-confirmed:
//                    Pos 2 : R140 × C110 = 100 kΩ × 8 µF  = 0.80 s  ✓
//                    Pos 3 : R140 × C112 = 100 kΩ × 20 µF = 2.00 s  ✓
//                  Positions 1, 4–6 match manual spec; exact switch sub-network
//                  for pos 1 (0.30 s) and pos 4 (5.00 s) not fully traced.
const std::array<TimingPreset, kNumTimingPresets> kTimingPresets = {{
    // Positions 1–4: fixed attack/release.
    // Attack 0.2 ms: manual-specified; schematic R107/R108=30 Ω + C101=4 µF
    // gives τ_RC=0.12 ms; 6AL5 source impedance raises effective τ to ~0.2 ms.
    { TimingKind::Fixed,       0.0002, 0.30, {} },  // Position 1 — manual spec
    { TimingKind::Fixed,       0.0002, 0.80, {} },  // Position 2 — schematic-confirmed (R140×C110)
    { TimingKind::Fixed,       0.0002, 2.00, {} },  // Position 3 — schematic-confirmed (R140×C112)
    { TimingKind::Fixed,       0.0002, 5.00, {} },  // Position 4 — manual spec

    // Positions 5–6: automatic / programme-dependent release (manual section 4).
    // Two parallel release branches produce quick initial recovery after brief
    // transients, and slower recovery after sustained heavy limiting.
    // releaseSec is set to the slow branch constant for backward-compatible
    // step-response measurements (see TimingPreset docs).
    // Pos 5 slow ≈ R138×C112 = 470 kΩ × 20 µF = 9.4 s (≈ 10 s, schematic estimate).
    { TimingKind::AutoRelease, 0.0002, 10.0, { 0.50, 10.0 } },  // Position 5
    { TimingKind::AutoRelease, 0.0002, 25.0, { 1.00, 25.0 } },  // Position 6
}};

double computeAlpha(double tauSec, double sampleRate) noexcept
{
    return std::exp(-1.0 / (tauSec * sampleRate));
}

} // namespace Models::Sidechain
