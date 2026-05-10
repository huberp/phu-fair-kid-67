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
| **CMake** | 3.15 | 3.23+ recommended (required for CMake Presets) |
| **C++ compiler** | C++17 | See platform-specific requirements below |
| **Git** | Any recent | Needed for submodule checkout |

### Windows

| Tool | Notes |
|---|---|
| **Visual Studio 2022 or later** | Desktop C++ workload required |
| **Windows SDK** | Included with Visual Studio |

> The `CMakePresets.json` file includes a `vs2026-x64` preset. If you are on Visual Studio 2022, use `cmake -G "Visual Studio 17 2022"` instead.

### macOS

| Tool | Notes |
|---|---|
| **Xcode 13 or later** | Command Line Tools are not sufficient; the full Xcode app is required for AU builds |
| **macOS 11 SDK or later** | Included with Xcode |

### Linux

| Package | Notes |
|---|---|
| `build-essential` / `gcc` / `clang` | C++17-capable compiler |
| JUCE system dependencies | See [JUCE Linux dependencies](https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md) — typically: `libasound2-dev libfreetype6-dev libx11-dev libxinerama-dev libxrandr-dev libxcursor-dev mesa-common-dev` |

A convenience script that installs all required Linux packages is available at `scripts/install-linux-deps.sh`:

```bash
sudo ./scripts/install-linux-deps.sh
```

---

## 2. Cloning the Repository

The project uses Git submodules for JUCE and `phu-audio-lib`. Always clone recursively:

```bash
git clone --recurse-submodules https://github.com/huberp/phu-fair-kid-67.git
cd phu-fair-kid-67
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

---

## 3. Build Instructions

### Option A — Using CMake Presets (recommended)

Presets are defined in `CMakePresets.json`.

**Windows (VST3, x64 Release)**
```bat
cmake --preset vs2026-x64
cmake --build --preset release
```
The VST3 bundle is written to `build/vs2026-x64/src/phu-fair-kid-67_artefacts/Release/VST3/`.

**macOS Intel (VST3 + AU, Release)**
```bash
cmake --preset macos-x86_64-release
cmake --build --preset macos-x86_64-build
```

**macOS Apple Silicon (VST3 + AU, Release)**
```bash
cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-build
```

**Linux (VST3, Release)**
```bash
cmake --preset linux-release
cmake --build --preset linux-build
```

### Option B — Manual CMake Invocation

```bash
cmake -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DPHU_BUILD_PLUGIN=ON
cmake --build build --config Release
```

### Build Without the Plug-in (DSP tests only)

```bash
cmake -B build -DPHU_BUILD_PLUGIN=OFF
cmake --build build
```

This skips JUCE and the audio plug-in target; only the unit-test executables are compiled. No platform audio SDKs are required.

---

## 4. Running the Tests

After building (with or without the plug-in):

```bash
cd build
ctest --output-on-failure
```

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
├── JUCE/                             # Git submodule — JUCE framework
├── phu-audio-lib/                    # Git submodule — shared audio utilities
├── CMakeLists.txt
└── CMakePresets.json
```

---

## 6. Contributing

Contributions are welcome. Please:

1. Fork the repository and create a feature branch from `main`.
2. Keep new code in C++17 and follow the style of surrounding files (no raw owning pointers, prefer value types and `unique_ptr`).
3. Add or update unit tests in `tests/` for any DSP logic changes.
4. Ensure `ctest` passes locally before opening a pull request.
5. Describe what the change does and, for DSP changes, include a brief note on how you verified correctness (expected level, transfer-function check, etc.).

---

## 7. License

This project is released under the **MIT License**. See [LICENSE](../LICENSE) for the full text.

Third-party components:
- **JUCE** — [ISC / proprietary dual licence](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) (check JUCE's licensing terms for commercial use)
- **phu-audio-lib** — see `phu-audio-lib/LICENSE`
