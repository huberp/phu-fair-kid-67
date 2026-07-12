# PHU-TUBE-SAT — 3-Stage Tube Saturator: Implementation Specification

**Version:** 1.0  
**Date:** 2026-07-04  
**Status:** Draft — for AI-assisted implementation planning  
**Repository target:** `phu-tube-sat` (new repository; spec hosted here in `phu-fair-kid-67` as the reference analog project)

---

## Table of Contents

1. [Purpose and Scope](#1-purpose-and-scope)
2. [Circuit Design](#2-circuit-design)
3. [Signal Flow](#3-signal-flow)
4. [phu-plugin Baseline Patterns](#4-phu-plugin-baseline-patterns)
5. [Repository and Build Structure](#5-repository-and-build-structure)
6. [DSP Architecture](#6-dsp-architecture)
7. [Parameter Layout (APVTS)](#7-parameter-layout-apvts)
8. [UI Design](#8-ui-design)
9. [Testing Strategy](#9-testing-strategy)
10. [Acceptance Criteria](#10-acceptance-criteria)
11. [Implementation Phases](#11-implementation-phases)
12. [Open Questions and Future Work](#12-open-questions-and-future-work)

---

## 1. Purpose and Scope

### 1.1 Product Description

**PHU-TUBE-SAT** is a VST3/AU audio plugin implementing a physically-modelled, three-stage vacuum-tube saturation circuit. It targets mix engineers and producers who want authentic valve harmonic colour without compression or gain reduction.

The model is derived from a classical hi-fi / recording console preamp topology: three common-cathode triode stages, capacitively coupled, followed by an output transformer coloration model. All nonlinear elements are solved per-sample using Modified Nodal Analysis (MNA) + Newton-Raphson iteration, reusing the `phu-audio-lib` analog primitives already established in `phu-fair-kid-67`.

### 1.2 Design Goals

| Goal | Criterion |
|---|---|
| Physical accuracy | Each stage solved with Koren triode model + NR, not a memoryless waveshaper |
| Correct inter-stage coupling | DC coupling caps modelled with trapezoidal companion integration |
| Harmonic character | Stage 1+2 (12AX7) produce predominantly 2nd harmonic; Stage 3 (12AU7) shifts character toward odd harmonics at higher drive levels |
| Transformer coloration | Output `TransformerLinear` adds HPF rolloff, LPF bandwidth limit, and soft core saturation |
| Oversampling | Optional 1×/2×/4×/8× for alias-free nonlinear processing |
| Parallel dry/wet | Full wet/dry mix control with latency-compensated dry path |
| DAW automation | All meaningful parameters exposed via APVTS |
| Real-time safety | Zero heap allocation, zero locking, zero system calls on the audio thread |
| Testability | JUCE-free DSP core; all physics testable without a plugin host |

### 1.3 What is NOT in scope

- Gain reduction / compression (use `phu-fair-kid-67` for that)
- Sidechain / CV detection
- Mid/Side processing
- Stereo-linked saturation (each channel is independent)

---

## 2. Circuit Design

### 2.1 Physical Topology

The circuit is a three-stage common-cathode triode preamplifier. Each stage is capacitively coupled to the next so that the quiescent (DC) operating point of each stage does not propagate forward. The signal path is:

```
                           Stage 1               Stage 2               Stage 3
                           12AX7                 12AX7                 12AU7

 Vin ──[Rg1]──[Cc0]──[Rgl1]──Grid1            [Rgl2]──Grid2         [Rgl3]──Grid3
                                │                       │                       │
                         Vcc──[Rp1]──Plate1     Vcc──[Rp2]──Plate2    Vcc──[Rp3]──Plate3
                                │                       │                       │
                             [Triode1]               [Triode2]              [Triode3]
                                │                       │                       │
                           Cathode1                Cathode2               Cathode3
                                │                       │                       │
                           [Rk1]──GND             [Rk2]──GND             [Rk3]──GND
                           (║Ck1)                 (║Ck2)                 (║Ck3)

         Plate1──[Cc1]──Rgl2──Grid2    Plate2──[Cc2]──Rgl3──Grid3    Plate3──[Cc3]──Vout
                                                                                    │
                                                                           [OutputTransformer]
                                                                                    │
                                                                                 Vout_final
```

**Legend**
- `Rg1` — input resistor (optional; models source impedance, 10 kΩ default)
- `Rgl1/2/3` — grid-leak resistors (1 MΩ); provide DC discharge path for coupling caps
- `Rp1/2/3` — plate load resistors
- `Rk1/2/3` — cathode self-bias resistors
- `Ck1/2/3` — cathode bypass capacitors (optional; increase stage gain when populated)
- `Cc0` — input coupling capacitor (47 nF); DC-blocks the plugin input
- `Cc1/2` — inter-stage coupling capacitors (47 nF)
- `Cc3` — output coupling capacitor (100 nF)
- `[OutputTransformer]` — `TransformerLinear` model (HPF + LPF + tanh saturator)

### 2.2 Component Values

#### Stage 1 — 12AX7 (high gain, primary harmonic generator)

| Component | Value | Notes |
|---|---|---|
| Tube | 12AX7 | µ ≈ 100, Koren params from `TubeParams::tubeParams12AX7()` |
| Vcc | 250 V | B+ supply |
| Rp1 | 100 kΩ | Classic 12AX7 plate load |
| Rk1 | 1.5 kΩ | Self-bias cathode resistor |
| Ck1 | 0 / 4.7 / 47 µF | Selectable; 0 = no bypass (lower gain, more NFB); 47 µF = full bypass (max gain) |
| Cc1 | 47 nF | Inter-stage coupling cap |
| Rgl1 | 1 MΩ | Grid-leak to ground |

**Quiescent point (target, no Ck):** Vpk ≈ 130 V, Vk ≈ 1.3 V, Ip ≈ 0.87 mA

#### Stage 2 — 12AX7 (second saturation layer, cascaded colour)

| Component | Value | Notes |
|---|---|---|
| Tube | 12AX7 | Same tube type as Stage 1 |
| Vcc | 250 V | |
| Rp2 | 100 kΩ | |
| Rk2 | 1.5 kΩ | |
| Ck2 | 0 / 4.7 / 47 µF | Same options as Stage 1 |
| Cc2 | 47 nF | |
| Rgl2 | 1 MΩ | |

**Quiescent point:** same as Stage 1 (identical topology)

#### Stage 3 — 12AU7 (output stage, lower gain, smoother character)

| Component | Value | Notes |
|---|---|---|
| Tube | 12AU7 | µ ≈ 21.5, `TubeParams::tubeParams12AU7()` |
| Vcc | 250 V | |
| Rp3 | 47 kΩ | Lower plate resistor for 12AU7 |
| Rk3 | 820 Ω | Lower cathode resistor |
| Ck3 | 0 / 4.7 µF | |
| Cc3 | 100 nF | Output coupling cap (larger for better LF extension) |
| Rgl3 | 1 MΩ | |

**Quiescent point (target):** Vpk ≈ 150 V, Vk ≈ 1.2 V, Ip ≈ 2.6 mA

#### Coupling Capacitor Integration

All coupling caps use the `CapacitorCompanion` trapezoidal model from `analog/circuit/elements/Capacitor.h`. Each cap `Cc` forms an RC high-pass with the downstream grid-leak resistor `Rgl`:

$$f_{-3dB} = \frac{1}{2\pi \cdot R_{gl} \cdot C_c} = \frac{1}{2\pi \cdot 10^6 \cdot 47 \times 10^{-9}} \approx 3.4\ \text{Hz}$$

This is well below the audio band; the coupling caps affect only the low-frequency phase response and transient DC offset behaviour.

#### Output Transformer

Uses `Analog::Models::TransformerLinear` configured as:

| Parameter | Value |
|---|---|
| `hpfCutoffHz` | 30 Hz (magnetising inductance rolloff) |
| `lpfCutoffHz` | 18 kHz (bandwidth limit) |
| `drive` | 1.0 (fully linear; user-accessible via `transformerDrive` parameter) |

### 2.3 Harmonic Character by Drive Level

| Drive level | Stage 1 | Stage 2 | Stage 3 | Net character |
|---|---|---|---|---|
| Low (< −20 dBFS input) | Warm 2nd | Warm 2nd | Minimal | Gentle even-harmonic colour |
| Medium (−10 dBFS) | Moderate 2nd/3rd | Moderate 2nd/3rd | 2nd clipping onset | Rich overtone structure |
| High (0 dBFS, clamped at grid) | Grid-onset clipping | Saturated | Soft-limiting | Aggressive but musical |

---

## 3. Signal Flow

### 3.1 Per-sample audio thread flow

```
processBlock():
  │
  ├─ Input trim (per channel, dB gain, juce::dsp::Gain)
  │
  └─ OversamplingChain::process():
       │
       └─ juce::dsp::Oversampling upsample
            │
            └─ ThreeStageChain::processSample() [per oversampled sample]:
                 │
                 ├─ sampleToVolts()          (±1.0 → ±10 V)
                 ├─ Cc0 companion update      (input coupling)
                 ├─ Stage1 NR solve           (2×2, Vp1/Vk1)
                 │   plate voltage → Cc1 → Rgl2 node voltage
                 ├─ Stage2 NR solve           (2×2, Vp2/Vk2)
                 │   plate voltage → Cc2 → Rgl3 node voltage
                 ├─ Stage3 NR solve           (2×2, Vp3/Vk3)
                 │   plate voltage → Cc3 → output
                 ├─ TransformerLinear::processSample()
                 └─ voltsToSample()           (V → ±1.0)
            │
            └─ juce::dsp::Oversampling downsample
  │
  ├─ Output trim (per channel, dB gain)
  │
  └─ DryWetMixer (latency-compensated dry path)
```

### 3.2 Cross-thread meter flow

```
Audio thread (processBlock):
  write std::atomic<float>  meterInputLDb_, meterInputRDb_   (peak input level)
  write std::atomic<float>  meterOutputLDb_, meterOutputRDb_ (peak output level)
  write std::atomic<float>  meterThdL_, meterThdR_           (estimated THD%, computed from running variance)

Message/UI thread (timerCallback, ~60 Hz):
  read atomics → update LevelMeter components
```

### 3.3 Latency

The only latency source is the oversampling FIR filter when factor > 1×. The `DryWetMixer` compensates the dry path by `OversamplingChain::getLatencySamples()`.

```cpp
setLatencySamples(oversamplingChain_.getLatencySamples());
```

---

## 4. phu-plugin Baseline Patterns

This section documents the engineering conventions that ALL phu-plugins share. Any implementation of this spec **MUST** follow these patterns exactly.

### 4.1 JUCE Plugin Class Structure

```
class Phu<Name>AudioProcessor : public juce::AudioProcessor
```

- Constructor: initializes `BusesProperties` (stereo in/out) and `apvts` in the member-initializer list.
- `apvts` is a public member of type `juce::AudioProcessorValueTreeState`.
- Parameter IDs are `static constexpr const char*` constants defined on the processor class.
- `createParameterLayout()` is a `static` factory called only from the constructor.
- Raw atomic parameter pointers are cached via `apvts.getRawParameterValue()` in the constructor (not in `prepareToPlay`).
- Meter values shared with the UI are `std::atomic<float>` private members, written per-block on the audio thread, read by the editor timer.
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Phu<Name>AudioProcessor)` at the end of the private section.

```
class Phu<Name>AudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
```

- Constructor takes a `Phu<Name>AudioProcessor&` reference.
- Inherits `juce::Timer` privately; calls `startTimerHz(60)` in the constructor.
- `timerCallback()` reads atomics from the processor, updates meters and repaints.
- APVTS attachments use `std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>` etc.
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` at end of private section.

### 4.2 Parameter Conventions

- Each parameter has a `static constexpr const char* kParam<Name>` string constant.
- Parameters use `juce::ParameterID{kParam<Name>, 1}` (version 1).
- Numeric ranges use `juce::NormalisableRange<float>` with explicit step size.
- Labels use `.withLabel("dB")`, `.withLabel("V")`, etc.
- Read-only meter parameters are `AudioParameterFloat` named `kParamMeter*`; they are written by the processor each block and should not be automated (document this in comments).

### 4.3 Audio Thread Safety Rules

All rules from `.github/instructions/audio-thread-safety.instructions.md` apply. Summary:

- **NEVER** allocate memory on the audio thread.
- **NEVER** lock a mutex on the audio thread.
- **NEVER** perform I/O (file, network, console) on the audio thread.
- Cross-thread state uses `std::atomic<float>` (meter values, peak levels).
- Oversampling order changes and quality changes are detected by comparing cached integers (`lastOversamplingOrder_`, `lastQualityChoice_`) each `processBlock` call. When a change is detected, re-prepare inside `processBlock` is only done if the change can be done safely; otherwise, trigger a host-side re-prepare by calling `prepareToPlay`.
- DSP objects are prepared in `prepareToPlay()` and used in `processBlock()` — never constructed or destroyed during audio callbacks.

### 4.4 Unit Scaling Convention

All analog circuit models operate in volts. The mapping between plugin samples and circuit voltages is defined in `analog/dsp/UnitScaling.h`:

```cpp
constexpr float kVoltsPerSample = 10.0f;  // ±1.0 normalized = ±10 V
```

Every plugin that uses `phu-audio-lib` analog models **must** apply `sampleToVolts()` before passing to circuit models and `voltsToSample()` on the way out. This keeps tube operating points (Vpk ≈ 100–250 V, Vgk ≈ −3–0 V) within their designed ranges.

### 4.5 DSP Core / JUCE Separation

- The physical circuit model (the part doing NR solves, MNA, and tube math) lives in a **JUCE-free static library** (e.g., `PhuTubeSatModels`).
- This library links only against `PhuAnalogLib` (and transitively `PhuAudioLib`, `PhuNetworkLib` if needed).
- JUCE headers must not be included in the DSP core. This enforces testability without a plugin host.
- The JUCE plugin code (`PluginProcessor.cpp`, `OversamplingChain.cpp`) wraps the DSP core.

### 4.6 Oversampling Wrapper Pattern

```cpp
class OversamplingChain {
    juce::dsp::Oversampling<float> oversampler_;
    <CoreType> core_;
public:
    void setOversamplingOrder(int order);   // message thread only
    void prepare(double sampleRate, int maxBlockSize);
    void process(juce::AudioBuffer<float>& buffer);
    int  getLatencySamples() const noexcept;
};
```

`setOversamplingOrder()` involves filter construction (heap allocation); it must NOT be called from the audio thread. The processor detects order changes in `processBlock` and defers re-initialization to the next `prepareToPlay`.

### 4.7 State Persistence

- `getStateInformation` / `setStateInformation` serialize/deserialize the APVTS XML.
- No additional state outside APVTS needs persisting for this plugin (no learned curves, no file references).

### 4.8 Build System

- `CMakeLists.txt` using `juce_add_plugin()`.
- `CMakePresets.json` with at minimum `vs2026-x64` (Windows) and `linux-release` (Linux CI) presets.
- JUCE and `phu-audio-lib` are **git submodules**.
- Tests enabled via `PHU_BUILD_TESTS=ON` option.
- `PIC` (`POSITION_INDEPENDENT_CODE ON`) set on all static libs that link into the VST3 shared library.
- `NOMINMAX` defined on MSVC to prevent `min`/`max` macro pollution.
- Compile definitions: `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_VST3_CAN_REPLACE_VST2=0`.

---

## 5. Repository and Build Structure

```
phu-tube-sat/
├── CMakeLists.txt                  # top-level; JUCE + phu-audio-lib submodules
├── CMakePresets.json               # vs2026-x64 / linux-release / linux-debug presets
├── JUCE/                           # git submodule (juce-framework/JUCE)
├── phu-audio-lib/                  # git submodule (huberp/phu-audio-lib, branch v0.5.0+)
├── src/
│   ├── CMakeLists.txt              # juce_add_plugin(phu-tube-sat)
│   ├── PluginProcessor.h/.cpp      # JUCE AudioProcessor wrapper
│   ├── PluginEditor.h/.cpp         # JUCE AudioProcessorEditor + timer UI
│   └── DSP/
│       ├── Models/
│       │   └── TubeSat/
│       │       ├── ThreeStageChain.h/.cpp   # JUCE-free: 3-stage MNA/NR per sample
│       │       └── CouplingCapStage.h       # helper: cap companion + grid-leak model
│       └── Utils/
│           └── OversamplingChain.h/.cpp     # juce::dsp::Oversampling wrapper
├── tests/
│   ├── CMakeLists.txt              # Catch2 via FetchContent; separate executables per component
│   ├── ThreeStageChainTests.cpp
│   ├── CouplingCapTests.cpp
│   └── TubeSatIntegrationTests.cpp
├── scripts/
│   ├── install-linux-deps.sh
│   └── find-cmake.ps1
└── docs/
    └── circuit-design.md           # ASCII schematic + component value rationale
```

### 5.1 CMake Targets

| Target | Type | Links against | Notes |
|---|---|---|---|
| `PhuTubeSatModels` | STATIC | `PhuAnalogLib` | JUCE-free DSP core |
| `phu-tube-sat` (VST3/AU) | SHARED | `PhuTubeSatModels`, `juce::juce_audio_processors`, `juce::juce_dsp` | Plugin binary |
| `phu_three_stage_tests` | EXECUTABLE | `Catch2::Catch2WithMain`, `PhuTubeSatModels` | Unit tests |
| `phu_tube_sat_integration_tests` | EXECUTABLE | `Catch2::Catch2WithMain`, `PhuTubeSatModels` | Integration tests |

---

## 6. DSP Architecture

### 6.1 `ThreeStageChain` — Core DSP class

Location: `src/DSP/Models/TubeSat/ThreeStageChain.h/.cpp`  
Dependencies: `analog/models/TubeStage.h`, `analog/circuit/elements/Capacitor.h`, `analog/models/transformer/TransformerLinear.h`, `analog/dsp/UnitScaling.h`  
No JUCE dependency.

#### Configuration

```cpp
struct ThreeStageChainConfig {
    Analog::Models::TubeStageConfig stage1;   // 12AX7, Rp=100k, Rk=1.5k
    Analog::Models::TubeStageConfig stage2;   // 12AX7, Rp=100k, Rk=1.5k
    Analog::Models::TubeStageConfig stage3;   // 12AU7, Rp=47k,  Rk=820R

    double Rgl         = 1.0e6;    // Grid-leak resistor (Ω), all stages
    double Cc_input_F  = 47.0e-9;  // Input coupling cap (F)
    double Cc12_F      = 47.0e-9;  // Stage 1→2 coupling cap (F)
    double Cc23_F      = 47.0e-9;  // Stage 2→3 coupling cap (F)
    double Cc_out_F    = 100.0e-9; // Output coupling cap (F)

    Analog::Models::TransformerLinearConfig transformer; // output transformer
};
```

**Default construction** must produce `ThreeStageChainConfig` that matches the component values in §2.2 exactly.

#### Per-stage coupling cap voltage node

Each inter-stage coupling cap `Cc` forms an RC with the downstream grid-leak `Rgl`. The grid voltage seen by stage N+1 is the voltage at the junction of `Cc` and `Rgl`. This is an additional first-order LP/HP system:

```
Plate(N) ──[Cc]──┬──[Rgl]── GND
                 │
             Vgrid(N+1)
```

Using the trapezoidal companion model (`CapacitorCompanion`), the grid voltage at time step k is:

$$V_{grid}[k] = \frac{V_{plate}[k-1] \cdot G_{eq,C} + I_{eq,C}}{G_{eq,C} + G_{Rgl}}$$

where $G_{eq,C} = 2C/T$ and $G_{Rgl} = 1/R_{gl}$. This is solved analytically (no NR needed) since it is a linear sub-circuit.

#### Interface

```cpp
class ThreeStageChain {
public:
    explicit ThreeStageChain(ThreeStageChainConfig cfg = {}) noexcept;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Process one sample (at oversampled rate when oversampling is enabled).
    // Input and output are normalized (±1.0 full-scale).
    [[nodiscard]] float processSample(float sample) noexcept;

    // Runtime configurability (safe to call between processBlock calls,
    // but NOT from within the audio callback itself):
    void setDriveDb(float dB) noexcept;        // Pre-stage input gain
    void setCk(int stageIndex, double farads) noexcept; // 0,1,2 → stages 1,2,3
    void setTransformerDrive(float drive) noexcept;

private:
    ThreeStageChainConfig cfg_;

    Analog::Circuit::CapacitorCompanion capIn_;   // Cc0 companion
    Analog::Circuit::CapacitorCompanion capCouple12_; // Cc1 companion
    Analog::Circuit::CapacitorCompanion capCouple23_; // Cc2 companion
    Analog::Circuit::CapacitorCompanion capOut_;  // Cc3 companion

    Analog::Models::TubeStage stage1_, stage2_, stage3_;
    Analog::Models::TransformerLinear transformer_;

    float driveLinear_ = 1.0f; // pre-stage drive scale factor
};
```

#### Coupling cap node computation (helper, inline)

```cpp
// Given plate voltage Vplate and the companion model for the coupling cap,
// compute the voltage at the Cc–Rgl junction (grid voltage for the next stage).
// Updates the companion model state.
static double couplingNodeVoltage(double Vplate,
                                  Analog::Circuit::CapacitorCompanion& cap,
                                  double Geq_Rgl) noexcept
{
    // KCL at the junction node:
    //   (Vplate - Vnode) * Geq_C - Ieq_C - Vnode * Geq_Rgl = 0
    //   Vnode = (Vplate * Geq_C - Ieq_C) / (Geq_C + Geq_Rgl)
    const double Vnode = (Vplate * cap.Geq - cap.Ieq) / (cap.Geq + Geq_Rgl);
    cap.update(Vplate - Vnode); // voltage across the cap = Vplate - Vnode
    return Vnode;
}
```

### 6.2 `ThreeStageChain::processSample` implementation sketch

```cpp
float ThreeStageChain::processSample(float sample) noexcept {
    using namespace Analog;

    // 1. Input drive and coupling
    const double Vin = sampleToVolts(sample * driveLinear_);
    const double Vg1 = couplingNodeVoltage(Vin, capIn_, Geq_Rgl_);

    // 2. Stage 1 NR solve: pass Vg1 as grid voltage
    //    TubeStage::processSample takes normalized input; re-scale
    const float  s1in  = voltsToSample(static_cast<float>(Vg1));
    const float  s1out = stage1_.processSample(s1in);
    const double Vp1   = sampleToVolts(s1out);  // plate voltage in volts

    // 3. Cc1 coupling: Stage1 plate → Stage2 grid
    const double Vg2 = couplingNodeVoltage(Vp1, capCouple12_, Geq_Rgl_);

    // 4. Stage 2
    const float  s2out = stage2_.processSample(voltsToSample(static_cast<float>(Vg2)));
    const double Vp2   = sampleToVolts(s2out);

    // 5. Cc2 coupling: Stage2 plate → Stage3 grid
    const double Vg3 = couplingNodeVoltage(Vp2, capCouple23_, Geq_Rgl_);

    // 6. Stage 3
    const float  s3out = stage3_.processSample(voltsToSample(static_cast<float>(Vg3)));
    const double Vp3   = sampleToVolts(s3out);

    // 7. Output coupling cap
    const double Vout_cap = couplingNodeVoltage(Vp3, capOut_, Geq_Rgl_);

    // 8. Output transformer coloration
    const float transformerIn = voltsToSample(static_cast<float>(Vout_cap));
    return transformer_.processSample(transformerIn);
}
```

> **Implementation note:** `TubeStage::processSample` internally does `sampleToVolts` on its input and `voltsToSample` on its output. The coupling cap helper works directly in volts. The round-trip volt→sample→volt→sample is safe but introduces a scale identity. An optimization pass may shortcut this by exposing plate voltage directly from `TubeStage` in a future lib version. For now, the round-trip conversion is correct.

### 6.3 `OversamplingChain` — JUCE wrapper

Same pattern as `phu-fair-kid-67/src/DSP/Utils/OversamplingChain.h`, substituting `ThreeStageChain` for `Fairchild670Core`. The chain handles stereo by maintaining two independent `ThreeStageChain` instances (one per channel).

```cpp
class OversamplingChain {
    juce::dsp::Oversampling<float> oversampler_;
    ThreeStageChain chainL_, chainR_;
    bool prepared_ = false;
    int  oversamplingOrder_ = 0;
public:
    void setOversamplingOrder(int order);   // message thread only
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);
    [[nodiscard]] int getLatencySamples() const noexcept;
    [[nodiscard]] ThreeStageChain& chainL() noexcept { return chainL_; }
    [[nodiscard]] ThreeStageChain& chainR() noexcept { return chainR_; }
};
```

---

## 7. Parameter Layout (APVTS)

### 7.1 Full Parameter Table

| Parameter ID | Type | Range | Default | Step | Label | Automatable |
|---|---|---|---|---|---|---|
| `inputTrimDb` | Float | −20…+20 | 0.0 | 0.1 | dB | Yes |
| `outputTrimDb` | Float | −20…+20 | 0.0 | 0.1 | dB | Yes |
| `drive` | Float | 0…+24 | 0.0 | 0.1 | dB | Yes |
| `mix` | Float | 0…1 | 1.0 | 0.01 | — | Yes |
| `stage1Ck` | Choice | 0µF / 4.7µF / 47µF | 0µF | — | — | Yes |
| `stage2Ck` | Choice | 0µF / 4.7µF / 47µF | 0µF | — | — | Yes |
| `stage3Ck` | Choice | 0µF / 4.7µF | 0µF | — | — | Yes |
| `oversampling` | Choice | 1x / 2x / 4x / 8x | 1x | — | — | No (host re-prepare) |
| `quality` | Choice | Draft / High | High | — | — | Yes |
| `bypass` | Bool | off/on | off | — | — | Yes |
| `transformerDrive` | Float | 1.0…10.0 | 1.0 | 0.1 | — | Yes |
| `meterInputLDb` *(read-only)* | Float | −60…0 | −60 | 0.1 | dBFS | No |
| `meterInputRDb` *(read-only)* | Float | −60…0 | −60 | 0.1 | dBFS | No |
| `meterOutputLDb` *(read-only)* | Float | −60…0 | −60 | 0.1 | dBFS | No |
| `meterOutputRDb` *(read-only)* | Float | −60…0 | −60 | 0.1 | dBFS | No |

### 7.2 Parameter String Constants (PluginProcessor.h)

```cpp
static constexpr const char* kParamInputTrimDb    = "inputTrimDb";
static constexpr const char* kParamOutputTrimDb   = "outputTrimDb";
static constexpr const char* kParamDrive          = "drive";
static constexpr const char* kParamMix            = "mix";
static constexpr const char* kParamStage1Ck       = "stage1Ck";
static constexpr const char* kParamStage2Ck       = "stage2Ck";
static constexpr const char* kParamStage3Ck       = "stage3Ck";
static constexpr const char* kParamOversampling   = "oversampling";
static constexpr const char* kParamQuality        = "quality";
static constexpr const char* kParamBypass         = "bypass";
static constexpr const char* kParamTransformerDrive = "transformerDrive";
static constexpr const char* kParamMeterInputLDb  = "meterInputLDb";
static constexpr const char* kParamMeterInputRDb  = "meterInputRDb";
static constexpr const char* kParamMeterOutputLDb = "meterOutputLDb";
static constexpr const char* kParamMeterOutputRDb = "meterOutputRDb";
```

### 7.3 `drive` parameter behaviour

`drive` is a pre-stage gain in dB applied inside `ThreeStageChain::processSample` before the input coupling cap:

```cpp
driveLinear_ = juce::Decibels::decibelsToGain(driveDb);
```

Increasing drive pushes more signal voltage into Stage 1's grid, moving the operating point toward and past the grid-conduction onset.

### 7.4 `stageCk` parameter behaviour

When `stage1Ck` changes, the processor detects the change in `processBlock` (via a cached integer), calls `chain.setCk(0, newFarads)` on both L/R chains, and then calls `chain.prepare(currentSampleRate)` to re-initialize the companion model `Geq`. This involves no heap allocation and is safe to do between sample groups within `processBlock`.

Ck values: 0µF → `0.0`, 4.7µF → `4.7e-6`, 47µF → `47e-6`.

### 7.5 `oversampling` parameter behaviour

When the oversampling order changes, `processBlock` detects the difference between `oversamplingOrder` and `lastOversamplingOrder_`. Because `setOversamplingOrder` causes filter reconstruction (heap allocation), the change is applied by requesting a host re-prepare via `AudioProcessor::setLatencySamples` + flagging `oversamplingOrderChanged_ = true`. The next `prepareToPlay` call applies the new order. This is the same pattern used in `phu-fair-kid-67`.

---

## 8. UI Design

### 8.1 Layout Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│  PHU TUBE SAT                                          [bypass] [?] │
├──────────┬──────────────────────────────────┬───────────────────────┤
│  IN      │         ● DRIVE                  │          OUT          │
│ ▮▮▮▮▮▯▯▯ │          (rotary knob)           │       ▮▮▮▮▮▯▯▯        │
│  L  R    │                                  │         L   R         │
├──────────┴──────────────────────────────────┴───────────────────────┤
│  STAGES                                                              │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐  │
│  │   STAGE 1        │  │   STAGE 2        │  │   STAGE 3        │  │
│  │   12AX7          │  │   12AX7          │  │   12AU7          │  │
│  │  Ck: [0/4.7/47µF]│  │  Ck: [0/4.7/47µF│  │  Ck: [0/4.7µF]  │  │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘  │
├──────────────────────────────────────────────────────────────────────┤
│  TRANSFORMER DRIVE: [slider 1.0…10.0]    MIX: [slider 0…100%]      │
│  OVERSAMPLING: [1x/2x/4x/8x]            QUALITY: [Draft/High]      │
│  INPUT TRIM: [slider]    OUTPUT TRIM: [slider]                       │
└──────────────────────────────────────────────────────────────────────┘
```

**Window size:** 600 × 400 px fixed (no resize).

### 8.2 Components

| Component | Type | Attachment |
|---|---|---|
| Drive knob | `juce::Slider` (RotaryVerticalDrag) | `SliderAttachment` → `kParamDrive` |
| Input trim | `juce::Slider` (LinearHorizontal) | `SliderAttachment` → `kParamInputTrimDb` |
| Output trim | `juce::Slider` (LinearHorizontal) | `SliderAttachment` → `kParamOutputTrimDb` |
| Mix slider | `juce::Slider` (LinearHorizontal) | `SliderAttachment` → `kParamMix` |
| Transformer drive | `juce::Slider` (LinearHorizontal) | `SliderAttachment` → `kParamTransformerDrive` |
| Stage 1 Ck | `juce::ComboBox` | `ComboBoxAttachment` → `kParamStage1Ck` |
| Stage 2 Ck | `juce::ComboBox` | `ComboBoxAttachment` → `kParamStage2Ck` |
| Stage 3 Ck | `juce::ComboBox` | `ComboBoxAttachment` → `kParamStage3Ck` |
| Oversampling | `juce::ComboBox` | `ComboBoxAttachment` → `kParamOversampling` |
| Quality | `juce::ComboBox` | `ComboBoxAttachment` → `kParamQuality` |
| Bypass | `juce::ToggleButton` | `ButtonAttachment` → `kParamBypass` |
| Input meter L | `LevelMeter` (Signal kind) | Read `meterInputLDb_` atomic |
| Input meter R | `LevelMeter` (Signal kind) | Read `meterInputRDb_` atomic |
| Output meter L | `LevelMeter` (Signal kind) | Read `meterOutputLDb_` atomic |
| Output meter R | `LevelMeter` (Signal kind) | Read `meterOutputRDb_` atomic |

### 8.3 `LevelMeter` component

Reuse the `LevelMeter` inline class pattern from `phu-fair-kid-67/src/PluginEditor.h` verbatim:

- Dark background, rounded rectangle bar.
- `Signal` kind: green → amber → red at −20 / −6 dBFS thresholds.
- Updated via `timerCallback()` at 60 Hz.

### 8.4 Font convention

Use `juce::Font(juce::FontOptions(size))` — never the deprecated `juce::Font(float)` constructor.

---

## 9. Testing Strategy

All tests use **Catch2 v3** fetched via `FetchContent`. Tests link only against the JUCE-free `PhuTubeSatModels` target (and `PhuAnalogLib`), so they can run without a DAW or audio device.

### 9.1 `ThreeStageChainTests.cpp`

| Test | Criterion |
|---|---|
| Stability: no NaN/Inf at any drive level | All output samples finite for inputs ±0.0 to ±1.0, 1000 samples each |
| Quiescent point convergence | Silent input → output settles to near-zero within 5000 samples |
| Monotone gain with drive | Average output RMS increases monotonically as drive increases 0→24 dB |
| Stage 1 gain without Ck | Voltage gain measured 30–40 dB at −40 dBFS sine input |
| Stage 1 gain with Ck=47µF | Voltage gain increases ≥ 6 dB vs. no-Ck case |
| 2nd harmonic dominance (Stage 1+2) | THD2 > THD3 at −20 dBFS drive-level input |
| Coupling cap HP cutoff | Input sine at 1 Hz attenuated > 40 dB vs. 1 kHz |
| prepare/reset idempotence | Two calls to `prepare()` produce identical results; `reset()` returns to quiescent |

### 9.2 `CouplingCapTests.cpp`

| Test | Criterion |
|---|---|
| DC blocking | DC input → output decays to < 0.001 V within 1 s at 44100 Hz |
| AC passthrough | 1 kHz sine passes with < 0.1 dB attenuation |
| Trapezoidal companion correctness | Step response matches analytic RC decay within 1% |

### 9.3 `TubeSatIntegrationTests.cpp`

| Test | Criterion |
|---|---|
| Stereo independence | L-channel drive does not affect R-channel output |
| `prepare()` + `reset()` at multiple sample rates | 44100, 48000, 88200, 96000 Hz — no crash, finite output |
| Oversampling chain at 4× | `OversamplingChain` output finite and not louder than input + 24 dB |
| State persistence round-trip | APVTS XML → restore → identical output on same input |

### 9.4 Test fixture convention (matches `phu-fair-kid-67` pattern)

```cpp
/// Warm up stage by processing silent samples, then return it ready to measure.
static ThreeStageChain makeWarmedChain(double sampleRate = 44100.0,
                                       int warmupSamples = 5000)
{
    ThreeStageChain chain;
    chain.prepare(sampleRate);
    for (int i = 0; i < warmupSamples; ++i)
        (void)chain.processSample(0.0f);
    return chain;
}
```

---

## 10. Acceptance Criteria

These criteria must be satisfied before the implementation can be considered complete. Each maps to at least one test case.

| ID | Criterion | How verified |
|---|---|---|
| AC-1 | Silent input produces silent output (< −120 dBFS) after warmup | Integration test: 5000-sample RMS of silence output |
| AC-2 | No NaN/Inf produced at any valid input in [−1.0, +1.0] | Unit test: exhaustive across drive settings |
| AC-3 | Gain increases monotonically with `drive` parameter | Unit test: RMS measurement across drive sweep |
| AC-4 | Cathode bypass Ck increases gain by ≥ 6 dB | Unit test: A/B measurement |
| AC-5 | 2nd harmonic is dominant distortion product at moderate drive | Unit test: THD2 > THD3 (FFT measurement) |
| AC-6 | DC coupling caps block DC to < 1 mV after 500 ms | Unit test |
| AC-7 | Output cleans up with Mix=0 (dry signal unchanged) | Integration test |
| AC-8 | APVTS state round-trips correctly | Integration test |
| AC-9 | Plugin builds as VST3 on Windows and Linux without warnings | CI build |
| AC-10 | Oversampling at 4× produces no audible aliasing fold-back of 10 kHz tone (verified by absence of image at 4 kHz below oversampling filter rolloff) | Unit test: spectral check |
| AC-11 | No audio thread allocations (verified by sanitizer) | Manual: AddressSanitizer + custom allocator hook |

---

## 11. Implementation Phases

Each phase produces a compilable, testable increment. An AI planner should generate specific implementation steps within each phase.

### Phase 1 — phu-audio-lib prerequisite: Coupling Cap Node Helper

**Goal:** Add the `couplingNodeVoltage` computation (§6.2) as a reusable utility in `phu-audio-lib` so it can be tested independently.

**Deliverables:**
- `analog/circuit/CouplingCapNode.h` — inline `couplingNodeVoltage(double Vplate, CapacitorCompanion&, double Geq_Rgl)` function
- Unit test in `phu-fair-kid-67/tests/CouplingCapNodeTests.cpp` verifying DC blocking and AC passthrough

**Files to create/modify:**
- `phu-audio-lib/analog/circuit/CouplingCapNode.h` (new)
- `phu-audio-lib/analog/CMakeLists.txt` (add header to install list if applicable)
- `phu-fair-kid-67/tests/CouplingCapNodeTests.cpp` (new)
- `phu-fair-kid-67/tests/CMakeLists.txt` (add test executable)

### Phase 2 — Repository scaffold

**Goal:** Create the `phu-tube-sat` repository skeleton with working empty plugin build.

**Deliverables:**
- `CMakeLists.txt`, `CMakePresets.json`
- Submodule references to JUCE and `phu-audio-lib`
- Minimal `PluginProcessor.h/.cpp` (no DSP yet, pass-through)
- Minimal `PluginEditor.h/.cpp` (blank window)
- `src/CMakeLists.txt`
- `scripts/install-linux-deps.sh`, `scripts/find-cmake.ps1`
- Empty `tests/CMakeLists.txt`
- Plugin builds as VST3 and passes silence

### Phase 3 — `ThreeStageChain` DSP core

**Goal:** Implement and test the JUCE-free chain.

**Deliverables:**
- `src/DSP/Models/TubeSat/ThreeStageChain.h/.cpp`
- `PhuTubeSatModels` CMake target
- `tests/ThreeStageChainTests.cpp` covering AC-1 to AC-6

**Key implementation decisions:**
- Use existing `Analog::Models::TubeStage` for each stage (do NOT re-implement NR)
- Use `CouplingCapNode::couplingNodeVoltage` (from Phase 1) for inter-stage coupling
- Use `Analog::Models::TransformerLinear` at the output
- Default `ThreeStageChainConfig` must match §2.2 component values

### Phase 4 — `OversamplingChain` wrapper

**Goal:** Wrap `ThreeStageChain` in JUCE oversampling.

**Deliverables:**
- `src/DSP/Utils/OversamplingChain.h/.cpp`
- Tests at multiple sample rates (AC-9, AC-10)

### Phase 5 — `PluginProcessor` integration

**Goal:** Wire APVTS, parameter caching, meter atomics, and dry/wet mixer.

**Deliverables:**
- Complete `PluginProcessor.h/.cpp` per §7
- All parameters in `createParameterLayout()` matching §7.1
- Parameter change detection logic (oversampling defer, Ck re-prepare)
- Input/output meter atomics written each block
- `getStateInformation` / `setStateInformation` using APVTS XML serialization
- AC-7, AC-8 passing

### Phase 6 — `PluginEditor` UI

**Goal:** Build the editor per §8.

**Deliverables:**
- Complete `PluginEditor.h/.cpp`
- All controls with APVTS attachments
- Level meters updating at 60 Hz via timer
- Correct `juce::Font(juce::FontOptions(...))` usage throughout
- Fixed 600×400 window

### Phase 7 — CI and release

**Goal:** Green builds on Windows and Linux.

**Deliverables:**
- GitHub Actions workflow: Windows `vs2026-x64` + Linux `linux-release`
- All test executables run via `ctest`
- VST3 artifact uploaded per build

---

## 12. Open Questions and Future Work

| Topic | Notes |
|---|---|
| **Grid conduction (Vgk > 0)** | Current `TubeStage` clamps `Vgk ≤ 0`. Extreme drive into grid-conduction is truncated. A future lib version should model the positive-going grid current as a soft diode branch in the NR system. |
| **B+ supply sag** | The `Vcc = 250 V` supply is ideal. Real tube amps droop under heavy load. PSU sag modelling requires an additional RC network on the supply node and is a future enhancement. |
| **Tube selection parameter** | A future version could expose tube selection per stage (12AX7 / 12AU7 / 6072 / 12BH7) as a parameter, allowing different harmonic characters. |
| **Nonlinear transformer hysteresis** | `TransformerLinear` uses memoryless `tanh`. Jiles-Atherton hysteresis would give the transformer memory across cycles, improving realism at very low frequencies. |
| **Interstage impedance loading** | `TubeStage` does not expose its plate impedance (`Rp ‖ rp`). A more faithful coupling model would load the downstream grid-leak against the upstream plate resistance. |
| **Mono plugin** | Current design always uses stereo buses. A future mono variant is trivially derived by using a single chain. |
| **Preset system** | No preset system is specified. A future version should add factory presets via `AudioProcessorValueTreeState::Listener` or a dedicated `PresetManager`. |
