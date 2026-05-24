# Fairchild 670 — New Calibration Approach Plan

## Objective
Calibrate the model against the 5 manual transfer curves **without adding plugin-side fitting logic** and without curve-by-curve guesswork.

The 5 curves are treated as **fixed sidechain trim states** (AC/DC threshold board settings), while a small set of **global physical model parameters** is fitted.

---

## 1) Circuit depiction used for calibration

```text
INPUT
  │
  ├─ P3 Input Transformer (HPF 30 Hz, LPF 30 kHz, drive 1.2)
  │      │
  │      ├─ Audio path:
  │      │    P7 Preamp tube (12AU7)
  │      │      -> P2 Variable-mu push-pull (6386 pair)
  │      │      -> P5 Interstage Transformer (HPF 25 Hz, LPF 22 kHz, drive 1.1)
  │      │      -> P6 Output Transformer (2nd-order HPF+LPF, tanh sat)
  │      │      -> OUTPUT
  │      │
  │      └─ Sidechain path:
  │           P8 Tube chain: 12AX7 -> 12AX7 -> 12BH7 (CV=0, nonlinear limiter)
  │           -> 6AL5 soft rectifier (Vf ≈ 0.8 V)
  │           -> timing network (positions 1..6)
  │           -> AC threshold trim + DC bias trim
  │           -> CV soft-knee + CV ceiling
  │           -> control grids of 6386 stage
```

### Key component/model parameters to keep explicit in calibration

- **Main variable-mu stage (P2) defaults**
  - Tube model: 6386 approximation (`mu=70`, `kp=300`, `kvb=300`, `kg1=700`, `x=1.0`)
  - `Vcc = 250 V`, `Rp = 100 kΩ`, `Rk = 1.5 kΩ`, `Ck = 0..47 µF`
  - `cvMaxV = 9.0 V`
- **Sidechain detector path**
  - `tubeRectifierForwardVoltageV = 0.8 V`
  - `sidechainAmplifierGain = 0.7`
  - `sidechainCvSoftKneeV = 0.75 V`
- **Transformers**
  - Input: HPF 30 Hz / LPF 30 kHz / drive 1.2
  - Interstage: HPF 25 Hz / LPF 22 kHz / drive 1.1
  - Output: 2nd-order HPF+LPF (`Q≈0.707`) + saturation

---

## 2) Fixed vs fitted quantities

## Fixed (anchors)
The 5 reference curves represent **fixed hardware trim states** of sidechain AC/DC thresholds and must remain fixed during final fitting:

- Curve 1: straight amplifier (no compression)
- Curve 2: light compression, high DC trim
- Curve 3: factory nominal trim
- Curve 4: maximum compression region
- Curve 5: heavy AC / low DC

Important: AC/DC trim values should be established once as a locked table (`curve_id -> ac_threshold_v, dc_bias_v`) and then treated as constants for all further calibration runs.

## Fitted (global candidates)
Fit only parameters that are physically global across all curves:

1. `sidechainAmplifierGain`
2. `tubeRectifierForwardVoltageV`
3. `sidechainCvSoftKneeV`
4. `stageCfg.cvMaxV`
5. Main-stage electrical operating point: `Rp`, `Rk`, optionally `Ck` (if static-curve residual slope indicates cathode feedback mismatch)
6. Tube law sensitivity (small deltas only):
   - 6386 fit (`kg1`, optionally `mu`)
   - Sidechain tube fit scale (12AX7/12BH7 effective gain factor if needed)

Do **not** fit per-curve ad-hoc plugin behaviors.

---

## 3) Could small tube-setting deviations explain the mismatch?

Yes, partially.

Small tube parameter/bias deviations can materially shift transfer-curve knee and GR depth, especially:
- 6386 transconductance sensitivity (`kg1`, bias point via `Rp/Rk`)
- sidechain chain effective gain/compression before rectifier
- rectifier onset (`Vf`) near knee region

But they must be solved as **global shared parameters across all 5 curves**, not per-curve free knobs.

---

## 4) Required approach changes (refinement)

1. **Lock AC/DC trim table first**
   - Derive a single fixed AC/DC table for curves 1..5.
   - Store it under `calibration/outputs/locked_threshold_table.json`.
   - Use monotonic constraints consistent with hardware intent.

2. **Run one global multi-curve objective**
   - Objective = weighted error across all 5 curves simultaneously.
   - Same global parameter vector for all curves.
   - No per-curve re-fit of global physics.

3. **Constrain fit ranges to physical tolerances**
   - Resistances/capacitances/tube constants allowed only in realistic tolerance bands.
   - Reject solutions that need implausible parameter jumps.

4. **Separate calibration layers**
   - Layer A: electrical scale alignment (dBFS↔dBm, straight-line curve)
   - Layer B: sidechain threshold table lock
   - Layer C: global physical fit
   - Layer D: validation and freeze defaults

5. **Freeze and document**
   - Commit locked thresholds + fitted globals + residual metrics.
   - Update defaults only from this locked result set.

---

## 5) Acceptance criteria (no guesswork)

- All 5 curves evaluated together with one global parameter set.
- AC/DC table unchanged during global fit.
- RMS/Max error per curve reported and versioned.
- Fitted parameters remain inside declared physical tolerance bounds.
- No new plugin runtime code that emulates calibration by special-case behavior.

---

## 6) Files and outputs

- Plan file: `calibration/New-Calibration-Approach-Plan.md`
- Locked trim mapping: `calibration/outputs/locked_threshold_table.json`
- Locked global fit: `calibration/outputs/locked_global_parameters.json`
- Curve comparison metrics/plots: existing calibration output pipeline

This keeps calibration traceable, physics-based, and reproducible.
