# PHU FAIR KID 67 — Building from Source & Contributing

> This document is for developers and contributors who want to compile the plug-in themselves, modify the DSP code, or run the unit tests.

---

## Table of Contents

1. [Build Prerequisites](#1-build-prerequisites)
2. [Cloning the Repository](#2-cloning-the-repository)
3. [Build Instructions](#3-build-instructions)
4. [Running the Tests](#4-running-the-tests)
5. [Project Structure](#5-project-structure)
6. [Contributing](#6-contributing)
7. [License](#7-license)

---

## 1. Build Prerequisites

### All Platforms

| Tool | Minimum Version | Notes |
|---|---|---|
| **CMake** | 3.28 | Required for CMake Presets and FetchContent |
| **C++ compiler** | C++20 | See platform-specific requirements below |
| **Git** | Any recent | Needed by CMake to fetch JUCE and `phu-audio-lib` |

### Windows

| Tool | Notes |
|---|---|
| **Visual Studio 2022 or later** | Desktop C++ workload required |
| **Windows SDK** | Included with Visual Studio |

> The Windows presets use the `Visual Studio 18 2026` generator. If you are on Visual Studio 2022, configure with `cmake -G "Visual Studio 17 2022" -B build/vs2022 -DBUILD_PLUGIN=ON` instead.

### macOS

| Tool | Notes |
|---|---|
| **Xcode 13 or later** | Command Line Tools are not sufficient; the full Xcode app is required for AU builds |
| **macOS 11 SDK or later** | Included with Xcode |

### Linux

| Package | Notes |
|---|---|
| `build-essential` / `gcc` / `clang` | C++20-capable compiler (GCC 11 or Clang 14 or newer) |
| JUCE system dependencies | See [JUCE Linux dependencies](https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md) — typically: `libasound2-dev libfreetype6-dev libx11-dev libxinerama-dev libxrandr-dev libxcursor-dev mesa-common-dev` |

A convenience script that installs all required Linux packages is available at `scripts/install-linux-deps.sh`:

```bash
sudo ./scripts/install-linux-deps.sh
```

---

## 2. Cloning the Repository

JUCE (8.0.15) and `phu-audio-lib` (branch `main`) are **not** git submodules. CMake downloads them into `JUCE/` and `phu-audio-lib/` (git-ignored) during the first configure, so network access to GitHub is required. See `cmake/PhuDependencies.cmake`.

```bash
git clone https://github.com/huberp/phu-fair-kid-67.git
cd phu-fair-kid-67
```

To work against a local checkout of the shared library, configure with `-DFETCHCONTENT_SOURCE_DIR_PHU-AUDIO-LIB=<path>`.

---

## 3. Build Instructions

### Option A — Using CMake Presets (recommended)

Presets are defined in `CMakePresets.json` and follow `<os>-<arch>-<format>-<config>` (macOS omits the format and builds VST3 + AU). Every configure preset has a matching `-build` preset; list them with `cmake --list-presets=all`.

**Windows (VST3, x64 Release)**
```bat
cmake --preset windows-x64-vst3-release
cmake --build --preset windows-x64-vst3-release-build
```
The VST3 bundle is written to `build/windows-x64-vst3-release/src/phu-fair-kid-67_artefacts/Release/VST3/`. Other Windows presets: `windows-x64-vst3-debug` and `windows-arm64-vst3-release` (each with `-build`).

**macOS Intel (VST3 + AU, Release)**
```bash
cmake --preset macos-x64-release
cmake --build --preset macos-x64-release-build
```

**macOS Apple Silicon (VST3 + AU, Release)**
```bash
cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release-build
```
Single-format builds on macOS: `macos-arm64-release-build-vst3` / `macos-arm64-release-build-au` (same configure preset). Debug presets: `macos-x64-debug`, `macos-arm64-debug`.

**Linux (VST3, Release)**
```bash
cmake --preset linux-x64-vst3-release
cmake --build --preset linux-x64-vst3-release-build
```
(`linux-arm64-vst3-release` for ARM64.)

**Optional build options and presets**

| Option / preset | Purpose |
|---|---|
| `-DENABLE_STRICT_WARNINGS=ON` | Strict compiler warnings for this project's code (not for fetched dependencies) |
| `--preset sanitizers` / `--preset sanitizers-build` | DSP-only Debug build with Address + Undefined Behavior sanitizers |
| `--preset coverage` / `--preset coverage-build` | DSP-only Debug build with coverage instrumentation |
| `-DPHU_PLUGIN_FORMATS="VST3;AU"` | Plug-in formats to build (default: `VST3;AU` on macOS, `VST3` elsewhere) |

### Option B — Manual CMake Invocation

```bash
cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_PLUGIN=ON
cmake --build build --config Release
```

### Build Without the Plug-in (DSP tests only)

```bash
cmake --preset dsp
cmake --build --preset dsp-build
```

(`dsp-debug` / `dsp-debug-build` for a Debug build.) This builds into `build/dsp/`, skips JUCE (it is not even fetched) and the audio plug-in target; only the DSP libraries, the unit-test executables and `phu_calibrate` are compiled. No platform audio SDKs are required. Equivalent manual invocation: `cmake -B build -DBUILD_PLUGIN=OFF -DBUILD_TESTING=ON`.

---

## 4. Running the Tests

Build with testing enabled (no JUCE required):

```bash
cmake --preset dsp
cmake --build --preset dsp-build
ctest --preset dsp-test
```

(Without presets: `cmake -B build -DBUILD_PLUGIN=OFF -DBUILD_TESTING=ON`, `cmake --build build`, `ctest --test-dir build --output-on-failure`.)

### Test Suites

| Test executable / CTest name | Area covered |
|---|---|
| `phu_fairchild670core_tests` | Stereo core: link modes, timing positions, meter readback |
| `phu_variable_mu_tests` | Variable-mu triode stage NR convergence and gain behaviour |
| `phu_transformer_linear_tests` | Transformer coloration model (HPF + LPF + saturation) |
| `phu_transformer_coupling_tests` | Coupled inductor trapezoidal companion models |
| `phu_circuit_linear_tests` | MNA linear circuit primitives |
| `phu_detector_tests` | Sidechain rectifier/envelope detector |
| `phu_nr_policy_tests` | Newton-Raphson iteration policy |
| `phu_triode_koren_tests` | Koren triode plate-current model |
| `phu_tube_stage_tests` | Common-cathode tube stage circuit |

---

## 5. Project Structure

```
phu-fair-kid-67/
├── src/
│   ├── PluginProcessor.{h,cpp}       # JUCE AudioProcessor (parameters, processBlock)
│   ├── PluginEditor.{h,cpp}          # JUCE AudioProcessorEditor (full custom GUI)
│   └── DSP/
│       ├── DbConversion.h            # dB ↔ linear helpers
│       ├── UnitScaling.h             # normalised samples ↔ volts
│       ├── Circuit/
│       │   ├── Circuit.{h,cpp}       # MNA matrix stamping utilities
│       │   ├── Elements/             # Capacitor / inductor companion models
│       │   └── Nonlinear/
│       │       ├── TriodeKoren.{h,cpp} # Koren triode model + derivatives
│       │       └── NR.{h,cpp}          # Newton-Raphson solver & policy
│       ├── Models/
│       │   ├── TubeStage.{h,cpp}     # Common-cathode tube stage (baseline)
│       │   ├── Transformer/          # TransformerLinear coloration model
│       │   ├── Fairchild/
│       │   │   ├── Fairchild670Core.{h,cpp}  # Stereo compressor core (link modes, meters)
│       │   │   └── VariableMuStage.{h,cpp}   # Variable-mu triode gain stage
│       │   └── Sidechain/
│       │       ├── TimingNetwork.{h,cpp}     # Timing presets & RC coefficients
│       │       └── RectifierDetector.{h,cpp} # Full-wave rectifier envelope detector
│       └── Utils/
│           └── OversamplingChain.{h,cpp}     # JUCE oversampling wrapper
├── docs/
│   ├── USER_GUIDE.md                 # End-user guide (installation, parameters, how it works)
│   ├── BUILDING.md                   # This file — building, testing, contributing
│   ├── TOOLS_AND_SCRIPTS.md          # Developer tools in tools/ and scripts/
│   ├── calibration-workflow.md       # Detailed offline calibration workflow
│   ├── fairchild670-spec-traceability.md
│   ├── performance-analysis.md
│   └── phu-fair-kid-screenshot.png   # GUI screenshot
├── tests/                            # Catch2-based unit tests
├── tools/
│   └── calibrate.cpp                 # Standalone CLI calibration tool (phu_calibrate)
├── scripts/
│   ├── install-linux-deps.sh         # Linux dependency installer
│   ├── plot_timing.py                # Plot attack/release step-response CSV
│   └── plot_transfer.py              # Plot transfer-curve CSV
├── JUCE/                             # Fetched by CMake (FetchContent, git-ignored)
├── phu-audio-lib/                    # Fetched by CMake (FetchContent, git-ignored)
├── cmake/                            # PhuDependencies, compiler_flags, juce_config
├── CMakeLists.txt
└── CMakePresets.json
```

---

## 6. Contributing

Contributions are welcome. Please:

1. Fork the repository and create a feature branch from `main`.
2. Keep new code in C++20 and follow the style of surrounding files (no raw owning pointers, prefer value types and `unique_ptr`).
3. Add or update unit tests in `tests/` for any DSP logic changes.
4. Ensure `ctest` passes locally before opening a pull request.
5. Describe what the change does and, for DSP changes, include a brief note on how you verified correctness (expected level, transfer-function check, etc.).

---

## 7. License

This project is released under the **MIT License**. See [LICENSE](../LICENSE) for the full text.

Third-party components:
- **JUCE** — [ISC / proprietary dual licence](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) (check JUCE's licensing terms for commercial use)
- **phu-audio-lib** — see `phu-audio-lib/LICENSE`
