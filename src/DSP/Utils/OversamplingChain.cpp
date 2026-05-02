#include "OversamplingChain.h"

namespace DSP {

// ── Constructor ───────────────────────────────────────────────────────────────

OversamplingChain::OversamplingChain() = default;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void OversamplingChain::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = maxBlockSize;

    rebuildOversampler();

    // Prepare the core at the oversampled rate so all time constants
    // (detector attack/release, cathode capacitors) remain correct.
    const double oversampledRate = sampleRate_ * static_cast<double>(1 << order_);
    core_.prepare(oversampledRate);

    prepared_ = true;
}

void OversamplingChain::reset()
{
    if (oversampler_ != nullptr)
        oversampler_->reset();
    core_.reset();
}

// ── Processing ────────────────────────────────────────────────────────────────

void OversamplingChain::process(juce::AudioBuffer<float>& buffer)
{
    juce::dsp::AudioBlock<float> block(buffer);

    if (order_ == 0 || oversampler_ == nullptr) {
        // 1× — no oversampling: process directly.
        const int numSamples = buffer.getNumSamples();
        float* chL = buffer.getWritePointer(0);
        float* chR = buffer.getWritePointer(1);
        float  outL, outR;
        for (int n = 0; n < numSamples; ++n) {
            core_.processStereo(chL[n], chR[n], outL, outR);
            chL[n] = outL;
            chR[n] = outR;
        }
        return;
    }

    // Upsample.
    auto osBlock = oversampler_->processSamplesUp(block);

    // Process the upsampled block through the core sample-by-sample.
    const int numOsSamples = static_cast<int>(osBlock.getNumSamples());
    float* chL = osBlock.getChannelPointer(0);
    float* chR = osBlock.getChannelPointer(1);
    float  outL, outR;
    for (int n = 0; n < numOsSamples; ++n) {
        core_.processStereo(chL[n], chR[n], outL, outR);
        chL[n] = outL;
        chR[n] = outR;
    }

    // Downsample back to the host rate.
    oversampler_->processSamplesDown(block);
}

// ── Latency ───────────────────────────────────────────────────────────────────

int OversamplingChain::getLatencySamples() const noexcept
{
    if (oversampler_ == nullptr)
        return 0;
    return static_cast<int>(oversampler_->getLatencyInSamples());
}

// ── Configuration ─────────────────────────────────────────────────────────────

void OversamplingChain::setOversamplingOrder(int order)
{
    jassert(order >= 0 && order <= 3); // valid: 0=1x, 1=2x, 2=4x, 3=8x
    if (order_ == order)
        return;

    order_ = order;

    if (prepared_) {
        rebuildOversampler();
        // Re-prepare the core at the (potentially new) oversampled rate.
        const double oversampledRate = sampleRate_ * static_cast<double>(1 << order_);
        core_.prepare(oversampledRate);
    }
}

void OversamplingChain::setQuality(Models::ProcessingQuality quality)
{
    core_.setQuality(quality);
}

// ── Private helpers ───────────────────────────────────────────────────────────

void OversamplingChain::rebuildOversampler()
{
    if (order_ == 0) {
        oversampler_.reset();
        return;
    }

    // Equiripple half-band FIR: best alias rejection, at the cost of extra
    // latency (~10–20 samples at 2× depending on order).
    oversampler_ = std::make_unique<juce::dsp::Oversampling<float>>(
        2,      // stereo
        order_,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple);

    oversampler_->initProcessing(static_cast<size_t>(maxBlockSize_));
}

} // namespace DSP
