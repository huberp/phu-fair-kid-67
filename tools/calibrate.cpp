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
#include <limits>
#include <sstream>
#include <stdexcept>
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

enum class TransferSweepMode {
    Up,
    Down,
    Both
};

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
///   input_dbfs, output_dbfs, gain_reduction_db, cv_volts,
///   raw_cv_volts, effective_cv_volts, applied_cv_volts, stage_cv_volts,
///   cv_clamp_ratio
static void measureTransfer(std::ostream& out,
                             int           positionIdx,
                             double        sampleRate,
                             float         threshVoltage = 0.0f,
                             float         sidechainGain = 0.7f,
                             float         cvSoftKneeV   = 0.75f,
                             float         cvMaxV        = 9.0f,
                             float         minInputDbfs  = -60.0f,
                             float         maxInputDbfs  = 6.0f,
                             float         stepDb        = 3.0f,
                             int           measureSamples = 1024,
                             float         settleMultiplier = 10.0f,
                             float         minSettleSec = 2.0f,
                             bool          resetPerLevel = true,
                             TransferSweepMode sweepMode = TransferSweepMode::Up)
{
    using namespace Models;
    using namespace Models::Sidechain;

    const TimingPosition pos = static_cast<TimingPosition>(positionIdx);
    const auto& preset = kTimingPresets[positionIdx];

    out << "# transfer_measurement\n";
    out << "# position=" << (positionIdx + 1)
        << " kind=" << (preset.kind == TimingKind::Fixed ? "Fixed" : "AutoRelease")
        << " threshold_v=" << threshVoltage
        << " sidechain_gain=" << sidechainGain
        << " cv_soft_knee_v=" << cvSoftKneeV
        << " cv_max_v=" << cvMaxV
        << " transfer_min_dbfs=" << minInputDbfs
        << " transfer_max_dbfs=" << maxInputDbfs
        << " transfer_step_db=" << stepDb
        << " transfer_measure_samples=" << measureSamples
        << " transfer_settle_multiplier=" << settleMultiplier
        << " transfer_min_settle_sec=" << minSettleSec
        << " transfer_reset_per_level=" << (resetPerLevel ? 1 : 0)
        << " sample_rate=" << sampleRate << "\n";
        out << "sweep_direction,input_dbfs,output_dbfs,gain_reduction_db,cv_volts,"
            "raw_cv_volts,effective_cv_volts,applied_cv_volts,stage_cv_volts,"
            "cv_clamp_ratio\n";

    if (stepDb <= 0.0f) {
        throw std::invalid_argument("transfer step must be positive");
    }
    if (measureSamples <= 0) {
        throw std::invalid_argument("transfer measure sample count must be positive");
    }
    if (maxInputDbfs < minInputDbfs) {
        std::swap(maxInputDbfs, minInputDbfs);
    }

    std::vector<float> levels;
    for (float db = minInputDbfs; db <= maxInputDbfs + 1e-6f; db += stepDb) {
        levels.push_back(db);
    }
    if (levels.empty()) {
        levels.push_back(minInputDbfs);
    }
    if (std::abs(levels.back() - maxInputDbfs) > 1e-3f) {
        levels.push_back(maxInputDbfs);
    }

    Fairchild670CoreConfig cfg;
    cfg.linkMode = LinkMode::Independent;
    cfg.timingPreset = pos;
    cfg.sidechainAmplifierGain = sidechainGain;
    cfg.sidechainCvSoftKneeV = cvSoftKneeV;
    cfg.stageCfg.cvMaxV = cvMaxV;

    const double settleTimeSec = std::max(static_cast<double>(minSettleSec),
                                          static_cast<double>(preset.releaseSec) * settleMultiplier);
    const int settleN = std::max(1, static_cast<int>(settleTimeSec * sampleRate));

    auto runSweep = [&](const std::vector<float>& sweepLevels, const char* sweepLabel) {
        Fairchild670Core core(cfg);
        core.prepare(sampleRate);
        core.setThresholdLeft(threshVoltage);
        core.setThresholdRight(threshVoltage);

        float outL = 0.0f, outR = 0.0f;
        const float freqHz = 1000.0f;
        long long sampleOffset = 0;

        for (float dbfs : sweepLevels) {
            const float amplitude = (dbfs <= -144.0f) ? 0.0f : dbfsToAmplitude(dbfs);

            if (resetPerLevel) {
                core = Fairchild670Core(cfg);
                core.prepare(sampleRate);
                core.setThresholdLeft(threshVoltage);
                core.setThresholdRight(threshVoltage);
                sampleOffset = 0;
            }

            const long long settleStart = sampleOffset;
            for (int i = 0; i < settleN; ++i) {
                const float in = amplitude * std::sin(
                    2.0f * static_cast<float>(M_PI) * freqHz
                        * static_cast<float>(settleStart + i) / static_cast<float>(sampleRate));
                core.processStereo(in, in, outL, outR);
            }
            sampleOffset += settleN;

        double sumInSq  = 0.0;
        double sumOutSq = 0.0;
        double sumFinalCv     = 0.0;
        double sumRawCv       = 0.0;
        double sumEffectiveCv = 0.0;
        double sumAppliedCv   = 0.0;
        double sumStageCv     = 0.0;
        double sumClamp       = 0.0;

            const long long measureStart = sampleOffset;
        for (int i = 0; i < measureSamples; ++i) {
            const float in = amplitude * std::sin(
                2.0f * static_cast<float>(M_PI) * freqHz
                    * static_cast<float>(measureStart + i)
                    / static_cast<float>(sampleRate));
            core.processStereo(in, in, outL, outR);
            sumInSq  += static_cast<double>(in)   * static_cast<double>(in);
            sumOutSq += static_cast<double>(outL)  * static_cast<double>(outL);
            const auto meters = core.meters();
            sumFinalCv     += static_cast<double>(meters.cvL);
            sumRawCv       += static_cast<double>(meters.rawCvL);
            sumEffectiveCv += static_cast<double>(meters.effectiveCvL);
            sumAppliedCv   += static_cast<double>(meters.appliedCvL);
            sumStageCv     += static_cast<double>(meters.stageCvL);
            sumClamp       += static_cast<double>(meters.cvClampedL);
        }
            sampleOffset += measureSamples;

        const float rmsIn  = static_cast<float>(std::sqrt(sumInSq  / measureSamples));
        const float rmsOut = static_cast<float>(std::sqrt(sumOutSq / measureSamples));

        const float inDbfs  = amplitudeToDbfs(rmsIn);
        const float outDbfs = amplitudeToDbfs(rmsOut);
        const float grDb    = inDbfs - outDbfs;
        const float avgFinalCv = static_cast<float>(sumFinalCv / measureSamples);
        const float avgRawCv = static_cast<float>(sumRawCv / measureSamples);
        const float avgEffectiveCv = static_cast<float>(sumEffectiveCv / measureSamples);
        const float avgAppliedCv = static_cast<float>(sumAppliedCv / measureSamples);
        const float avgStageCv = static_cast<float>(sumStageCv / measureSamples);
        const float clampRatio = static_cast<float>(sumClamp / measureSamples);

        out << sweepLabel << ","
            << inDbfs << ","
            << outDbfs << ","
            << grDb << ","
            << avgFinalCv << ","
            << avgRawCv << ","
            << avgEffectiveCv << ","
            << avgAppliedCv << ","
            << avgStageCv << ","
            << clampRatio << "\n";
        }
    };

    if (sweepMode == TransferSweepMode::Up || sweepMode == TransferSweepMode::Both) {
        runSweep(levels, "up");
    }
    if (sweepMode == TransferSweepMode::Down || sweepMode == TransferSweepMode::Both) {
        std::vector<float> down(levels.rbegin(), levels.rend());
        runSweep(down, "down");
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
        << "  --sidechain-gain <x> Sidechain CV gain scalar (default: 0.7).\n"
        << "  --cv-soft-knee <V>   Soft-knee width before CV ceiling clamp (default: 0.75).\n"
        << "  --cv-max <V>         Maximum stage CV ceiling (default: 9.0).\n"
        << "  --transfer-min-dbfs <dB>   Lowest transfer input level (default: -60).\n"
        << "  --transfer-max-dbfs <dB>   Highest transfer input level (default: +6).\n"
        << "  --transfer-step-db <dB>    Transfer level step (default: 3).\n"
        << "  --transfer-measure-samples <N>   RMS measurement window size (default: 1024).\n"
        << "  --transfer-settle-multiplier <x> Settling window = x * release tau (default: 10).\n"
        << "  --transfer-min-settle-sec <sec>  Minimum settle time per level (default: 2).\n"
        << "  --transfer-reset-per-level  Re-initialise core at each level (legacy behavior).\n"
        << "  --transfer-sweep-mode <up|down|both> Sweep direction(s) for path checks (default: up).\n"
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
    bool        doTiming      = false;
    bool        doTransfer    = false;
    int         position      = 1;       // 1-based
    double      sampleRate    = 44100.0;
    float       thresholdV    = 0.0f;
    float       sidechainGain = 0.7f;
    float       cvSoftKneeV   = 0.75f;
    float       cvMaxV        = 9.0f;
    float       transferMinDbfs = -60.0f;
    float       transferMaxDbfs = 6.0f;
    float       transferStepDb = 3.0f;
    int         transferMeasureSamples = 1024;
    float       transferSettleMultiplier = 10.0f;
    float       transferMinSettleSec = 2.0f;
    bool        transferResetPerLevel = false;
    TransferSweepMode transferSweepMode = TransferSweepMode::Up;
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
        } else if (arg == "--sidechain-gain" && i + 1 < argc) {
            sidechainGain = std::stof(argv[++i]);
        } else if (arg == "--cv-soft-knee" && i + 1 < argc) {
            cvSoftKneeV = std::stof(argv[++i]);
        } else if (arg == "--cv-max" && i + 1 < argc) {
            cvMaxV = std::stof(argv[++i]);
        } else if (arg == "--transfer-min-dbfs" && i + 1 < argc) {
            transferMinDbfs = std::stof(argv[++i]);
        } else if (arg == "--transfer-max-dbfs" && i + 1 < argc) {
            transferMaxDbfs = std::stof(argv[++i]);
        } else if (arg == "--transfer-step-db" && i + 1 < argc) {
            transferStepDb = std::stof(argv[++i]);
        } else if (arg == "--transfer-measure-samples" && i + 1 < argc) {
            transferMeasureSamples = std::stoi(argv[++i]);
        } else if (arg == "--transfer-settle-multiplier" && i + 1 < argc) {
            transferSettleMultiplier = std::stof(argv[++i]);
        } else if (arg == "--transfer-min-settle-sec" && i + 1 < argc) {
            transferMinSettleSec = std::stof(argv[++i]);
        } else if (arg == "--transfer-reset-per-level") {
            transferResetPerLevel = true;
        } else if (arg == "--transfer-sweep-mode" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode == "up") {
                transferSweepMode = TransferSweepMode::Up;
            } else if (mode == "down") {
                transferSweepMode = TransferSweepMode::Down;
            } else if (mode == "both") {
                transferSweepMode = TransferSweepMode::Both;
            } else {
                std::cerr << "Error: --transfer-sweep-mode must be one of up|down|both.\n";
                return 1;
            }
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

    if (sidechainGain < 0.0f) {
        std::cerr << "Error: --sidechain-gain must be non-negative.\n";
        return 1;
    }

    if (cvSoftKneeV < 0.0f) {
        std::cerr << "Error: --cv-soft-knee must be non-negative.\n";
        return 1;
    }

    if (cvMaxV <= 0.0f) {
        std::cerr << "Error: --cv-max must be positive.\n";
        return 1;
    }
    if (transferStepDb <= 0.0f) {
        std::cerr << "Error: --transfer-step-db must be positive.\n";
        return 1;
    }
    if (transferMeasureSamples <= 0) {
        std::cerr << "Error: --transfer-measure-samples must be positive.\n";
        return 1;
    }
    if (transferSettleMultiplier <= 0.0f) {
        std::cerr << "Error: --transfer-settle-multiplier must be positive.\n";
        return 1;
    }
    if (transferMinSettleSec < 0.0f) {
        std::cerr << "Error: --transfer-min-settle-sec must be non-negative.\n";
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
        measureTransfer(out,
                        posIdx,
                        sampleRate,
                        thresholdV,
                        sidechainGain,
                        cvSoftKneeV,
                        cvMaxV,
                        transferMinDbfs,
                        transferMaxDbfs,
                        transferStepDb,
                        transferMeasureSamples,
                        transferSettleMultiplier,
                        transferMinSettleSec,
                        transferResetPerLevel,
                        transferSweepMode);
    }

    return 0;
}
