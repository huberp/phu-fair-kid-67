# Fairchild 670 — Manual Reference Plot (Input vs. Output Curves)

**Source image:** `docs/ManualReferencePlot.png`  
**Original document date:** December 1959 (supersedes March 1959 issue)  
**Graph paper:** K&E (Keuffel & Esser) 10×10 to the inch, catalogue no. 359-5

---

## 1. What Is dBm?

**dBm** (decibels relative to one milliwatt) is a logarithmic power ratio unit expressing an absolute power level referenced to 1 mW:

```
P(dBm) = 10 × log₁₀ ( P_mW / 1 mW )
```

For audio work with **600 Ω** balanced lines (the Fairchild 670's standard impedance):

| dBm  | Power      | Voltage (RMS into 600 Ω) |
|------|-----------|--------------------------|
| −10  | 100 µW    | 245 mV                   |
|   0  | 1 mW      | **775 mV** (0 dBm reference) |
| +4   | 2.5 mW    | 1.23 V  (standard "line level" +4 dBu ≡ +4 dBm at 600 Ω) |
| +10  | 10 mW     | 2.45 V                   |
| +20  | 100 mW    | 7.75 V                   |
| +30  | 1 W       | 24.5 V                   |

The formula for the corresponding RMS voltage at impedance R is:

```
V_rms = √( 10^(dBm/10) × 10⁻³ × R )
```

At 600 Ω, **0 dBm = 0.775 V RMS** (this exact value defines the older "VU" reference in broadcast engineering). dBm was the lingua franca of professional broadcast and studio equipment throughout the 1950s and 1960s because all equipment was designed for 600 Ω termination.

> **Why it matters for this plug-in:**  
> The Fairchild 670 service manual plots level in dBm, while the plug-in works in dBFS (decibels full-scale). To compare the simulation against the manual curves a calibration offset must be applied — see §6.

---

## 2. Graph Overview

The plot is titled **"INPUT VS. OUTPUT CURVES"** and shows the steady-state transfer characteristic of the Fairchild 670 — that is, how hard the unit hits the output for a given continuous input level.  The graph was drawn on a Keuffel & Esser (K&E) 10×10/inch semi-log paper sheet, a standard tool in mid-century broadcast engineering labs.

| Axis       | Label       | Range          | Units |
|------------|-------------|----------------|-------|
| Horizontal | DBM IN      | −10 to +30     | dBm   |
| Vertical   | DBM OUT     | −10 to +20     | dBm   |

A **unity-gain diagonal** (1 dB out per 1 dB in) would run at exactly 45° across the chart. Any curve that falls below this diagonal is being compressed; the farther below, the greater the gain reduction at that input level.

---

## 3. The Five Transfer Curves

The legend lists five numbered configurations.  Each curve was measured at a **1 kHz steady-state sine tone** sweeping from −10 dBm to +30 dBm while the compressor settled.

### Legend text (verbatim from the image)

1. Straight amplifier, AC THRESHOLD control fully CCW, DC THRESHOLD control position nonimportant.
2. AC THRESHOLD control slightly CW from CCW position, DC THRESHOLD control fully CW.
3. Factory-adjusted condition.
4. AC THRESHOLD control fully CW and DC THRESHOLD control slightly CCW from CW position.
5. AC THRESHOLD control fully CW, DC THRESHOLD control slightly CW from CCW position.

### Rotation terminology used in the legend

- **CW** means clockwise rotation of a potentiometer shaft.
- **CCW** means counter-clockwise rotation.
- **"slightly CW from CCW"** means start at full CCW (minimum), then rotate a small amount toward CW.
- **"slightly CCW from CW"** means start at full CW (maximum), then rotate a small amount back toward CCW.

### What "AC THRESHOLD" and "DC THRESHOLD" mean in the hardware

The Fairchild 670 has **two separate sidechain calibration potentiometers** on the sidechain board.
In this document, they are treated as **internal alignment controls** used for service/factory setup,
not as day-to-day programme controls:

| Control       | Function |
|---------------|----------|
| **AC THRESHOLD** | Sets the gain of the AC-coupled signal path feeding the 6AL5 twin-diode rectifier. Turning CCW (minimum) reduces the rectifier drive to near zero; fully CW maximises the drive. This is the primary sensitivity control for programme-dependent gain reduction. |
| **DC THRESHOLD** | Applies a fixed DC bias current directly to the control grids of the 6386 variable-mu tubes, independently of the audio signal. A small DC current at the grid nudges the tube's operating point toward greater gain reduction at all times. |

Together they allow fine-grained calibration of the onset (AC) and depth floor (DC) of the compressor.

Operational controls (front-panel use) are conceptually separate from these internal AC/DC calibration trims.

---

### Curve ① — Straight Amplifier (no compression)

**Settings:** AC THRESHOLD fully CCW (sidechain input = minimum, rectifier receives no meaningful signal), DC THRESHOLD position irrelevant (set to mid or any value).

**Behaviour:** The circuit acts as a pure fixed-gain amplifier. The 6386 tubes idle at their quiescent operating point without any modulation of the control grid bias. The output rises exactly 1 dB per 1 dB of input (unit slope), offset by the net fixed gain of the signal chain (input transformer + 12AU7 pre-amplifier + 6386 + output transformer), which measures approximately **+9 dB** on this unit.

**Shape:** Perfectly straight, slope = 1.

| DBM IN | DBM OUT | Approx. GR vs. ① |
|--------|---------|------------------|
| −10    | −1      | 0 dB             |
| −5     | +4      | 0 dB             |
|  0     | +9      | 0 dB             |
| +5     | +14     | 0 dB             |
| +10    | +19     | 0 dB             |

---

### Curve ② — Light Programme Compression

**Settings:** AC THRESHOLD slightly CW from CCW position (sidechain active but only lightly driven), DC THRESHOLD fully CW (maximum DC grid bias applied — gives a raised fixed gain-reduction floor).

**Behaviour:** The DC bias alone shifts the 6386's quiescent operating point, resulting in a constant offset of several dB of gain reduction even at low levels. The AC sidechain adds only a mild programme-dependent component on top. The curve follows the straight amplifier closely at low levels, then gently bends toward an asymptote of roughly **+12.5 dBm** at high inputs.

**Peak gain reduction vs. ①:** ≈ 16 dB at +25 dBm in.

| DBM IN | DBM OUT | Approx. GR vs. ① |
|--------|---------|------------------|
| −5     | +4      |  0 dB            |
|  0     | +9      |  0 dB            |
| +5     | +12     |  2 dB            |
| +10    | +13     |  6 dB            |
| +15    | +13     |  11 dB           |
| +20    | +13     |  16 dB           |
| +25    | +12.5   |  16.5 dB         |
| +30    | +12     |  18 dB           |

---

### Curve ③ — Factory-Adjusted Condition

**Settings:** Not specified in the legend (all controls set to the factory calibration marks on the unit). This is the **reference operating point** intended for normal programme use.

**Behaviour:** The factory setting balances AC and DC threshold controls to provide moderate to heavy programme compression across the full input range. Gain reduction starts as soon as any programme material drives the rectifier, deepens rapidly above the knee (around 0 dBm), and approaches a limiting plateau near **+5 to +6 dBm** output at maximum input. This represents approximately **23–24 dB of gain reduction** at +30 dBm in, referenced to the straight amplifier.

| DBM IN | DBM OUT | Approx. GR vs. ① |
|--------|---------|------------------|
| −5     | +2      |  2 dB            |
|  0     | +5      |  4 dB            |
| +5     | +7      |  7 dB            |
| +10    | +7.5    |  11.5 dB         |
| +15    | +7      |  17 dB           |
| +20    | +6      |  23 dB           |
| +25    | +5.5    |  23.5 dB         |
| +30    | +5      |  24 dB           |

---

### Curve ④ — Maximum Programme Compression

**Settings:** AC THRESHOLD fully CW (sidechain driven at maximum sensitivity), DC THRESHOLD slightly CCW from full CW (near-maximum DC grid bias — only slightly backed off from maximum to avoid over-biasing the tube into complete cutoff).

**Behaviour:** Both AC and DC components contribute near-maximum gain reduction. Even moderate input levels produce close to the ceiling gain reduction that the tube topology supports. The output barely rises above **0 dBm** regardless of how hard the input is driven. This represents **≥ 29 dB of gain reduction** at +30 dBm in.

| DBM IN | DBM OUT | Approx. GR vs. ① |
|--------|---------|------------------|
|  0     | +2      |  7 dB            |
| +5     | +1      |  13 dB           |
| +10    | 0       |  19 dB           |
| +15    | 0       |  24 dB           |
| +20    | 0       |  29 dB           |
| +25    | −0.5    |  29.5 dB         |
| +30    | −1      |  30 dB           |

---

### Curve ⑤ — Heavy AC Compression, Minimal DC Bias

**Settings:** AC THRESHOLD fully CW (same as curve ④, maximum sidechain sensitivity), DC THRESHOLD slightly CW from CCW position (near-minimum DC bias — just the programme-dependent AC-driven compression, almost no fixed DC offset).

**Behaviour:** Maximum AC threshold sensitivity means the rectifier drives the grids hard with any programme above the noise floor. However, with almost no DC bias, the compression starts only once the programme level builds sufficient control voltage; there is no constant-gain-reduction floor. The result is a curve that sits **between ② and ③** — heavier than ② at moderate inputs, but with a higher output ceiling than ③ because the DC floor is absent.

| DBM IN | DBM OUT | Approx. GR vs. ① |
|--------|---------|------------------|
| −5     | +3      |  1 dB            |
|  0     | +7      |  2 dB            |
| +5     | +9      |  5 dB            |
| +10    | +10     |  9 dB            |
| +15    | +10.5   |  13.5 dB         |
| +20    | +10     |  19 dB           |
| +25    | +9.5    |  19.5 dB         |
| +30    | +9      |  21 dB           |

---

## 4. Gain Reduction Summary (all five curves)

The following table shows the GR for each curve **relative to the straight amplifier (①)** at selected input levels.  Values are read from the graph and rounded to the nearest 0.5 dB.

| DBM IN | ① (GR) | ② (GR) | ⑤ (GR) | ③ (GR) | ④ (GR) |
|--------|--------|--------|--------|--------|--------|
|  0     | 0      | 0      |  2     |  4     |  7     |
| +5     | 0      | 2      |  5     |  7     | 13     |
| +10    | 0      | 6      |  9     | 11.5   | 19     |
| +15    | 0      | 11     | 13.5   | 17     | 24     |
| +20    | 0      | 16     | 19     | 23     | 29     |
| +25    | 0      | 16.5   | 19.5   | 23.5   | 29.5   |
| +30    | 0      | 18     | 21     | 24     | 30     |

---

## 5. dBm ↔ dBFS Conversion for the Simulation

The plug-in operates with normalised audio samples where **±1.0 = 0 dBFS**.  Internally, the sidechain and tube-model voltage scaling treats ±1.0 as ±10 V peak (see `USER_GUIDE.md`, "Sidechain Detector" section: *"scaled from normalised audio to volts (±1 full-scale = ±10 V)"*).

Converting this to dBm at 600 Ω:

```
V_peak  = 10 V      (at 0 dBFS)
V_rms   = 10 / √2 = 7.07 V
P_mW    = V_rms² / R = 7.07² / 600 = 83.3 mW
P_dBm   = 10 × log₁₀(83.3) ≈ +19.2 dBm
```

Therefore the calibration relationship is:

```
dBFS = dBm − 19.2
dBm  = dBFS + 19.2
```

### Conversion table

| DBM (hardware) | dBFS (plug-in input) |
|---------------|----------------------|
| −10           | −29.2                |
|  −5           | −24.2                |
|   0           | −19.2                |
|  +5           | −14.2                |
| +10           |  −9.2                |
| +15           |  −4.2                |
| +20           |  −0.8 (~0 dBFS)      |
| +25           |  +5.8 (above 0 dBFS) |
| +30           | +10.8 (above 0 dBFS) |

> **Important:** The hardware curves extend to +30 dBm, which is **+10.8 dBFS** — well above what a normalised plug-in can represent without clipping.  The high-input behaviour of curves ③ and ④ therefore cannot be directly replicated with the plug-in at unity input trim.  Use a positive **Input Trim** (+6 dB or more) on the test signal to push the plug-in into the equivalent operating region.

### Gain normalisation note

The plug-in applies **unity small-signal gain** normalisation at CV = 0 (see `fairchild670-spec-traceability.md` §2).  The hardware shows a fixed **+9 dB** amplifier gain for curve ①.  When comparing output levels between the hardware curves and the plug-in, subtract 9 dB from the hardware dBm OUT values (or add +9 dB of Output Trim in the plug-in) before aligning the axes.

---

## 6. Plug-in Settings to Reproduce Each Reference Curve

The hardware has independent AC THRESHOLD and DC THRESHOLD calibration controls. The plug-in model
uses operational threshold controls plus internal AC/DC calibration parameters to approximate this behavior.

Historically this project used a **single Threshold parameter per channel** (range 0–10) as:

```
effectiveCV = max(0,  detectorCV − thresholdVoltage)
thresholdVoltage = 10 − Threshold_param
```

| Threshold param | Threshold voltage | Meaning |
|-----------------|-------------------|---------|
| 0               | 10 V              | No compression at any level |
| 5 (default)     | 5 V               | Onset near −6 dBFS |
| 10              | 0 V               | Always compressing |

When only a single-threshold path is used, the plug-in does not have a separate DC bias control and curve matching is approximate.
The table below should be read as a practical approximation baseline, not an exact AC/DC hardware mapping:

| Hardware curve | AC THRESHOLD  | DC THRESHOLD       | **Plug-in Threshold** | Link Mode   | Notes |
|---------------|--------------|--------------------|-----------------------|-------------|-------|
| ① Straight amp | Fully CCW    | Nonimportant       | **0** (= 10 V, no GR) | Independent | No compression; verifies unity-gain path |
| ② Light comp   | Slightly CW  | Fully CW           | **2** (= 8 V)         | Independent | Mild onset; plug-in has no separate DC bias |
| ③ Factory      | (factory)    | (factory)          | **5** (= 5 V, default)| Independent | Default plugin operating point |
| ⑤ Heavy AC     | Fully CW     | Slightly CW from CCW | **7** (= 3 V)       | Independent | Heavy compression, no DC floor |
| ④ Max comp     | Fully CW     | Slightly CCW from CW | **9–10** (0–1 V)    | Independent | Maximum compression |

> Curves ② and ③ differ in the hardware by the DC threshold contribution.  Since the plug-in cannot independently dial in a DC grid bias, the recommended settings above are approximations.  Curve ② in particular will look more like ③ (without the flat DC floor at low levels) because the plug-in's onset is purely programme-dependent.

### Recommended additional settings

| Parameter      | Value                            | Reason |
|----------------|----------------------------------|--------|
| Timing         | 1 (0.2 ms attack / 0.3 s release)| Fastest settling for steady-state tone tests |
| Link Mode      | Independent                      | Matches single-channel hardware measurement |
| Oversampling   | 1×                               | No oversampling needed for 1 kHz sine tests |
| Quality        | High                             | Full NR accuracy for comparison |
| Ck             | 0 µF                             | Flat frequency response; removes LF boost |
| Input Trim     | +10 dB                           | Shifts 0 dBFS to correspond to +10 dBm (use to extend into the hardware's +20–30 dBm region) |
| Output Trim    | +9 dB                            | Compensates for the plug-in's gain normalisation; aligns output level with the hardware's +9 dB fixed gain |
| Mix            | 100 %                            | Wet signal only |

---

## 7. Testing with the Calibration Tool (Offline, No DAW)

The standalone `phu_calibrate` tool (see `docs/calibration-workflow.md`) sweeps a 1 kHz sine tone across input levels and measures steady-state transfer — the same method used to generate the manual curves.

### 7.1 Build

```bash
cmake -B build -DPHU_BUILD_PLUGIN=OFF
cmake --build build
# binary: ./build/tools/phu_calibrate
```

### 7.2 Measure transfer for each reference curve

Run the tool with AC/DC sidechain tuple values that approximate each hardware setting:

```bash
# Curve ① — straight amplifier (AC 10.0 V, DC 0.0 V)
./build/tools/phu_calibrate --measure-transfer --position 1 --threshold-ac 10.0 --threshold-dc 0.0 \
    --output /tmp/curve1_transfer.csv

# Curve ② — light compression (AC 8.0 V, DC 1.5 V)
./build/tools/phu_calibrate --measure-transfer --position 1 --threshold-ac 8.0 --threshold-dc 1.5 \
    --output /tmp/curve2_transfer.csv

# Curve ③ — factory condition (AC 5.0 V, DC 1.0 V)
./build/tools/phu_calibrate --measure-transfer --position 1 --threshold-ac 5.0 --threshold-dc 1.0 \
    --output /tmp/curve3_transfer.csv

# Curve ⑤ — heavy AC compression, minimal DC (AC 3.0 V, DC 0.2 V)
./build/tools/phu_calibrate --measure-transfer --position 1 --threshold-ac 3.0 --threshold-dc 0.2 \
    --output /tmp/curve5_transfer.csv

# Curve ④ — maximum compression (AC 0.5 V, DC 1.8 V)
./build/tools/phu_calibrate --measure-transfer --position 1 --threshold-ac 0.5 --threshold-dc 1.8 \
    --output /tmp/curve4_transfer.csv
```

### 7.3 Plot and overlay

The calibration tool outputs dBFS.  To overlay against the hardware dBm curves, add +19.2 to the `input_dbfs` and `output_dbfs` columns and then shift the output by +9 dB (gain normalisation correction):

```bash
# Example overlay with Python (requires matplotlib):
python3 - << 'EOF'
import csv, math, matplotlib.pyplot as plt

DBFS_TO_DBM = 19.2   # offset to convert plugin dBFS → hardware dBm
GAIN_NORM   = 9.0    # hardware fixed gain not present in plugin

def load(path):
    xs, ys = [], []
    with open(path) as f:
        for row in csv.DictReader(f):
            xs.append(float(row['input_dbfs'])  + DBFS_TO_DBM)
            ys.append(float(row['output_dbfs']) + DBFS_TO_DBM + GAIN_NORM)
    return xs, ys

fig, ax = plt.subplots()
for path, label in [
    ('/tmp/curve1_transfer.csv', '① Straight amp (thr=10V)'),
    ('/tmp/curve2_transfer.csv', '② Light comp  (thr=8V)'),
    ('/tmp/curve3_transfer.csv', '③ Factory     (thr=5V)'),
    ('/tmp/curve5_transfer.csv', '⑤ Heavy AC    (thr=3V)'),
    ('/tmp/curve4_transfer.csv', '④ Max comp    (thr=0V)'),
]:
    x, y = load(path)
    ax.plot(x, y, label=label)

ax.set_xlabel('DBM IN'); ax.set_ylabel('DBM OUT')
ax.set_title('Simulation vs. Fairchild 670 Manual Reference')
ax.legend(); ax.grid(True); plt.show()
EOF
```

### 7.4 Existing ctest integration

The automated tests in `TransferCurveTests.cpp` already run these measurements as part of `ctest`:

```bash
cd build && ctest -R TransferCurve --output-on-failure
```

---

## 8. Testing with the Plug-in in a DAW

For a live comparison inside a DAW host:

1. **Generate a swept-level test tone** (many DAWs have a signal generator; alternatively use an offline sine wave generator that sweeps amplitude in 5 dB steps at 1 kHz, holding each level for ≥ 2 seconds to allow the detector to settle with Timing position 1).

2. **Insert the plug-in** and configure per the settings table in §6 for the desired curve number.

3. **Measure RMS output level** at each input step using the DAW's peak/RMS meter or a metering plug-in.

4. **Convert to dBm** using `dBm = dBFS + 19.2`, then plot the (input_dBm, output_dBm) pairs and overlay against the manual reference tables in §3.

5. **Expected deviations:**
   - Curves ①–② will show a near-flat gain at low levels (plugin normalises gain; hardware adds +9 dB offset).
   - Curve ② will not exhibit the hardware DC-bias floor; it will look more like a softer version of ③.
   - Curves ③ and ④ should align closely with the hardware reference (within ±4 dB) using the settings from §6 when Input Trim is +10 dB.

---

## 9. Code Changes Needed to Register the Hardware Reference Data

### 9.1 Update `tests/transfer_curve_reference.csv`

Add digitised hardware reference rows for the factory curve (③) and the no-compression curve (①).  The format required by `TransferCurveTests.cpp` is:

```
input_dbfs,output_dbfs,gr_db,note
```

Hardware data converted to dBFS (using dBFS = dBm − 19.2) and GR referenced to straight-amplifier output (input_dBm + 9 dB):

```csv
# Curve ③ — factory condition (Hardware dBm converted: input_dbfs = dBm_in - 19.2,
#   output_dbfs = dBm_out - 19.2 - 9   [remove +9 dB hardware gain normalisation],
#   gr_db = (dBm_in + 9) - dBm_out  [GR vs. unity-gain pass-through])
-19.2,-22.2,4.0,factory_0dBm
-14.2,-21.2,7.0,factory_+5dBm
 -9.2,-20.7,11.5,factory_+10dBm
 -4.2,-21.2,17.0,factory_+15dBm
 -0.8,-22.2,23.0,factory_+20dBm
```

### 9.2 Update `tests/TransferCurveTests.cpp`

#### a) Add hardware-digitised reference arrays

Add a separate reference array for the factory condition, alongside the existing implementation-provisional `kTransferReference`:

```cpp
/// Hardware-digitised reference for Curve ③ (factory condition).
/// Source: Fairchild 670 service manual, "Input vs. Output Curves", December 1959.
/// Tolerance: ±4 dB (implementation is approximate; tube parameters not fully calibrated).
static const std::vector<TransferPoint> kFactoryReference = {
    { -19.2f, -22.2f,  4.0f },   // 0 dBm hardware input
    { -14.2f, -21.2f,  7.0f },   // +5 dBm hardware input
    {  -9.2f, -20.7f, 11.5f },   // +10 dBm hardware input
    {  -4.2f, -21.2f, 17.0f },   // +15 dBm hardware input
    {  -0.8f, -22.2f, 23.0f },   // +20 dBm hardware input (≈ 0 dBFS)
};

/// Hardware-digitised reference for Curve ① (straight amplifier, no compression).
/// Threshold = 10 V (above any signal); output should equal input (0 dB GR).
static const std::vector<TransferPoint> kStraightAmpReference = {
    { -29.2f, -29.2f, 0.0f },    // -10 dBm hardware input
    { -19.2f, -19.2f, 0.0f },    //   0 dBm hardware input
    { -14.2f, -14.2f, 0.0f },    //  +5 dBm hardware input
    {  -9.2f,  -9.2f, 0.0f },    // +10 dBm hardware input
};
```

#### b) Add a test case for the straight-amplifier curve

```cpp
TEST_CASE("TransferCurve: curve ① straight amplifier — near-unity gain at all levels",
          "[transfer][hardware_reference]")
{
    // With threshold = 10 V the detector CV never reaches the tube grids;
    // the stage should behave as a fixed-gain amplifier (< 1 dB GR at all levels).
    constexpr float kThresholdV = 10.0f;
    constexpr float kMaxGrDb    = 1.0f;

    for (const auto& ref : kStraightAmpReference) {
        const float amp = dbfsToAmplitude(ref.inputDbfs);
        auto m = measureTransfer(amp,
                                 Sidechain::TimingPosition::P1,
                                 44100.0,
                                 /*settleN=*/  30000,
                                 /*measureN=*/ 8192,
                                 kThresholdV);
        INFO("ref inputDbfs=" << ref.inputDbfs << " meas grDb=" << m.grDb);
        REQUIRE(m.grDb < kMaxGrDb);
    }
}
```

#### c) Add a test case for the factory-condition curve

```cpp
TEST_CASE("TransferCurve: curve ③ factory condition — hardware reference (±4 dB)",
          "[transfer][hardware_reference]")
{
    // Factory threshold ≈ 5 V (plugin default = Threshold param 5).
    // The longer settleN (200000 samples ≈ 4.5 s at 44.1 kHz) is required because
    // at moderate threshold the sidechain CV approaches its steady-state value slowly
    // — the RC envelope follower needs many more time constants to fully settle than
    // the zero-threshold case where the CV reaches its ceiling quickly.
    constexpr float kThresholdV   = 5.0f;
    constexpr float kToleranceDb  = 4.0f;

    for (const auto& ref : kFactoryReference) {
        const float amp = dbfsToAmplitude(ref.inputDbfs);
        auto m = measureTransfer(amp,
                                 Sidechain::TimingPosition::P1,
                                 44100.0,
                                 /*settleN=*/  200000,
                                 /*measureN=*/ 8192,
                                 kThresholdV);
        INFO("ref inputDbfs=" << ref.inputDbfs
             << " ref grDb="  << ref.grDb
             << " meas grDb=" << m.grDb
             << " tolerance ±" << kToleranceDb << " dB");
        REQUIRE_THAT(m.grDb,
                     Catch::Matchers::WithinAbs(ref.grDb, kToleranceDb));
    }
}
```

#### d) Update the TODO comment in the existing test

In `TransferCurveTests.cpp`, near the `kTransferReference` declaration, change:

```cpp
// TODO: replace with digitised Fairchild 670 manual measurements and
//       tighten the tolerance once the implementation is better calibrated.
```

to:

```cpp
// NOTE: hardware-digitised reference points are in kFactoryReference and
//       kStraightAmpReference below.  This array (kTransferReference) retains
//       the implementation-provisional values used as regression guards.
//       Once kFactoryReference passes consistently, kTransferReference can be
//       replaced with kFactoryReference and the tolerance tightened to ±2 dB.
```

### 9.3 DSP parameter tuning guidance

If `TransferCurveTests` fails after adding the hardware-digitised reference points, the following parameters are most likely to need adjustment.  Adjust **one at a time**, re-run `phu_calibrate --measure-transfer --threshold-ac 5.0 --threshold-dc 1.0` after each change, and compare against the §3 factory-condition table:

| Parameter | Location | Effect on transfer curve | Direction for more GR |
|-----------|----------|--------------------------|-----------------------|
| `sidechainAmplifierGain` | `Fairchild670Core.h` | Scales how much CV the rectifier output applies to the grids | Increase (e.g. 1.5 → 2.0) |
| `cvMaxV` | `Fairchild670Core.h` via `makeStageCfg6386()` | Sets the ceiling CV that can be applied to the grid | Increase (e.g. 8.0 → 10.0) |
| `kg1` | `TubePresets6386.h` | Controls transconductance; lower = higher gm = more GR | Decrease (e.g. 700 → 500)  |
| `Rp` | `VariableMuStage` default config | Plate load resistor; higher = more gain and more dynamic range | Increase slightly |
| `tubeRectifierForwardVoltageV` | `Fairchild670Core.h` | Raises the sidechain dead zone (reduces compression at low levels) | Decrease toward 0 to remove dead zone |

When matching the **maximum-compression curve (④)**, note that the plug-in may not reach 29–30 dB GR with the current default `cvMaxV = 8 V`.  Consider:
- Raising `cvMaxV` to 10–12 V for the max-compression scenario, or
- Exposing `sidechainAmplifierGain` as a user-facing "Sidechain Drive" parameter (future feature).

### 9.4 Update `docs/fairchild670-spec-traceability.md`

In section 8 ("Input-vs-output transfer curves"), change the status from *not yet digitised* to *approximate — digitised from manual image*:

```markdown
## 8. Input-vs-output transfer curves

Status: **approximate** — curves digitised from the December 1959 service manual image
(`docs/ManualReferencePlot.png`). Reference data is embedded in `TransferCurveTests.cpp`
(`kFactoryReference`, `kStraightAmpReference`).  Tolerance ±4 dB.

Future work: tighten tolerance to ±2 dB by calibrating tube/sidechain parameters
against the factory curve.
```

---

## 10. Summary

| Curve | Hardware setting | Plugin Threshold | Peak GR (at +20 dBm / ~0 dBFS) | Calibration tool flag |
|-------|-----------------|------------------|---------------------------------|-----------------------|
| ①    | AC fully CCW, DC n/a | AC=10.0 / DC=0.0 | 0 dB                         | `--threshold-ac 10.0 --threshold-dc 0.0` |
| ②    | AC slight CW, DC max | AC=8.0 / DC=1.5 | 16 dB                         | `--threshold-ac 8.0 --threshold-dc 1.5` |
| ⑤    | AC full CW, DC min | AC=3.0 / DC=0.2 | 19 dB                          | `--threshold-ac 3.0 --threshold-dc 0.2` |
| ③    | Factory          | AC=5.0 / DC=1.0  | 23 dB                           | `--threshold-ac 5.0 --threshold-dc 1.0` |
| ④    | AC full CW, DC near-max | AC=0.5 / DC=1.8 | 29+ dB                   | `--threshold-ac 0.5 --threshold-dc 1.8` |

The reference image (`docs/ManualReferencePlot.png`) is a primary calibration target for the simulation. The factory condition (curve ③) is the most important: the plugin's default settings should produce a transfer curve that falls within ±4 dB of curve ③ across the entire −20 to 0 dBFS input range when using `Threshold = 5`, `Timing = 1`, and `LinkMode = Independent`.
