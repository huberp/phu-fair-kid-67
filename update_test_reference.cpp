#include <cmath>
#include <cstdio>
#include <vector>
#include <iostream>

#include "src/DSP/Models/Fairchild/Fairchild670Core.h"

using namespace phu::dsp::fairchild;

// From test
static float dbfsToAmplitude(float dbfs) {
    return std::pow(10.0f, dbfs / 20.0f);
}

static float amplitudeToDbfs(float amp) {
    if (amp <= 0.0f) return -144.0f;
    return 20.0f * std::log10(amp);
}

struct TransferMeasurement {
    float inputDbfs;
    float outputDbfs;
    float grDb;
};

static TransferMeasurement measureTransfer(
    float amplitude,
    double sampleRate = 44100.0,
    int settleN = 80000,
    int measureN = 8192,
    float threshV = 0.0f)
{
    Fairchild670CoreConfig cfg;
    cfg.linkMode = LinkMode::Independent;
    cfg.timingPreset = Sidechain::TimingPosition::P1;

    Fairchild670Core core(cfg);
    core.prepare(sampleRate);
    core.setThresholdLeft(threshV);
    core.setThresholdRight(threshV);

    // Settle
    float outL, outR;
    const float freqHz = 1000.0f;
    for (int i = 0; i < settleN; ++i) {
        const float in = amplitude * std::sin(
            2.0f * static_cast<float>(M_PI) * freqHz
                * static_cast<float>(i) / static_cast<float>(sampleRate));
        core.processStereo(in, in, outL, outR);
    }

    // Measure
    double sumInSq = 0.0;
    double sumOutSq = 0.0;
    for (int i = 0; i < measureN; ++i) {
        const float in = amplitude * std::sin(
            2.0f * static_cast<float>(M_PI) * freqHz
                * static_cast<float>(settleN + i)
                / static_cast<float>(sampleRate));
        core.processStereo(in, in, outL, outR);
        sumInSq += static_cast<double>(in) * static_cast<double>(in);
        sumOutSq += static_cast<double>(outL) * static_cast<double>(outL);
    }

    const float rmsIn = static_cast<float>(std::sqrt(sumInSq / measureN));
    const float rmsOut = static_cast<float>(std::sqrt(sumOutSq / measureN));

    TransferMeasurement m;
    m.inputDbfs = amplitudeToDbfs(rmsIn);
    m.outputDbfs = amplitudeToDbfs(rmsOut);
    m.grDb = m.inputDbfs - m.outputDbfs;
    return m;
}

int main() {
    std::vector<float> testLevels = { -40.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f };
    
    std::printf("Updated reference table for TransferCurveTests.cpp (gain=0.7):\n\n");
    std::printf("constexpr std::array<TransferReference, 7> kTransferReference = {{\n");
    
    for (float dbfs : testLevels) {
        const float amp = dbfsToAmplitude(dbfs);
        auto m = measureTransfer(amp, 44100.0, 80000, 8192, 0.0f);
        std::printf("    { %6.1ff, %7.2ff, %6.2ff },   // %s\n",
                    m.inputDbfs,
                    m.outputDbfs,
                    m.grDb,
                    dbfs >= 0.0f ? "0 dBFS full scale" :
                    dbfs >= -3.0f ? "heavy limiting" :
                    dbfs >= -6.0f ? "noticeable GR" :
                    dbfs >= -12.0f ? "moderate compression" :
                    dbfs >= -18.0f ? "gentle compression" :
                    "slight compression");
    }
    std::printf("}};\n");
    
    return 0;
}
