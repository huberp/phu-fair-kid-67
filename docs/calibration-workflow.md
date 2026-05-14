# Offline Calibration Workflow

This document explains how to build the standalone calibration tool, run
timing and transfer measurements, and generate plots — all without opening
a DAW.

---

## 1. Build the calibration tool

The tool (`phu_calibrate`) is built alongside the DSP unit tests when
`PHU_BUILD_PLUGIN=OFF`.  It has no JUCE dependency.

```bash
# From the repository root:
cmake -B build -DPHU_BUILD_PLUGIN=OFF
cmake --build build

# The binary will be at:
./build/tools/phu_calibrate
```

---

## 2. Measure timing (step-response)

The `--measure-timing` flag feeds a unit step to the sidechain detector and
records the control voltage over time (attack phase), followed by silence
(release phase).  Output is a CSV with columns:

```
sample_index, phase, input_normalised, cv_volts, time_sec
```

### Examples

```bash
# Position 1 (fixed, 0.2 ms attack / 0.30 s release):
./build/tools/phu_calibrate --measure-timing --position 1 > /tmp/p1_timing.csv

# Position 5 (auto-release — programme-dependent):
./build/tools/phu_calibrate --measure-timing --position 5 > /tmp/p5_timing.csv

# Position 6, 48 kHz sample rate:
./build/tools/phu_calibrate --measure-timing --position 6 --sample-rate 48000 \
    --output /tmp/p6_timing_48k.csv
```

### Reading the timing CSV

| Phase     | Meaning |
|-----------|---------|
| `attack`  | Full-scale step input; CV rises from 0 toward final value |
| `release` | Silence applied; CV decays from peak |

For Fixed modes the CV follows a single-exponential curve.
For AutoRelease modes (positions 5 and 6) the release trace shows a
bi-exponential shape: a faster initial decay followed by a slower long tail.

---

## 3. Measure transfer curve

The `--measure-transfer` flag sweeps a 1 kHz sine tone from −60 dBFS to 0 dBFS,
waits for the compressor to settle at each level, then measures RMS input and
output.  Output CSV columns:

```
input_dbfs, output_dbfs, gain_reduction_db, cv_volts
```

Newer calibration runs can also emit extended diagnostics (`raw/effective/applied/stage` CV and clamp ratio),
plus sweep direction tags for up/down hysteresis checks.

### Examples

```bash
# Position 1, threshold = 0 V (always compress):
./build/tools/phu_calibrate --measure-transfer --position 1 > /tmp/p1_transfer.csv

# Position 1, threshold = 3 V (compress only above a moderate level):
./build/tools/phu_calibrate --measure-transfer --position 1 --threshold-ac 3.0 --threshold-dc 0.2 \
    > /tmp/p1_transfer_thr3.csv

# Combine timing and transfer in one run:
./build/tools/phu_calibrate --measure-timing --measure-transfer --position 1 \
    --output /tmp/p1_all.csv

# High-drive + path-check sweep (up and down):
./build/tools/phu_calibrate --measure-transfer --position 1 --threshold-ac 0.5 --threshold-dc 1.8 \
    --transfer-min-dbfs -60 --transfer-max-dbfs 6 --transfer-step-db 3 \
    --transfer-sweep-mode both --transfer-measure-samples 4096 \
    --transfer-settle-multiplier 20 \
    --output /tmp/p1_transfer_hysteresis.csv
```

> **Note on settling time:** For positions with long release time constants
> (especially positions 5 and 6), the transfer measurement uses `10 × τ_release`
> as the settling window.  This makes position 6 measurements slow (~250 s of
> simulated audio per level step).  Use `--sample-rate 8000` for faster
> exploratory measurements at the cost of reduced HF accuracy.

---

## 4. Generate plots

Python 3 with `matplotlib` is required:

```bash
pip install matplotlib
```

### Timing plot

```bash
python3 scripts/plot_timing.py /tmp/p1_timing.csv
# Save to PNG:
python3 scripts/plot_timing.py /tmp/p1_timing.csv --output /tmp/p1_timing.png
```

The plot shows:
- **Left panel**: attack step-response with a 63.2 % reference line.
- **Right panel**: release decay with a 36.8 % reference line.

For AutoRelease positions the release panel will show the characteristic
dual-slope shape.

### Transfer-curve plot

```bash
python3 scripts/plot_transfer.py /tmp/p1_transfer.csv
# With manual-derived reference overlay:
python3 scripts/plot_transfer.py /tmp/p1_transfer.csv \
    --reference calibration/reference/transfer_curve_reference.csv \
    --output /tmp/p1_transfer.png

# Calibration dashboard across all five threshold references:
python3 scripts/plot_transfer.py \
    calibration/reference/transfer_curve_ref_thresh10v0.csv \
    calibration/reference/transfer_curve_ref_thresh3v5.csv \
    calibration/reference/transfer_curve_ref_thresh2v8.csv \
    calibration/reference/transfer_curve_ref_thresh2v0.csv \
    calibration/reference/transfer_curve_ref_thresh0v0.csv \
    --output /tmp/transfer_family_dashboard.png
```

The plot shows:
- **Left panel**: input dBFS vs output dBFS (unity gain diagonal for reference).
- **Right panel**: gain reduction (dB) vs input dBFS.

---

## 5. Recommended settings for reproducible calibration

| Setting | Value | Reason |
|---------|-------|--------|
| Sample rate | 44100 Hz | Matches default plugin rate; all reference values are at this rate |
| Threshold | 0 V | Measures the full compressor curve from silence upwards |
| Position | 1 (for transfer) | Fastest settling; good for curve shape validation |
| Position | 5 or 6 (for auto-release) | Exercises programme-dependent behaviour |
| `--sample-rate` | 8000 for quick checks | Reduces settling time; not suitable for HF accuracy |

---

## 6. Running automated calibration checks (ctest)

The timing conformance and transfer-curve comparison tests run inside `ctest`
and do not require the calibration tool binary or any CSV files:

```bash
# Build and run all tests:
cmake -B build -DPHU_BUILD_PLUGIN=OFF
cmake --build build
cd build && ctest --output-on-failure

# Run only the new conformance tests:
ctest -R "TimingConformance|TransferCurve" --output-on-failure
```

These tests are deterministic and suitable for CI use.

---

## 7. Adding new reference points

To add manual-derived transfer-curve reference points:

1. Edit `tests/transfer_curve_reference.csv`.
2. Add rows in the format `input_dbfs,output_dbfs,gr_db,note`.
3. Rebuild and re-run `ctest`.  If the new points fall outside the current
   ±4 dB tolerance in `TransferCurveTests.cpp`, adjust the tolerance or
   recalibrate the DSP parameters (`cvMaxV`, `Rp`, `Rk`, etc.).

---

## 8. Recalibration guidance

If `TransferCurveTests` fail after adding tighter reference points, the
following parameters are the most likely candidates for adjustment:

| Parameter | File | Effect |
|-----------|------|--------|
| `cvMaxV` | `VariableMuStage.h` | Maximum CV applied to grid; controls depth of limiting |
| `Rp` | `VariableMuStage.h` | Plate resistor; affects gain and headroom |
| `Rk` | `VariableMuStage.h` | Cathode resistor; affects quiescent operating point |
| `kVoltsPerSample` | `UnitScaling.h` | Full-scale voltage mapping |
| detector scaling | `RectifierDetector.cpp` | CV derived from input level |

Make changes to one parameter at a time, re-run `phu_calibrate --measure-transfer`,
compare with the reference using `plot_transfer.py`, and iterate.
