#pragma once

/// @file TimingNetworkAdapter.h
/// @brief Adapter between Fairchild-specific TimingPosition presets and
///        Analog::Models::Sidechain::RectifierDetectorConfig.
///
/// This header-only adapter is the only place in the plugin that knows about
/// both the plugin-specific preset table (TimingNetwork.h) and the generic
/// lib config struct (analog/models/sidechain/RectifierDetector.h).  The lib
/// class itself has no knowledge of Fairchild timing presets.

#include "TimingNetwork.h"
#include "analog/models/sidechain/RectifierDetector.h"

namespace Models::Sidechain {

/// Map a Fairchild timing position to a generic RectifierDetectorConfig.
///
/// @param pos  Timing preset position (P1–P6).
/// @return     Config struct suitable for Analog::Models::Sidechain::RectifierDetector.
[[nodiscard]] inline Analog::Models::Sidechain::RectifierDetectorConfig
toDetectorConfig(TimingPosition pos) noexcept
{
    const auto& p = kTimingPresets[static_cast<int>(pos)];

    Analog::Models::Sidechain::RectifierDetectorConfig cfg;
    cfg.attackSec = p.attackSec;

    if (p.kind == TimingKind::Fixed) {
        cfg.kind       = Analog::Models::Sidechain::TimingKind::Fixed;
        cfg.releaseSec = p.releaseSec;
    } else {
        cfg.kind          = Analog::Models::Sidechain::TimingKind::AutoRelease;
        cfg.fastReleaseSec = p.autoRelease.fastReleaseSec;
        cfg.slowReleaseSec = p.autoRelease.slowReleaseSec;
    }

    return cfg;
}

} // namespace Models::Sidechain
