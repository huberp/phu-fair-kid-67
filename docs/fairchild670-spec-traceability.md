# Fairchild 670 Spec Traceability

This document maps the Fairchild 670 manual (see [Fairchild Model 670 Manual](https://fairchildusa.com/wp-content/uploads/2023/12/Fairchild-Model-670-Manual.pdf)) to the current plugin implementation.  Each behaviour is classified as one of:

| Status | Meaning |
|---|---|
| **exact** | Directly derived from the manual; mathematically faithful |
| **approximate** | Captures the intent but with simplified or estimated values |
| **not implemented** | Feature described in the manual that is absent from the code |
| **modern extension** | Feature not in the original hardware; added for usability |
| **unknown** | Behaviour not clearly specified in the manual |

---

## 1. Time-constant switch (timing positions 1–6)

### Manual description

The Fairchild 670 has a six-position rotary switch that selects combinations of
attack and release capacitor/resistor values in the sidechain RC network.
Positions 1–4 select fixed attack/release time constants.
**Positions 5 and 6 are automatic / programme-dependent release modes**: the
release time adapts to programme material so that brief transients recover
quickly, while sustained loud passages hold the gain reduction for longer.

The manual indicates attack is very fast and roughly constant across all
positions (~0.2 ms).

### Manual-derived target values

| Position | Kind        | Attack   | Release (fixed) or [fast / slow] (auto) |
|----------|-------------|----------|-----------------------------------------|
| 1        | Fixed       | 0.2 ms   | 0.30 s |
| 2        | Fixed       | 0.2 ms   | 0.80 s |
| 3        | Fixed       | 0.2 ms   | 2.00 s |
| 4        | Fixed       | 0.2 ms   | 5.00 s |
| 5        | AutoRelease | 0.2 ms   | fast 0.50 s / slow 10.0 s |
| 6        | AutoRelease | 0.2 ms   | fast 1.00 s / slow 25.0 s |

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| Six positions exist | **exact** | `TimingPosition` enum, `kTimingPresets[6]` |
| Positions 1–4 as fixed RC | **approximate** | Values estimated; not from traced hardware measurements |
| Attack = 0.2 ms for all positions | **approximate** | Aligned to manual intent; exact RC values unknown |
| Positions 5–6 as auto/programme-dependent | **approximate** | Implemented via dual-branch release (`AutoReleaseParams`); fast/slow τ values are estimates, not traced from hardware |
| Exact programme-dependent law | **unknown** | The precise CV→release-rate function is not detailed in the available manual pages |

### Implementation approach (positions 5 & 6)

The current implementation uses a **dual parallel release branch** model:

1. Both branches share the same fast attack coefficient.
2. After the signal drops, a *fast branch* decays with τ_fast and a *slow branch* decays with τ_slow.
3. The output CV is `max(cv_fast, cv_slow)`.

This produces the expected qualitative behaviour:
- Brief transients: fast branch decays, quick recovery.
- Sustained loud material: slow branch persists, slower recovery.

The fast/slow time constants are estimates. See `src/DSP/Models/Sidechain/TimingNetwork.cpp` for current values.

---

## 2. Variable-mu gain stage

### Manual description

The Fairchild 670 uses 6386 remote-cutoff variable-mu pentodes in a push-pull
common-cathode topology.  The tube's transconductance decreases gradually and
asymmetrically as the grid is driven more negative, providing programme-dependent
gain reduction with a characteristically smooth, never-hard-clipping response.

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| Variable-mu topology | **approximate** | MNA + Newton-Raphson triode solve per sample |
| Tube model (6386 remote-cutoff) | **approximate** | Koren model with `x=1.0` exponent (versus 1.4 for sharp-cutoff) gives gradual Ip ∝ E₁ law, approximating the remote-cutoff character |
| Push-pull (differential) topology | **approximate** | `VariableMuPushPullStage`: two `VariableMuStage` arms driven at ±Vin; differential combination cancels even harmonics |
| B+ supply (250 V) | **approximate** | Default `Vcc = 250 V` |
| Plate resistor | **approximate** | Default `Rp = 100 kΩ` |
| Cathode resistor | **approximate** | Default `Rk = 1.5 kΩ` |
| Cathode bypass capacitor | **approximate** | Continuously variable 0–47 µF knob (modern extension) |
| Gain normalisation | **modern extension** | Unity small-signal gain at CV=0, no hardware equivalent |

### Remote-cutoff model notes

The 6386 Koren fit uses `x = 1.0` (versus `x ≈ 1.4` for the earlier 6072
sharp-cutoff approximation).  With `x = 1`, the plate current is proportional
to E₁ (the effective grid voltage) rather than E₁^1.4, which stretches the
knee of the gain-reduction curve and prevents it from ever reaching a
hard cutoff.  This is a first-order approximation; a more accurate fit would
require matching published 6386 plate curves.

---

## 3. Sidechain / detector

### Manual description

The sidechain is a full-wave rectifier feeding an RC envelope follower.  The
hardware uses a 6AL5 twin-diode rectifier.  The detected envelope (control
voltage) is applied to the grid of the variable-mu stage to reduce gain.

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| Full-wave rectifier with 6AL5 soft onset | **approximate** | `SoftRectifierDetector`: forward voltage Vf ≈ 0.8 V applied before the RC smoother |
| Attack/release RC smoothing | **approximate** | One-pole IIR; physically equivalent to an RC network |
| Dual-branch auto release (P5/P6) | **approximate** | See §1 above |
| Detector-to-stage CV law | **unknown** | The exact voltage gain from detector output to grid bias is not published; `cvMaxV = 6 V` is an estimate |

---

## 4. Transformers

### Manual description

The Fairchild 670 uses input, interstage, and output transformers.  Each
transformer introduces bandwidth limiting and saturation colouration.  The
input transformer feeds both the audio path and the sidechain.  The interstage
transformer couples the variable-mu stage to the output amplifier.

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| Input transformer (P3) | **approximate** | `TransformerLinear`; HPF=30 Hz, LPF=30 kHz, drive=1.2; feeds both sidechain and signal path |
| Interstage transformer (P5) | **approximate** | `TransformerLinear`; HPF=25 Hz, LPF=22 kHz, drive=1.1; between push-pull stage and output |
| Output transformer (P6) | **approximate** | `TransformerSecondOrder`; 2nd-order biquad HPF+LPF (Q=0.7071 Butterworth); tanh saturator |
| HF resonance peak (output transformer) | **approximate** | Modelled via `lpfQ` parameter; default Q=0.7071 (flat) |
| Exact transformer parameters | **unknown** | Fairchild's transformer specifications are not publicly available |

---

## 5. Pre-amplifier and output amplifier tube stages

### Manual description

The hardware stacks multiple tube stages per channel (input amp, variable-mu
stage, output amp), each contributing harmonic coloration and a small amount
of bandwidth limiting via coupling capacitors.

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| Pre-amplifier stage (P7) | **approximate** | `VariableMuStage` with 12AU7 Koren params; CV=0 always (fixed gain, harmonic coloration only) |
| Output amplifier | **not implemented** | No separate output-amp stage yet; output transformer follows directly |
| Tube types (12AX7, 12AU7, etc.) | **approximate** | 12AU7 Koren parameters used for pre-amp; exact hardware tube complement not verified |

---

## 6. Stereo link modes

### Manual description

The Fairchild 670 has a Lateral/Vertical (L/V) switch that puts the left and
right channels in a Mid/Side configuration.

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| Independent mode | **approximate** | Channels process independently |
| Mid/Side (Lateral/Vertical) | **approximate** | Matches manual intent |
| Linked Max / Linked Average | **modern extension** | Not in original hardware; added for flexibility |

---

## 7. Exposed plugin parameters

| Parameter | Source | Status |
|-----------|--------|--------|
| Threshold (L/R) | Plugin abstraction | **modern extension** — the original hardware has no threshold control; gain reduction starts immediately with any signal level |
| Time constant position (1–6) | Manual switch | **approximate** |
| Stereo mode (Independent / Linked / M-S) | Manual L/V switch + extensions | **modern extension** (Linked modes); **approximate** (M/S) |
| Cathode bypass Ck | Hardware pot (not original) | **modern extension** |
| Oversampling | Plugin only | **modern extension** |
| Draft / High quality | Plugin only | **modern extension** |
| Mix | Plugin only | **modern extension** |

---

## 8. Input-vs-output transfer curves

The Fairchild 670 manual contains transfer-curve graphs showing input level vs
output level and gain reduction.  These curves are **not yet digitised** into
the repo.  The current test reference data in `TransferCurveTests.cpp`
(updated for the enhanced model) contains qualitative approximations measured
from the DSP core itself, not digitised hardware measurements.  The new model
(6386 remote-cutoff tube + 6AL5 soft rectifier) produces less gain reduction
at low-to-moderate drive levels compared to the previous 6072 approximation,
which is expected: the remote-cutoff characteristic and the rectifier dead zone
both work to keep the compressor "barely breathing" on gentle material.

**Status: approximate / not fully calibrated against hardware.**

Future work: digitise the manual's transfer curves and tighten the automated
comparison tolerance in `tests/TransferCurveTests.cpp`.

---

## 9. What is intentionally not authentic

The following behaviours are plugin-only and have no hardware counterpart:

- **Threshold control**: the original 670 compresses any signal above the
  inherent bias point of the tube.  The threshold abstraction is kept for
  flexibility but is documented here as a modern extension.
- **Oversampling**: the original hardware is entirely analogue.
- **Dry/Wet (Mix) knob**: analogue hardware does not have a mix control.
- **Draft/High quality modes**: affect only the NR iteration count, a
  software-only concept.
- **Linked Average stereo mode**: the original only has L/V (M/S); a
  Linked Max mode is a commonly documented variant, but Linked Average is
  a modern extension.
