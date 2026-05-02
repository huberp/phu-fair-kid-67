#include "TimingNetwork.h"

#include <cmath>

namespace Models::Sidechain {

const std::array<TimingPreset, kNumTimingPresets> kTimingPresets = {{
    { 0.0002, 0.30 },  // Position 1
    { 0.0002, 0.80 },  // Position 2
    { 0.0004, 2.00 },  // Position 3
    { 0.0008, 5.00 },  // Position 4
    { 0.0020, 10.00 }, // Position 5
    { 0.0080, 25.00 }, // Position 6
}};

double computeAlpha(double tauSec, double sampleRate) noexcept
{
    return std::exp(-1.0 / (tauSec * sampleRate));
}

} // namespace Models::Sidechain
