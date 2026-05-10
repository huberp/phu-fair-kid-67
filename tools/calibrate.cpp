/// phu-fair-kid-67 offline calibration tool
///
/// Standalone CLI tool that exercises the Fairchild670Core DSP without a DAW.
/// Outputs timing and transfer-curve data as CSV to stdout or a file.
///
/// Usage:
///   calibrate --measure-timing --position <1-6> [--sample-rate <Hz>]
///   calibrate --measure-transfer             [--position <1-6>] [--sample-rate <Hz>]
///   calibrate --measure-timing --measure-transfer --position <1-6>
///
/// Options:
///   --measure-timing         Measure attack/release step-response for the
///                            selected timing position and export time-series CSV.
///   --measure-transfer       Measure steady-state input-vs-output transfer curve
///                            across a range of input levels and export CSV.
///   --position <1-6>         Timing position (default: 1).
///   --sample-rate <Hz>       Sample rate in Hz (default: 44100).
///   --output <file>          Write CSV to <file> instead of stdout.
///   --help                   Show this help and exit.
///
/// Examples:
///   calibrate --measure-timing  --position 1
///   calibrate --measure-timing  --position 5
///   calibrate --measure-transfer
///   calibrate --measure-timing --measure-transfer --position 6 --output out.csv
///
/// The tool uses the same DSP core as the plugin and is fully deterministic.

#include "../src/DSP/Models/Fairchild/Fairchild670Core.h"
#include "../src/DSP/Models/Sidechain/TimingNetwork.h"
#include "../src/DSP/Models/Sidechain/TimingNetworkAdapter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Helper utilities
// ─────────────────────────────────────────────────────────────────────────────

static float dbfsToAmplitude(float dbfs)
{
    return std::pow(10.0f, dbfs / 20.0f);
}

static float amplitudeToDbfs(float amp)
{
    if (amp <= 0.0f) return -144.0f;
    return 20.0f * std::log10(amp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Timing measurement
// ─────────────────────────────────────────────────────────────────────────────

/// Measure the step-response (attack + release) of the detector for one
/// timing position and write a CSV with columns:
///   sample_index, phase, input_normalised, cv_volts, time_sec
///
/// Phase values: "attack" / "release"
static void measureTiming(std::ostream& out,
                           int           positionIdx,  // 0-based
                           double        sampleRate)
{
    using namespace Models::Sidechain;

    const TimingPosition pos = static_cast<TimingPosition>(positionIdx);
    const auto& preset = kTimingPresets[positionIdx];

    // Determine how long to run each phase.
    // For AutoRelease presets the output is determined by both branches; use
    // the slow branch constant so the full release tail is captured.
    const double effectiveReleaseSec =
        (preset.kind == TimingKind::AutoRelease)
            ? preset.autoRelease.slowReleaseSec
            : preset.releaseSec;

    const double attackDuration  = std::max(0.01, preset.attackSec * 10.0);
    const double releaseDuration = effectiveReleaseSec * 3.0;

    const int attackN  = static_cast<int>(attackDuration  * sampleRate) + 1;
    const int releaseN = static_cast<int>(releaseDuration * sampleRate) + 1;

    Analog::Models::Sidechain::RectifierDetector det(toDetectorConfig(pos));
    det.prepare(sampleRate);

    out << "# timing_measurement\n";
    out << "# position=" << (positionIdx + 1)
        << " kind=" << (preset.kind == TimingKind::Fixed ? "Fixed" : "AutoRelease")
        << " attack_sec=" << preset.attackSec
        << " release_sec=" << preset.releaseSec;
    if (preset.kind == TimingKind::AutoRelease) {
        out << " fast_release_sec=" << preset.autoRelease.fastReleaseSec
            << " slow_release_sec=" << preset.autoRelease.slowReleaseSec;
    }
    out << " sample_rate=" << sampleRate << "\n";
    out << "sample_index,phase,input_normalised,cv_volts,time_sec\n";

    // Attack phase: unit step input.
    for (int n = 0; n < attackN; ++n) {
        const float cv  = det.processSample(1.0f);
        const double t  = static_cast<double>(n) / sampleRate;
        out << n << ",attack,1.0," << cv << "," << t << "\n";
    }

    // Release phase: silence input, CV decays.
    for (int n = 0; n < releaseN; ++n) {
        const float cv  = det.processSample(0.0f);
        const double t  = static_cast<double>(attackN + n) / sampleRate;
        out << (attackN + n) << ",release,0.0," << cv << "," << t << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Transfer-curve measurement
// ─────────────────────────────────────────────────────────────────────────────

/// Measure steady-state input-vs-output transfer at a range of dBFS levels.
/// Outputs CSV with columns:
///   input_dbfs, output_dbfs, gain_reduction_db, cv_volts
static void measureTransfer(std::ostream& out,
                             int           positionIdx,
                             double        sampleRate,
                             float         threshVoltage = 0.0f)
{
    using namespace Models;
    using namespace Models::Sidechain;

    const TimingPosition pos = static_cast<TimingPosition>(positionIdx);
    const auto& preset = kTimingPresets[positionIdx];

    out << "# transfer_measurement\n";
    out << "# position=" << (positionIdx + 1)
        << " kind=" << (preset.kind == TimingKind::Fixed ? "Fixed" : "AutoRelease")
        << " threshold_v=" << threshVoltage
        << " sample_rate=" << sampleRate << "\n";
    out << "input_dbfs,output_dbfs,gain_reduction_db,cv_volts\n";

    // Input levels from -60 dBFS to 0 dBFS.
    const std::vector<float> levels = {
        -60.0f, -48.0f, -40.0f, -36.0f, -30.0f, -24.0f,
        -20.0f, -18.0f, -15.0f, -12.0f, -9.0f,  -6.0f,
        -3.0f,  -1.5f,   0.0f
    };

    for (float dbfs : levels) {
        const float amplitude = (dbfs <= -144.0f) ? 0.0f : dbfsToAmplitude(dbfs);

        Fairchild670CoreConfig cfg;
        cfg.linkMode           = LinkMode::Independent;
        cfg.timingPreset = pos;

        Fairchild670Core core(cfg);
        core.prepare(sampleRate);
        core.setThresholdLeft(threshVoltage);
        core.setThresholdRight(threshVoltage);

        // Settle for 10× the release time constant (or a minimum of 2 s).
        const double settleTimeSec = std::max(2.0, preset.releaseSec * 10.0);
        const int    settleN       = static_cast<int>(settleTimeSec * sampleRate);

        float outL, outR;
        const float freqHz = 1000.0f;
        for (int i = 0; i < settleN; ++i) {
            const float in = amplitude * std::sin(
                2.0f * static_cast<float>(M_PI) * freqHz
                    * static_cast<float>(i) / static_cast<float>(sampleRate));
            core.processStereo(in, in, outL, outR);
        }

        // Measurement window: 1024 samples.
        constexpr int kMeasureN = 1024;
        double sumInSq  = 0.0;
        double sumOutSq = 0.0;
        float  lastCv   = 0.0f;

        for (int i = 0; i < kMeasureN; ++i) {
            const float in = amplitude * std::sin(
                2.0f * static_cast<float>(M_PI) * freqHz
                    * static_cast<float>(settleN + i)
                    / static_cast<float>(sampleRate));
            core.processStereo(in, in, outL, outR);
            sumInSq  += static_cast<double>(in)   * static_cast<double>(in);
            sumOutSq += static_cast<double>(outL)  * static_cast<double>(outL);
            lastCv    = core.meters().cvL;
        }

        const float rmsIn  = static_cast<float>(std::sqrt(sumInSq  / kMeasureN));
        const float rmsOut = static_cast<float>(std::sqrt(sumOutSq / kMeasureN));

        const float inDbfs  = amplitudeToDbfs(rmsIn);
        const float outDbfs = amplitudeToDbfs(rmsOut);
        const float grDb    = inDbfs - outDbfs;

        out << inDbfs << "," << outDbfs << "," << grDb << "," << lastCv << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CLI entry point
// ─────────────────────────────────────────────────────────────────────────────

static void printHelp(const char* progName)
{
    std::cerr
        << "Usage:\n"
        << "  " << progName << " --measure-timing [--position <1-6>] [--sample-rate <Hz>]\n"
        << "  " << progName << " --measure-transfer [--position <1-6>] [--sample-rate <Hz>]\n"
        << "  " << progName << " --measure-timing --measure-transfer ...\n"
        << "\n"
        << "Options:\n"
        << "  --measure-timing     Emit step-response (attack+release) CSV.\n"
        << "  --measure-transfer   Emit transfer-curve CSV.\n"
        << "  --position <1-6>     Timing switch position (default: 1).\n"
        << "  --sample-rate <Hz>   Sample rate in Hz (default: 44100).\n"
        << "  --threshold <V>      Threshold voltage for transfer measurement (default: 0).\n"
        << "  --output <file>      Write output to file instead of stdout.\n"
        << "  --help               Show this help and exit.\n"
        << "\n"
        << "Examples:\n"
        << "  " << progName << " --measure-timing --position 1\n"
        << "  " << progName << " --measure-timing --position 5\n"
        << "  " << progName << " --measure-transfer --sample-rate 48000\n"
        << "  " << progName << " --measure-timing --measure-transfer --position 6 --output out.csv\n";
}

int main(int argc, char* argv[])
{
    bool        doTiming     = false;
    bool        doTransfer   = false;
    int         position     = 1;       // 1-based
    double      sampleRate   = 44100.0;
    float       thresholdV   = 0.0f;
    std::string outputFile;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "--measure-timing") {
            doTiming = true;
        } else if (arg == "--measure-transfer") {
            doTransfer = true;
        } else if (arg == "--position" && i + 1 < argc) {
            position = std::stoi(argv[++i]);
        } else if (arg == "--sample-rate" && i + 1 < argc) {
            sampleRate = std::stod(argv[++i]);
        } else if (arg == "--threshold" && i + 1 < argc) {
            thresholdV = std::stof(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            outputFile = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }

    if (!doTiming && !doTransfer) {
        std::cerr << "Error: specify at least one of --measure-timing or --measure-transfer.\n\n";
        printHelp(argv[0]);
        return 1;
    }

    if (position < 1 || position > Models::Sidechain::kNumTimingPresets) {
        std::cerr << "Error: --position must be 1–"
                  << Models::Sidechain::kNumTimingPresets << ".\n";
        return 1;
    }

    if (sampleRate <= 0.0) {
        std::cerr << "Error: --sample-rate must be positive.\n";
        return 1;
    }

    // Open output stream.
    std::ofstream fileStream;
    if (!outputFile.empty()) {
        fileStream.open(outputFile);
        if (!fileStream.is_open()) {
            std::cerr << "Error: cannot open output file: " << outputFile << "\n";
            return 1;
        }
    }
    std::ostream& out = outputFile.empty() ? std::cout : fileStream;

    const int posIdx = position - 1; // convert to 0-based

    if (doTiming) {
        measureTiming(out, posIdx, sampleRate);
        if (doTransfer)
            out << "\n"; // blank line between sections
    }

    if (doTransfer) {
        measureTransfer(out, posIdx, sampleRate, thresholdV);
    }

    return 0;
}
