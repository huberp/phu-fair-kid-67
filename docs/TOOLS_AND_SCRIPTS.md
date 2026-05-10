# PHU FAIR KID 67 — Developer Tools & Scripts

This document describes the standalone developer tools in `tools/` and the helper scripts in `scripts/`. These are not needed for normal plug-in use; they exist to support offline DSP verification, calibration, and visualisation.

---

## Table of Contents

1. [tools/calibrate.cpp — Offline Calibration Tool (`phu_calibrate`)](#1-toolscalibratecpp--offline-calibration-tool-phu_calibrate)
2. [scripts/plot_timing.py — Attack/Release Step-Response Plotter](#2-scriptsplot_timingpy--attackrelease-step-response-plotter)
3. [scripts/plot_transfer.py — Transfer-Curve Plotter](#3-scriptsplot_transferpy--transfer-curve-plotter)
4. [scripts/install-linux-deps.sh — Linux Dependency Installer](#4-scriptsinstall-linux-depssh--linux-dependency-installer)
5. [End-to-End Workflow Example](#5-end-to-end-workflow-example)
6. [Further Reading](#6-further-reading)

---

## 1. tools/calibrate.cpp — Offline Calibration Tool (`phu_calibrate`)

### What it does

`phu_calibrate` is a standalone command-line tool that exercises `Fairchild670Core` — the same DSP core used by the plug-in — without a DAW or JUCE audio thread. It has two measurement modes:

| Mode | Flag | Description |
|---|---|---|
| **Timing** | `--measure-timing` | Feeds a unit step to the sidechain detector and records the control voltage (CV) over time (attack phase), then feeds silence and records the release phase. |
| **Transfer** | `--measure-transfer` | Sweeps a 1 kHz sine tone from −60 dBFS to 0 dBFS, waits for the compressor to settle at each level, and measures RMS input and output to produce a steady-state transfer curve. |

Both modes write CSV to stdout or a file. The tool is fully deterministic and suitable for CI comparisons.

### Building

The tool is built automatically when `PHU_BUILD_PLUGIN=OFF` (alongside the unit tests). No JUCE dependency is required.

```bash
cmake -B build -DPHU_BUILD_PLUGIN=OFF
cmake --build build
# Binary is placed at:
./build/tools/phu_calibrate
```

### Usage

```
phu_calibrate --measure-timing   --position <1-6> [--sample-rate <Hz>]
phu_calibrate --measure-transfer              [--position <1-6>] [--sample-rate <Hz>]
phu_calibrate --measure-timing --measure-transfer --position <1-6> [--output <file>]
```

#### Options

| Option | Default | Description |
|---|---|---|
| `--measure-timing` | — | Enable attack/release step-response measurement |
| `--measure-transfer` | — | Enable steady-state transfer-curve measurement |
| `--position <1-6>` | 1 | Timing position to use |
| `--sample-rate <Hz>` | 44100 | Sample rate for simulation |
| `--output <file>` | stdout | Write CSV to a file instead of stdout |
| `--help` | — | Show help and exit |

#### Examples

```bash
# Timing step-response for position 1 (fixed, 0.2 ms attack / 0.30 s release):
./build/tools/phu_calibrate --measure-timing --position 1 > /tmp/p1_timing.csv

# Timing for position 5 (auto-release, programme-dependent):
./build/tools/phu_calibrate --measure-timing --position 5 > /tmp/p5_timing.csv

# Transfer curve for position 1:
./build/tools/phu_calibrate --measure-transfer --position 1 > /tmp/p1_transfer.csv

# Both measurements at once, written to a file:
./build/tools/phu_calibrate --measure-timing --measure-transfer --position 6 \
    --output /tmp/p6_all.csv
```

### Timing CSV format

```
# position=1 kind=Fixed attack_sec=0.0002 release_sec=0.3
sample_index, phase, input_normalised, cv_volts, time_sec
0, attack, 1.0, 0.0012, 0.0000
...
```

| Column | Description |
|---|---|
| `sample_index` | Zero-based sample counter |
| `phase` | `attack` (step-on) or `release` (step-off) |
| `input_normalised` | Input amplitude (1.0 = full scale, 0.0 = silence) |
| `cv_volts` | Control voltage at the output of the RC envelope follower (0–6 V) |
| `time_sec` | Elapsed time in seconds |

For **Fixed** timing modes (positions 1–4) the CV follows a single exponential curve. For **AutoRelease** modes (positions 5–6) the release shows a characteristic bi-exponential shape: a faster initial decay followed by a slower long tail.

### Transfer-curve CSV format

```
# position=1 kind=Fixed
input_dbfs, output_dbfs, gain_reduction_db, cv_volts
-60.0, -60.0, 0.0, 0.001
...
```

> **Note on settling time:** Positions with long release time constants (especially 5 and 6) require `10 × τ_release` as the settling window, which makes position 6 measurements slow (~250 s of simulated audio per level step). Use `--sample-rate 8000` for faster exploratory runs.

---

## 2. scripts/plot_timing.py — Attack/Release Step-Response Plotter

### What it does

Reads a timing CSV produced by `phu_calibrate --measure-timing` and generates a two-panel plot:

- **Left panel** — attack step-response with a 63.2 % (one time-constant) reference line.
- **Right panel** — release decay with a 36.8 % reference line. For AutoRelease positions the characteristic dual-slope shape is visible here.

### Requirements

```bash
pip install matplotlib
```

### Usage

```bash
python3 scripts/plot_timing.py <timing_csv> [--output <image.png>]
```

If `--output` is omitted the plot is shown interactively.

### Examples

```bash
# Interactive display:
python3 scripts/plot_timing.py /tmp/p1_timing.csv

# Save to PNG:
python3 scripts/plot_timing.py /tmp/p1_timing.csv --output /tmp/p1_timing.png

# Full pipeline (generate data and plot in one go):
./build/tools/phu_calibrate --measure-timing --position 5 > /tmp/p5_timing.csv
python3 scripts/plot_timing.py /tmp/p5_timing.csv --output /tmp/p5_timing.png
```

---

## 3. scripts/plot_transfer.py — Transfer-Curve Plotter

### What it does

Reads a transfer-curve CSV produced by `phu_calibrate --measure-transfer` and generates a two-panel plot:

- **Left panel** — input dBFS vs output dBFS with a unity-gain diagonal reference.
- **Right panel** — gain reduction (dB) vs input dBFS.

An optional manually-derived reference CSV can be overlaid on both panels for visual comparison against expected hardware behaviour.

### Requirements

```bash
pip install matplotlib
```

### Usage

```bash
python3 scripts/plot_transfer.py <transfer_csv> \
    [--reference <ref_csv>] \
    [--output <image.png>]
```

### Examples

```bash
# Interactive display:
python3 scripts/plot_transfer.py /tmp/p1_transfer.csv

# With manual reference overlay:
python3 scripts/plot_transfer.py /tmp/p1_transfer.csv \
    --reference tests/transfer_curve_reference.csv

# Save to PNG:
python3 scripts/plot_transfer.py /tmp/p1_transfer.csv \
    --reference tests/transfer_curve_reference.csv \
    --output /tmp/p1_transfer.png
```

### Reference CSV format

The reference CSV uses the same column layout as the tool output:

```
input_dbfs, output_dbfs, gr_db, note
-30.0, -30.0, 0.0, no compression
-6.0, -8.5, 2.5, light compression
```

The `note` column is ignored by the plotter but useful for documentation. Reference points are also used by `TransferCurveTests` (run via `ctest`) for automated regression checking.

---

## 4. scripts/install-linux-deps.sh — Linux Dependency Installer

### What it does

Installs all apt packages required to build the plug-in (including the JUCE GUI and audio backends) on Ubuntu/Debian-based distributions.

### Usage

```bash
sudo ./scripts/install-linux-deps.sh
```

### What it installs

| Group | Packages |
|---|---|
| Build tools | `build-essential`, `cmake`, `ninja-build` |
| Audio | `libasound2-dev`, `libjack-jackd2-dev` |
| Graphics | `libfreetype6-dev`, `libfontconfig1-dev` |
| GUI (X11) | `libx11-dev`, `libxcomposite-dev`, `libxcursor-dev`, `libxext-dev`, `libxinerama-dev`, `libxrandr-dev`, `libxrender-dev` |
| OpenGL | `libgl1-mesa-dev` |

Only the minimal set of dependencies needed for this plug-in is installed. Optional JUCE features (WebKit, curl, LADSPA) are disabled via CMake flags and their packages are not required.

After running the script, build with:

```bash
cmake --preset linux-release
cmake --build --preset linux-build
```

---

## 5. End-to-End Workflow Example

The following example generates and inspects the timing and transfer characteristics for position 1 from scratch:

```bash
# 1. Build the tools (no JUCE required):
cmake -B build -DPHU_BUILD_PLUGIN=OFF
cmake --build build

# 2. Measure timing step-response:
./build/tools/phu_calibrate --measure-timing --position 1 > /tmp/p1_timing.csv

# 3. Measure transfer curve:
./build/tools/phu_calibrate --measure-transfer --position 1 > /tmp/p1_transfer.csv

# 4. Plot results:
pip install matplotlib
python3 scripts/plot_timing.py   /tmp/p1_timing.csv   --output /tmp/p1_timing.png
python3 scripts/plot_transfer.py /tmp/p1_transfer.csv \
    --reference tests/transfer_curve_reference.csv \
    --output /tmp/p1_transfer.png

# 5. Run automated conformance tests:
cd build && ctest --output-on-failure
```

---

## 6. Further Reading

- **[docs/calibration-workflow.md](calibration-workflow.md)** — Detailed calibration workflow: measuring all six timing positions, recommended settings for reproducible results, adding new reference points, and recalibration guidance when DSP parameters change.
- **[docs/BUILDING.md](BUILDING.md)** — Full build and test instructions including CMake presets, platform-specific requirements, and the complete unit-test suite.
- **[docs/fairchild670-spec-traceability.md](fairchild670-spec-traceability.md)** — Traceability from original Fairchild 670 hardware specifications to DSP model parameters.
- **[docs/performance-analysis.md](performance-analysis.md)** — CPU and latency performance analysis.
