#pragma once

#include "analog/nonlinear/TriodeKoren.h"

namespace DSP {

/// Approximate Koren model parameters for the Fairchild 6386 remote-cutoff pentode.
///
/// The 6386 is a frame-grid remote-cutoff pentode used in the Fairchild 670/670
/// variable-mu compressor.  Its defining characteristic is a very gradual
/// transconductance vs. grid-voltage curve: unlike a sharp-cutoff tube (which
/// reaches a definite knee and then shuts off), the 6386's gain continues to
/// decrease smoothly even at deep negative bias — it never "clamps".
///
/// The key difference from the 6072 sharp-cutoff model is the exponent x = 1.0:
/// with x = 1 the Koren model computes Ip ∝ E1 (linear, not power-law), giving
/// a gradual, never-abrupt gain-reduction curve.  The 6072 uses x = 1.4, which
/// produces a sharper knee.
///
/// kg1 is reduced from the 6072 value (1060) to 700 to increase the
/// transconductance (gm) of the remote-cutoff stage.  The 6386 has a higher gm
/// than the 6072 preamp triode, which is necessary to produce the 20–30 dB of
/// gain reduction visible in the Fairchild 670 manual transfer curves.
///
/// This is a first-order approximation; a more accurate fit would require
/// matching published 6386 plate curves or measured hardware data.
inline Analog::Nonlinear::TubeParams tubeParams6386() noexcept
{
    return { /*mu=*/70.0, /*kp=*/300.0, /*kvb=*/300.0, /*kg1=*/700.0, /*x=*/1.0 };
}

} // namespace DSP
