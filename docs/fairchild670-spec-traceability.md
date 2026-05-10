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

The Fairchild 670 uses 6386 variable-mu pentodes (modelled here as 6072
triodes) in a common-cathode topology.  The tube's transconductance decreases
as the grid is driven more negative, providing programme-dependent gain
reduction.

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| Variable-mu topology | **approximate** | MNA + Newton-Raphson triode solve per sample |
| Tube model (6386 → 6072) | **approximate** | 6072 Koren triode parameters; not 6386 pentode |
| B+ supply (250 V) | **approximate** | Default `Vcc = 250 V`; exact value from manual unclear |
| Plate resistor | **approximate** | Default `Rp = 100 kΩ` |
| Cathode resistor | **approximate** | Default `Rk = 1.5 kΩ` |
| Cathode bypass capacitor | **approximate** | Continuously variable 0–47 µF knob (modern extension) |
| Gain normalisation | **modern extension** | Unity small-signal gain at CV=0, no hardware equivalent |

---

## 3. Sidechain / detector

### Manual description

The sidechain is a full-wave rectifier feeding an RC envelope follower.  The
detected envelope (control voltage) is applied to the grid of the variable-mu
stage to reduce gain.

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| Full-wave rectifier | **exact** | `std::abs(UnitScaling::sampleToVolts(sample))` |
| Attack/release RC smoothing | **approximate** | One-pole IIR; physically equivalent to an RC network |
| Dual-branch auto release (P5/P6) | **approximate** | See §1 above |
| Detector-to-stage CV law | **unknown** | The exact voltage gain from detector output to grid bias is not published; `cvMaxV = 6 V` is an estimate |

---

## 4. Output transformer

### Manual description

The Fairchild 670 uses Fairchild-spec output transformers that introduce
bandwidth limiting and saturation colouration.

### Implementation status

| Aspect | Status | Notes |
|--------|--------|-------|
| HPF (transformer low-frequency rolloff) | **approximate** | First-order HPF; default ~30 Hz |
| LPF (transformer high-frequency rolloff) | **approximate** | First-order LPF; default ~18 kHz |
| Saturation | **approximate** | Soft-clip via `tanh` |
| Exact transformer parameters | **unknown** | Fairchild's transformer specifications are not publicly available |

---

## 5. Stereo link modes

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

## 6. Exposed plugin parameters

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

## 7. Input-vs-output transfer curves

The Fairchild 670 manual contains transfer-curve graphs showing input level vs
output level and gain reduction.  These curves are **not yet digitised** into
the repo.  The current test reference data (`tests/transfer_curve_reference.csv`)
contains qualitative approximations, not digitised hardware measurements.

**Status: approximate / not fully implemented.**

Future work: digitise the manual's transfer curves and tighten the automated
comparison tolerance in `tests/TransferCurveTests.cpp`.

---

## 8. What is intentionally not authentic

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
