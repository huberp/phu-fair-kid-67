# Fairchild 670 Compressor — Calibration Plan

**Goal:** Calibrate the compressor so its steady-state transfer curves (measured in dBFS by `phu_calibrate`) match the five reference curves from the original Fairchild 670 service manual (`calibration/reference/Curve-1.csv` – Curve-5.csv, values in **dBm at 600 Ω**).

---

## 1. Background

### 1.1 The Five Reference Curves

| File | Hardware condition | AC Threshold pot | DC Threshold pot |
|---|---|---|---|
| Curve-1.csv | Straight amplifier — no compression | Fully CCW (~10 V) | Irrelevant |
| Curve-2.csv | Light programme compression | Slightly CW from CCW (~8–9 V) | Fully CW (max DC floor) |
| Curve-3.csv | Factory-adjusted condition | Mid-range calibrated | Mid-range calibrated |
| Curve-4.csv | Maximum programme compression | Fully CW (~0 V) | Slightly CCW from CW (near-max DC) |
| Curve-5.csv | Heavy AC, light DC | Fully CW (~0 V) | Slightly CW from CCW (near-min DC) |

Format: `input_dBm; output_dBm` (semicolon-separated, European decimal comma).

### 1.2 Unit Conversion: dBm ↔ dBFS

The plugin maps ±1.0 FS to ±10 V peak at 600 Ω. Therefore:

$$K = 10 \times \log_{10}\!\left(\frac{(10/\sqrt{2})^2}{600 \times 10^{-3}}\right) = +19.2 \;\text{dBm}$$
$$\text{dBm} = \text{dBFS} + 19.2$$

**Practical consequence:** the manual's curve range (−10 to +10 dBm) maps to −29 to −9 dBFS in the plugin. All calibration sweeps must use `--transfer-min-dbfs -35 --transfer-max-dbfs -5 --transfer-step-db 0.5`.

*K must be verified empirically in Phase 1.*

### 1.3 CV Signal Path (internal, simplified)

```
rawCv        = softRectifier(input)                     // 6AL5 dead-zone
effectiveCv  = max(0, rawCv − acThreshold) + dcBias     // ← both hardware pots
appliedCv    = effectiveCv × sidechainAmplifierGain      // global scalar
stageCv      = softKnee(appliedCv, cvMaxV, cvSoftKneeV) // shape
GR           = f(stageCv, 6386 Koren model)
```

---

## 2. Parameter Inventory

### 2.1 Currently Exposed (APVTS front panel)

| ID | Internal target | Calibration role |
|---|---|---|
| `thresholdLeft/Right` | `acThresholdVoltageL/R` via `10 − param` | **Primary onset control** |
| `inputTrimLeft/Right` | pre-gain offset dB | Level alignment |
| `outputTrimLeft/Right` | post-gain offset dB | Level alignment |

### 2.2 Currently Internal — NOT Exposed (critical gap)

| Field | Setter | Default | Why it matters |
|---|---|---|---|
| `dcBiasVoltageL/R` | `setDcBiasLeft/Right()` | **0.0 V** | Models **DC Threshold** pot. Required for Curves 2 & 4. |
| `sidechainAmplifierGain` | `Fairchild670CoreConfig` field | 0.7 | Shapes global GR gain law across all curves |
| `sidechainCvSoftKneeV` | `Fairchild670CoreConfig` field | 0.75 V | GR ceiling rounding |
| `cvMaxV` (`stageCfg.cvMaxV`) | `Fairchild670CoreConfig` / `makeStageCfg6386()` | 9.0 V | Maximum GR ceiling |
| `tubeRectifierForwardVoltageV` | `Fairchild670CoreConfig` field | 0.8 V | 6AL5 dead zone / minimum sidechain drive |

### 2.3 `phu_calibrate` Tool Coverage

Already accepts (no code changes needed to the tool):

| Flag | Internal call |
|---|---|
| `--threshold-ac <V>` | `setAcThresholdLeft/Right()` |
| `--threshold-dc <V>` | `setDcBiasLeft/Right()` |
| `--sidechain-gain <f>` | `cfg.sidechainAmplifierGain` |
| `--cv-soft-knee <V>` | `cfg.sidechainCvSoftKneeV` |
| `--cv-max <V>` | `cfg.stageCfg.cvMaxV` |

---

## 3. What Must Be Built

| Item | Priority | Phase |
|---|---|---|
| Expose `dcBiasLeft` / `dcBiasRight` as APVTS parameters in `PluginProcessor.cpp` | **High** | 3 |
| `calibration/scripts/compare_to_reference.py` — convert plugin CSV (dBFS) to dBm, overlay against reference, compute RMS error | **High** | 2 |
| `calibration/scripts/run_curve_family.ps1` — run all 5 curves with a given AC/DC table and call compare script | **High** | 2 |
| `calibration/scripts/fit_ac_dc.py` — scipy-based optimiser for (AC, DC) per curve | Medium | 5 |
| `calibration/outputs/locked_parameters.json` — committed record of final fitted values | Medium | 6 |

---

## 4. Phases

### Phase 1 — Establish Level Reference (K)
1. Run `phu_calibrate --threshold-ac 10.0 --threshold-dc 0.0 --transfer-min-dbfs -35 --transfer-max-dbfs -5 --transfer-step-db 0.5 --output calibration/outputs/straight_amp_verify.csv`
2. Convert output: `output_dBm = output_dBFS + 19.2`
3. Compare against Curve-1.csv. **Pass criterion:** slope = 1.0 ±0.05, y-intercept = −0.5 dBm ±1 dB
4. If intercept differs by >1 dB, correct K accordingly. All subsequent phases use the verified K.

### Phase 2 — Build Comparison and Run Scripts *(parallel with Phase 3)*

#### `calibration/scripts/compare_to_reference.py`
- `--plugin-csv`, `--reference-csv`, `--k-offset` (default 19.2), `--output-png`, `--output-stats`
- Parses plugin CSV using `load_transfer_csv()` from plot_transfer.py
- Parses reference CSV using the same semicolon/decimal-comma parser already in plot_transfer.py
- Interpolates reference at plugin input levels with `numpy.interp`
- Outputs: overlay plot (reference = solid, plugin = dashed, both in dBm) + residual panel + JSON stats (rms_error_db, max_error_db)

#### `calibration/scripts/run_curve_family.ps1`
- Accepts AC/DC table per curve, calls `phu_calibrate` for each, then calls `compare_to_reference.py`
- Encodes initial AC/DC estimates from Phase 4

### Phase 3 — Expose DC Bias as Plugin Parameter *(parallel with Phase 2)*

**PluginProcessor.cpp — `createParameterLayout()`:** add:

| ID | Range | Default |
|---|---|---|
| `dcBiasLeft` | 0.0 – 5.0 V | 0.0 V |
| `dcBiasRight` | 0.0 – 5.0 V | 0.0 V |

**PluginProcessor.cpp — `processBlock()`:** call `core_.setDcBiasLeft/Right()` with the new param values.

No changes to `Fairchild670Core` — the setters already exist.

### Phase 4 — First-Pass Measurements

Initial AC/DC hypothesis per curve:

| Curve | AC (V) | DC (V) | Hardware rationale |
|---|---|---|---|
| 1 | 10.0 | 0.0 | Fully CCW AC = max threshold = no sidechain drive |
| 2 | 8.5 | 4.5 | Light AC drive, full DC floor |
| 3 | 5.0 | 1.5 | Factory balanced condition |
| 4 | 0.5 | 4.0 | Max AC drive, near-max DC floor |
| 5 | 0.5 | 0.5 | Max AC drive, minimal DC |

Run `run_curve_family.ps1` with these values and inspect the comparison plots.

### Phase 5 — Parameter Fitting

Two-level optimisation:

**Level A (global):** grid search over `sidechainAmplifierGain` ∈ {0.4…0.9}, `sidechainCvSoftKneeV` ∈ {0.25…1.0}, `cvMaxV` ∈ {6.0…10.0}. For each combination, fit per-curve (AC, DC) for curves 3–5 and record total RMS.

**Level B (per-curve):** `fit_ac_dc.py` uses `scipy.optimize.minimize` (Nelder-Mead or bounded Powell), calling `phu_calibrate` as a subprocess, comparing output to reference CSV, minimising RMS(output_dBm − reference_dBm). Seed from Phase 4 initial estimates; use multiple random starts to avoid local minima.

### Phase 6 — Validation and Lock-In

**Acceptance criteria:**

| Metric | Threshold |
|---|---|
| RMS output error per curve | < 0.5 dBm |
| Max point error per curve | < 1.5 dBm |
| Curve-4 GR > Curve-5 GR at all inputs ≥ 0 dBm | ΔGR ≥ 1 dB |
| Curve-2 GR > Curve-1 GR at inputs ≥ 0 dBm | ΔGR ≥ 0.5 dB |

**Lock-in steps:**
1. Update `Fairchild670CoreConfig` defaults in Fairchild670Core.h (`sidechainAmplifierGain`, `sidechainCvSoftKneeV`, `makeStageCfg6386()` → `cvMaxV`)
2. Update `thresholdLeft/Right` and `dcBiasLeft/Right` defaults in `createParameterLayout()` to match the Curve-3 factory condition fitted values
3. Commit `calibration/outputs/locked_parameters.json` with all fitted values

---

## 5. Dependency Order

```
Phase 1  ── verify K (standalone, blocking for all others)
Phase 2  ── build scripts (parallel with 3, depends on Phase 1 for level range)
Phase 3  ── expose DC bias param (parallel with 2, standalone C++ change)
Phase 4  ── first-pass runs (depends on 1, 2)
Phase 5  ── fitting (depends on 4)
Phase 6  ── lock-in (depends on 5)
```

---

## 6. Files Modified / Created

| File | Action | Phase |
|---|---|---|
| calibration/scripts/compare_to_reference.py | **Create** | 2 |
| calibration/scripts/run_curve_family.ps1 | **Create** | 2 |
| calibration/scripts/fit_ac_dc.py | **Create** | 5 |
| `calibration/outputs/locked_parameters.json` | **Create** | 6 |
| PluginProcessor.cpp | **Modify**: `dcBiasLeft`/`dcBiasRight` params | 3 |
| Fairchild670Core.h | **Modify**: update defaults post-lock-in | 6 |
| ManualReferencePlot.md | **Update**: add fitted AC/DC table | 6 |

calibrate.cpp — **no changes needed** (already accepts `--threshold-ac` and `--threshold-dc`).

---

## 7. Risks

| Risk | Mitigation |
|---|---|
| K = 19.2 is wrong | Phase 1 verifies empirically before anything depends on it |
| DC range 0–5 V too narrow for Curve-2 | Widen to 0–10 V if data shows DC > 5 V needed |
| Reference CSVs only cover −10 to +9 dBm | Mark higher-level behaviour as uncalibrated extrapolation |
| `sidechainAmplifierGain` and `cvMaxV` are correlated | Fix one, optimise the other; or constrained optimisation |
| Local minima in Phase 5 fitting | Multiple random starts seeded from Phase 4 estimates |