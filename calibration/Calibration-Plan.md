# Fairchild 670 Calibration Plan

## Goal

Hardware-grounded calibration of the plugin transfer curve against 5 reference
curves hand-digitized from the Fairchild 670 service manual.  
The previous protocol compared the plugin against its own output — circular.  
This plan replaces it with a real ground-truth comparison in dBm.

---

## Level Conversion (K Offset)

    dBm = dBFS + K        where  K = 19.2 dBm

Derivation:  
0 dBFS = ±10 V peak → 7.071 V RMS into 600 Ω = 83.3 mW = **+19.2 dBm**.

---

## Reference Curves

Stored in `calibration/reference/` as `Transfere-Curve-N.csv` (N = 1–5).  
Format: European CSV — semicolon separator, decimal comma (e.g. `-9,5; -10`).  
Values are in **dBm**.

| Curve | Description                    | AC initial (V) | DC initial (V) |
|-------|--------------------------------|----------------|----------------|
| 1     | Linear — straight amp, no GR   | 10.0           | 0.0            |
| 2     | Light compression, DC fully CW | 8.5            | 4.5            |
| 3     | Factory operating condition    | 5.0            | 1.5            |
| 4     | Max compression, DC near-max   | 0.5            | 4.0            |
| 5     | Heavy AC, minimal DC           | 0.5            | 0.5            |

*"AC initial" = starting estimate for `--threshold-ac` (V).*  
*"DC initial" = starting estimate for `--threshold-dc` (V).*

---

## Calibration Sweep Range

    phu_calibrate --measure-transfer \
        --transfer-min-dbfs -26 \
        --transfer-max-dbfs  +9 \
        --transfer-step-db   0.5

`--transfer-min/max-dbfs` are **peak** levels; phu_calibrate stores RMS in the
CSV (sine peak−to−RMS = 3.01 dB).  With K = 19.2 dBm, `input_dbm = input_dbfs_rms + 19.2`.

| Boundary | Peak dBFS | RMS dBFS | dBm |
|----------|-----------|----------|-----|
| Sweep min | −26 | −29.0 | −9.8 |
| Sweep max | +9  | +5.99  | +25.2 |

This range covers the full extent of the hand-digitized reference curves (~−9.5 to +25 dBm).

---

## Phases

### Phase 1 — Verify K

1. Run phu_calibrate with threshold high (AC=10 V, DC=0 V) so no compression occurs.
2. Convert output_dBFS → output_dBm using K = 19.2.
3. Overlay against `Transfere-Curve-1.csv` (linear reference).
4. Accept if RMS error < 0.2 dB across sweep range.

If K is off, adjust until the straight-amp output tracks Curve-1.

### Phase 2 — Create Tooling

Scripts to automate the measurement → compare loop:

- `calibration/scripts/compare_to_reference.py`  
  Loads a plugin sweep CSV and a reference curve CSV, converts to dBm, computes
  RMS/max/mean error, writes a plot and JSON stats.

- `calibration/scripts/run_curve_family.ps1`  
  Runs phu_calibrate for each of the 5 curves with its initial AC/DC estimates,
  calls compare_to_reference.py for each, and writes results under
  `calibration/outputs/curve_family/`.

### Phase 3 — Expose DC Bias Parameters

Add `dcBiasLeft` / `dcBiasRight` APVTS parameters to the plugin:

- Range: 0.0 – 10.0 V, step 0.01 V, default 0.0 V.
- Plugin ID strings: `"dcBiasLeft"`, `"dcBiasRight"`.
- Core setters: `Fairchild670Core::setDcBiasLeft()` / `setDcBiasRight()`.

Without this step phu_calibrate's `--threshold-dc` flag has no corresponding
plugin state that a DAW can automate or recall.

### Phase 4 — First-Pass Measurements

Run `run_curve_family.ps1` with the initial AC/DC estimates from the table above.  
Examine overlays in `calibration/outputs/curve_family/`.  
Identify which curves have large errors and need parameter adjustment.

### Phase 5 — Parameter Fitting

`calibration/scripts/fit_ac_dc.py`:

- Uses `scipy.optimize.minimize` (Nelder-Mead, unconstrained).
- For each curve, the objective is: *RMS(plugin_output_dBm − reference_output_dBm)*.
- Invokes phu_calibrate as a subprocess with candidate (AC, DC) values.
- Writes fitted parameters to `calibration/outputs/fitted_parameters.json`.

### Phase 6 — Validation & Lock-In

1. Re-run `run_curve_family.ps1` with fitted parameters; verify RMS error < 0.5 dB.
2. Update default values in `Fairchild670CoreConfig`:
   - `sidechainAmplifierGain`, `sidechainCvSoftKneeV`, `cvMaxV` if needed.
3. Commit `calibration/outputs/locked_parameters.json` with fitted AC/DC values
   and achieved RMS errors.
4. Tag as calibration checkpoint on the branch.

---

## File Map

| Path | Purpose |
|------|---------|
| `calibration/reference/Transfere-Curve-N.csv` | Ground-truth curves (keep) |
| `calibration/plots/curve_1_5_transfer.png` | Visual inspection reference (keep) |
| `scripts/plot_transfer.py` | Plot CSVs (keep, extended for xy_reference format) |
| `calibration/scripts/compare_to_reference.py` | Phase 2 tooling |
| `calibration/scripts/run_curve_family.ps1` | Phase 2 batch runner |
| `calibration/scripts/fit_ac_dc.py` | Phase 5 optimizer |
| `calibration/outputs/curve_family/` | Per-curve comparison outputs |
| `calibration/outputs/fitted_parameters.json` | Phase 5/6 result |
| `calibration/outputs/locked_parameters.json` | Phase 6 locked values |
