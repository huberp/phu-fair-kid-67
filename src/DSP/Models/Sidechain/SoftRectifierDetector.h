#pragma once

#include "analog/models/sidechain/RectifierDetector.h"
#include "analog/dsp/UnitScaling.h"

#include <algorithm>
#include <cmath>

namespace Models::Sidechain {

/// Tube rectifier detector modelling the 6AL5 twin-diode in the Fairchild 670
/// sidechain — including its forward-voltage soft onset.
///
/// The 6AL5 has a forward voltage of approximately 0.8 V, so the rectifier
/// does not respond to signals smaller than Vf.  Above Vf, conduction
/// increases progressively, giving the hardware a natural inherent soft
/// threshold even with the front-panel threshold set to maximum.  This is a
/// key reason the 670 sounds gentle on low-level material.
///
/// Signal path per sample:
///   1. Full-wave rectification:  rectVolts = max(0, |sampleToVolts(x)| − Vf)
///   2. Convert back to normalised domain and pass to the inner RectifierDetector
///      for RC smoothing.
///
/// When forwardVoltageV = 0 (the default) this class is behaviourally
/// identical to the underlying RectifierDetector.
class SoftRectifierDetector {
public:
    explicit SoftRectifierDetector(
        Analog::Models::Sidechain::RectifierDetectorConfig cfg = {},
        float forwardVoltageV = 0.0f) noexcept
        : inner_(cfg), forwardVoltageV_(std::max(0.0f, forwardVoltageV))
    {}

    void prepare(double sampleRate) noexcept { inner_.prepare(sampleRate); }
    void reset()  noexcept { inner_.reset(); }

    void setConfig(Analog::Models::Sidechain::RectifierDetectorConfig cfg) noexcept
    {
        inner_.setConfig(cfg);
    }

    void setForwardVoltage(float vf) noexcept
    {
        forwardVoltageV_ = std::max(0.0f, vf);
    }

    [[nodiscard]] float controlVoltage() const noexcept
    {
        return inner_.controlVoltage();
    }

    /// Process one audio sample.
    /// @param sample  Normalised input (±1.0 full-scale).
    /// @return        Smoothed control voltage (V).
    [[nodiscard]] float processSample(float sample) noexcept
    {
        // Full-wave rectification with forward-voltage dead zone.
        // Converting back to normalised domain ensures the inner detector's
        // own sampleToVolts call reconstructs `rectVolts` exactly.
        const float volts     = Analog::sampleToVolts(sample);
        const float rectVolts = std::max(0.0f, std::abs(volts) - forwardVoltageV_);
        return inner_.processSample(Analog::voltsToSample(rectVolts));
    }

private:
    Analog::Models::Sidechain::RectifierDetector inner_;
    float forwardVoltageV_;
};

} // namespace Models::Sidechain
