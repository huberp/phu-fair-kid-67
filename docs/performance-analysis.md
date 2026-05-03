# Performance Analysis: phu-fair-kid-67

## 1. Project Type, Stack, and Hot Paths

**Stack:** C++17 audio plugin (JUCE VST3/AU), CMake build, Catch2 unit tests, GitHub Actions CI.
No web frontend, no database, no network I/O at runtime.

**Entrypoints:**
- `src/PluginProcessor.cpp` — `processBlock()` is the real-time audio callback, called by the host DAW at the audio interrupt rate.
- `src/PluginEditor.cpp` — trivial JUCE editor (placeholder, no meaningful rendering yet).

**Main hot path (per audio block):**

```
processBlock()                           PluginProcessor.cpp:112
  ├─ inputGain.process()                 (JUCE, SIMD-accelerated)
  ├─ OversamplingChain::process()        OversamplingChain.cpp:35
  │    ├─ oversampler_->processSamplesUp()    (JUCE FIR upsample, O(N·M))
  │    ├─ per-sample loop [N × 2^order samples]:
  │    │    Fairchild670Core::processStereo() Fairchild670Core.cpp:77
  │    │      ├─ RectifierDetector×2         RectifierDetector.cpp:24
  │    │      └─ VariableMuStage×2           VariableMuStage.cpp:69
  │    │           └─ NRPolicy::solve()      NR.h:99
  │    │                └─ stepFn [up to 20×]:
  │    │                     ├─ triodeIp()          TriodeKoren.cpp:91
  │    │                     ├─ triodeDIpDVpk()     TriodeKoren.cpp:100
  │    │                     └─ triodeDIpDVgk()     TriodeKoren.cpp:121
  │    └─ oversampler_->processSamplesDown()  (JUCE FIR downsample)
  ├─ outputGain.process()
  └─ dryWetMixer.mixWetSamples()
```

At 44.1 kHz + 8× oversampling, `processStereo()` is called ~353,000 times per second.
Worst-case per call: 2 stages × 20 NR iterations × 3 `korenIntermediates` evaluations = **120 transcendental-math calls per audio sample.**

---

## 2. Performance Bottlenecks & Recommendations

### 🔴 HIGH-1 — Three redundant `korenIntermediates` calls per NR iteration

**Files:** `src/DSP/Circuit/Nonlinear/TriodeKoren.cpp:91–133`, `src/DSP/Models/Fairchild/VariableMuStage.cpp:105–134`

Inside the NR step lambda, three separate public functions are called:

```cpp
const double Ip  = triodeIp(Vpk, Vgk, tube);        // calls korenIntermediates
const double gds = triodeDIpDVpk(Vpk, Vgk, tube);   // calls korenIntermediates again
const double gm  = triodeDIpDVgk(Vpk, Vgk, tube);   // calls korenIntermediates a third time
```

Each call independently recomputes `sqrt(kvb + Vpk²)`, the sigmoid `exp`, and `log1p`.
That is 3× redundant work for every NR iteration.

**Fix:** Add a fused function `triodeIpAndPartials(Vpk, Vgk, p, &Ip, &gds, &gm)` that calls
`korenIntermediates` once and fills all three outputs. Refactor the NR lambda to call it.
**Expected speedup: ~2–2.5× reduction in transcendental math per iteration.**

**Effort: S | Risk: Low**

---

### 🔴 HIGH-2 — `std::pow(E1, p.x)` called up to 3× per NR iteration

**File:** `src/DSP/Circuit/Nonlinear/TriodeKoren.cpp:97, 118, 132`

```cpp
return std::pow(E1, p.x) / p.kg1;                          // line 97
return p.x * std::pow(E1, p.x - 1.0) / p.kg1 * dE1_dVpk;  // line 118
return p.x * std::pow(E1, p.x - 1.0) / p.kg1 * dE1_dVgk;  // line 132
```

`p.x = 1.4` (fractional). `std::pow` with a non-integer exponent is typically implemented as
`exp(x·log(y))` — two transcendental operations. The derivative calls can reuse `E1^x`
directly: `E1^(x-1) = E1^x / E1`, turning 3 pow calls into 1. In the fused function:

```cpp
const double Ex   = std::pow(E1, p.x);
const double Exx1 = (E1 > 0.0) ? Ex / E1 : 0.0;
Ip  = Ex / p.kg1;
gds = p.x * Exx1 / p.kg1 * dE1_dVpk;
gm  = p.x * Exx1 / p.kg1 * dE1_dVgk;
```

**Expected speedup: reduces 3 pow calls to 1 per NR iteration.**

**Effort: S | Risk: Low**

---

### 🔴 HIGH-3 — `std::vector<double>` for fixed-size 2-element state vectors in the NR hot path

**Files:** `src/DSP/Models/Fairchild/VariableMuStage.h:131`, `src/DSP/Circuit/Nonlinear/NR.h:88–89`

```cpp
std::vector<double> x_;         // VariableMuStage.h:131 — always size 2 (Vp, Vk)
std::vector<double> xPrev_;     // NR.h:88 — scratch, always size 2
std::vector<double> xFallback_; // NR.h:89 — scratch, always size 2
```

In `NRPolicy::solve()` (NR.h:108–114), every call runs:

```cpp
xFallback_.assign(x.begin(), x.end());  // every call
// ...per iteration:
xPrev_.assign(x.begin(), x.end());      // every iteration
```

Even though `assign` on a stable-size vector does not allocate, it still uses the vector's
heap pointer, causes indirect memory access, and prevents compiler stack-allocation /
register optimization. Switching to `std::array<double, 2>` (or a templated N-size
`NRPolicy`) eliminates heap indirection and allows these small arrays to live in registers.

**Effort: M | Risk: Low** (requires templating `NRPolicy` or specialising `VariableMuStage`)

---

### 🔴 HIGH-4 — Double-precision arithmetic throughout the NR hot path

**Files:** `src/DSP/Models/Fairchild/VariableMuStage.cpp:69–144`, `src/DSP/Circuit/Nonlinear/NR.h:99–165`, `src/DSP/Circuit/Nonlinear/TriodeKoren.cpp`

The input/output of `processSample()` is `float`, but all internal computation (NR state,
Koren model, KCL residuals, Jacobian) uses `double`. At typical tube voltages (`Vp ≈ 100–200 V`,
`Vk ≈ 1–3 V`), `float` provides ~7 significant digits — sufficient for the 1e-6 to 1e-9
convergence tolerance needed in practice. The NR convergence tolerance is set to `1e-9`
(NRConfig default), which only double can satisfy; relaxing it to `1e-6` (still audibly
transparent) would allow float use.

On modern CPUs: `double` operations use 64-bit FP lanes, while `float` can vectorize 2×
more per SIMD width. On Apple M-series, `float` NEON throughput is ~4× that of `double`.

**Effort: M | Risk: Medium** (requires verifying output quality against reference measurements)

---

### 🟡 MEDIUM-5 — `assign` overhead in `NRPolicy::solve` every iteration

**File:** `src/DSP/Circuit/Nonlinear/NR.h:114`

```cpp
xPrev_.assign(x.begin(), x.end());  // inside the iteration loop
```

Called up to 20 times per sample. For a size-2 vector this is a memcpy of 16 bytes via a
vector iterator path. Combined with HIGH-3, switching to `std::array` and a simple
`xPrev_ = x` eliminates vector iterator overhead.

**Effort: S | Risk: Low**

---

### 🟡 MEDIUM-6 — Two `std::sqrt` calls per NR convergence check

**File:** `src/DSP/Circuit/Nonlinear/NR.h:156`

```cpp
if (std::sqrt(deltaL2) < cfg_.convergenceTol * (std::sqrt(xL2) + 1.0)) {
```

Two `sqrt` operations per iteration. For the 2D state vector, `xL2` is a large value
(~10,000 V²) so `sqrt(xL2) + 1 ≈ sqrt(xL2)`. Precomputing `xNorm = std::sqrt(xL2) + 1.0`
once avoids the second sqrt:

```cpp
const double xNorm = std::sqrt(xL2) + 1.0;
if (std::sqrt(deltaL2) < cfg_.convergenceTol * xNorm) { ... }
```

Alternatively, square both sides to replace both sqrts with a single multiply:

```cpp
if (deltaL2 < cfg_.convergenceTol * cfg_.convergenceTol * xNorm * xNorm) { ... }
```

**Effort: S | Risk: Low**

---

### 🟡 MEDIUM-7 — Mixed float/double in `RectifierDetector::processSample`

**File:** `src/DSP/Models/Sidechain/RectifierDetector.cpp:30–33`

```cpp
const double alpha = (rect > cv_) ? alphaAttack_ : alphaRelease_;
cv_ = static_cast<float>(alpha * cv_ + (1.0 - alpha) * rect);
```

`alphaAttack_` / `alphaRelease_` are `double` (RectifierDetector.h:52). The multiply-add is
performed in double after implicit promotions. Changing the alpha fields to `float` keeps
the hot-path computation in float and avoids promotion. The coefficients only need ~6 digits
of precision (computed from `exp(-1/(tau·fs))`).

**Effort: S | Risk: Low**

---

### 🟡 MEDIUM-8 — Link mode re-applied unconditionally on every `processBlock`

**File:** `src/PluginProcessor.cpp:156–169`

```cpp
const int linkChoice = static_cast<int>(apvts.getRawParameterValue(kParamLinkMode)->load());
switch (linkChoice) {
    case 0: oversamplingChain_.core().setLinkMode(LinkMode::Independent); break;
    case 1: oversamplingChain_.core().setLinkMode(LinkMode::Linked);
            oversamplingChain_.core().setEnvelopeStrategy(LinkedEnvelopeStrategy::Max); break;
    default: ...
}
```

Unlike `kParamTimingPosition` (already cached via `lastTimingPosition_`), the link mode
reads `getRawParameterValue` — a hash lookup + atomic load — and calls setters
unconditionally every block. A cached `lastLinkMode_` field, following the same pattern
used for `lastTimingPosition_`, `lastOversamplingOrder_`, and `lastQualityChoice_`, would
skip the setter calls when nothing has changed.

**Effort: S | Risk: Low**

---

### 🟡 MEDIUM-9 — Heap allocation on the audio thread during oversampling order change

**Files:** `src/PluginProcessor.cpp:140–145`, `src/DSP/Utils/OversamplingChain.cpp:105–120`

```cpp
if (currentOsOrder != lastOversamplingOrder_) {
    oversamplingChain_.setOversamplingOrder(currentOsOrder);
    oversamplingChain_.prepare(getSampleRate(), buffer.getNumSamples());
    setLatencySamples(...);
}
```

`rebuildOversampler()` calls `std::make_unique<juce::dsp::Oversampling<float>>(...)` and
`initProcessing()`, both of which heap-allocate from the audio thread. On some platforms
(Windows WASAPI, macOS CoreAudio with real-time constraints), memory allocation from the
audio thread can cause priority inversion.

**Mitigation:** Use a lock-free message queue (or JUCE's `AsyncUpdater`) to rebuild the
oversampler on the message thread and swap atomically. Or pre-allocate all four oversampling
objects at `prepareToPlay` time and select among them with a pointer swap.

**Effort: M | Risk: Medium**

---

### 🟢 LOW-10 — Early `E1 ≤ 0` guard placed after expensive computation in derivatives

**File:** `src/DSP/Circuit/Nonlinear/TriodeKoren.cpp:103–105, 123–125`

```cpp
double E1, sig, sqV2;
korenIntermediates(Vpk, Vgk, p, E1, sig, sqV2);  // expensive sqrt + exp + log1p
if (E1 <= 0.0) return 0.0;                         // check placed after
```

When the tube is near or at cutoff (high CV), `E1 ≤ 0` can occur. A quick pre-check
(`Vpk <= 0.0` guarantees `E1 ≤ 0` since the softplus term is multiplied by `Vpk/kp`)
could short-circuit the full evaluation in that subset of samples.

**Effort: S | Risk: Low**

---

### 🟢 LOW-11 — `ctest` never invoked in CI

**File:** `.github/workflows/build.yml`

The build includes all ten test executables via `add_subdirectory(tests)` in
`CMakeLists.txt:59`, but `ctest` is never run. Regressions in the DSP core (NR convergence,
triode model accuracy, detector time constants) go undetected on CI.

**Fix:** Add `ctest --test-dir build -C Release --output-on-failure` after the build step
on at least one CI platform (Linux is the fastest).

**Effort: S | Risk: Low**

---

### 🟢 LOW-12 — No LTO or explicit SIMD flags in release presets

**File:** `CMakePresets.json`

The release presets (`linux-release`, `macos-*-release`) set only `CMAKE_BUILD_TYPE=Release`.
No `-flto` (Link-Time Optimization) or explicit vectorization hints are configured. Given the
tight inner loop (NR iterations on 2-element vectors), LTO can inline across translation unit
boundaries, and platform-appropriate SIMD flags enable wider SIMD operations.
Estimated uplift: 10–25% on x86_64.

**Effort: S | Risk: Low** (use target-specific flags rather than `-march=native` for distribution builds)

---

## 3. Summary Table

| # | Description | File : line | Impact | Effort | Risk |
|---|-------------|-------------|--------|--------|------|
| 1 | Fuse 3 `korenIntermediates` calls into 1 per NR step | `TriodeKoren.cpp:91–133`, `VariableMuStage.cpp:105` | **High** | S | Low |
| 2 | Reduce `pow(E1, x)` calls from 3→1 per NR step | `TriodeKoren.cpp:97,118,132` | **High** | S | Low |
| 3 | Replace `std::vector<double>` (size 2) with `std::array<double,2>` for NR state | `NR.h:88–89`, `VariableMuStage.h:131` | **High** | M | Low |
| 4 | Switch NR/triode internals from `double` to `float` (relax tol to 1e-6) | `VariableMuStage.cpp`, `NR.h`, `TriodeKoren.cpp` | **High** | M | Medium |
| 5 | Eliminate per-iteration `assign` in `NRPolicy` | `NR.h:114` | Medium | S | Low |
| 6 | Replace 2 `sqrt` calls in convergence check with 1 | `NR.h:156` | Medium | S | Low |
| 7 | Use `float` alpha coefficients in `RectifierDetector` | `RectifierDetector.cpp:30–33` | Medium | S | Low |
| 8 | Cache link mode in `processBlock` (like timing/quality/OS order) | `PluginProcessor.cpp:156–169` | Medium | S | Low |
| 9 | Guard oversampler rebuild away from audio thread | `PluginProcessor.cpp:140–145` | Medium | M | Medium |
| 10 | Early `E1 ≤ 0` cutoff before `korenIntermediates` | `TriodeKoren.cpp:103,123` | Low | S | Low |
| 11 | Add `ctest` run to CI | `build.yml` | Low | S | Low |
| 12 | Enable LTO / explicit SIMD in release presets | `CMakePresets.json` | Low | S | Low |

---

## 4. Existing Measurement Hooks & Proposed Benchmarking Plan

### Existing hooks

- **`NRPolicy::totalIterations()` / `resetCounters()`** (`NR.h:80–83`) — debug-only
  cumulative NR iteration counter. Enables measuring the average NR convergence rate in a
  profiling build.
- **`juce::ScopedNoDenormals`** in `processBlock` (`PluginProcessor.cpp:114`) — guards
  against costly denormal FP traps.
- **`ProcessingQuality::Draft`** (8 NR iterations) vs. **`High`** (20) — a built-in
  fast/accurate trade-off knob usable as an informal A/B performance reference.

### Proposed minimal measurement plan

1. **Unit-level micro-benchmark** — Add Catch2 `BENCHMARK` cases on
   `VariableMuStage::processSample()` for N=1,000 samples at a steady-state CV. Measure
   before and after each optimization. Target baseline: <500 ns/sample (budget of ~22 µs
   for a 512-sample block at 44.1 kHz, 8× OS, 2 channels).

2. **NR iteration distribution** (debug build) — Enable `NRPolicy::totalIterations()`, run
   a 10-second sine sweep through the compressor, and compute the histogram. If mean < 4
   iterations, reducing `maxIterations` to 8 (Draft quality) for production builds is
   defensible.

3. **Hot-function profiling** — Build with `-O2 -g` (RelWithDebInfo), run under
   `perf record -g` (Linux) or Instruments Time Profiler (macOS) with pluginval driving the
   plugin at 8× oversampling. Confirm that `triodeIp`, `triodeDIpDVpk`, `triodeDIpDVgk`,
   and `NRPolicy::solve` dominate the profile before and after fusing.

4. **Block-level wall-clock timing** (compile-flag guarded) — Wrap
   `oversamplingChain_.process(buffer)` in `std::chrono::high_resolution_clock`
   measurements, logging min/max/mean over 1,000 blocks. Compare across oversampling
   orders and quality levels.

5. **CI regression gate** — Once benchmarks are in place, add a `ctest` run (LOW-11) so
   that correctness regressions are caught before performance work silently regresses
   correctness.
