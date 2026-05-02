# Phase 11 — Jitter Convergence & Direction Resolution Fixes

**Date:** 2026-05-02
**Trigger:** Three user-reported issues after Phase 10:
1. Direction resolution (`D`) accepted odd values, which are degenerate in octahedral encoding.
2. Temporal jitter convergence was poor — probes moved too far per frame for the EMA to keep up.
3. Jitter amplitude (±0.5 probe cell) was too large, causing excessive frame-to-frame variance.

---

## Summary of Changes

| Change | Files |
|---|---|
| Even-only `dirRes` via radio buttons 2/4/6/8 | `src/demo3d.cpp` |
| `probeJitterScale` (default 0.25, was ±0.5 implicit) | `src/demo3d.h`, `src/demo3d.cpp` |
| `jitterPatternSize` — wrap Halton at N (default 8) | `src/demo3d.h`, `src/demo3d.cpp` |
| `jitterHoldFrames` — dwell each position K frames | `src/demo3d.h`, `src/demo3d.cpp` |
| UI sliders: Scale / Pattern N / Dwell under jitter checkbox | `src/demo3d.cpp` |
| Tutorial panel: 9b line shows live scale/N/dwell | `src/demo3d.cpp` |

---

## Issue 1 — Odd `D` is Degenerate in Octahedral Encoding

### Why odd D is unsafe

Restricting `dirRes` to even values is a conservative engineering constraint
based on the symmetry of the octahedral projection. At odd D, bin centers are
spaced at intervals of `1/D`, and for certain odd values the center nearest the
fold diagonal (where `u + v = 1`) falls close enough to create near-degenerate
mirror bins that can bleed across hemispheres. Even D keeps the fold between
bin centers by construction, avoiding this class of defect.

The exact failure mode at each specific odd D has not been exhaustively verified;
the even-only constraint is adopted as a safe default rather than a proven
geometric theorem.

### Fix

Replace the free `SliderInt` (range 2–8, allowed odd) with radio buttons:

```cpp
static const int kDirResOpts[] = { 2, 4, 6, 8 };
for (int k = 0; k < 4; ++k) {
    if (k > 0) ImGui::SameLine();
    char lbl[8]; snprintf(lbl, sizeof(lbl), "%d##dr%d", kDirResOpts[k], k);
    if (ImGui::RadioButton(lbl, dirRes == kDirResOpts[k]))
        dirRes = kDirResOpts[k];
}
```

Radio buttons make the constraint structurally enforced — no runtime clamping needed.

---

## Issue 2 — Jitter Convergence: Pattern Cycling and Dwell

### Root cause

The original jitter advanced `probeJitterIndex` by 1 every frame (unbounded
Halton walk). With stagger=8 and `temporalAlpha=0.1`:

- C0 rebuilds every frame → new jitter position every frame
- EMA half-life at α=0.1: ~6.6 frames
- The probe position changes faster than the EMA can integrate it

Result: the running average is always "chasing" the most recent jitter position
rather than integrating over a stable set. The accumulated atlas is dominated by
the last few jitter positions rather than a wide spatial footprint.

### Fix: repeating N-tap pattern (`jitterPatternSize`)

Wrap `probeJitterIndex` at `N = jitterPatternSize` (default 8):

```cpp
probeJitterIndex = (probeJitterIndex + 1) % static_cast<uint32_t>(jitterPatternSize);
```

After N frames the pattern repeats. The EMA now integrates over the same N positions
indefinitely. Choosing `alpha ≈ 1/N` gives each new position a weight of `1/N` in the
blend. In steady state this produces a geometrically decaying weighted sum over the N
positions — approximately proportional to a uniform N-tap spatial average after many
cycles, but with the most recent positions always weighted more heavily than earlier
ones in the same cycle. The EMA is not a true box average; it is a bounded, periodic
exponential filter.

**Practical guideline:** `α ≈ 1/N` as a starting point; adjust upward for faster
convergence (accepting more recency bias) or downward for smoother accumulation.

### Fix: jitter dwell (`jitterHoldFrames`)

Hold each jitter position for `K = jitterHoldFrames` frames before advancing.
**Important:** the sample must be taken before the counter is checked, so that
index 0 is always sampled and every position gets exactly K frames:

```cpp
// Sample first — ensures index 0 is sampled and each position gets exactly K frames.
currentProbeJitter = glm::vec3(...halton(probeJitterIndex)...);
++jitterHoldCounter;
if (jitterHoldCounter >= jitterHoldFrames) {
    jitterHoldCounter = 0;
    probeJitterIndex = (probeJitterIndex + 1) % jitterPatternSize;
}
```

If the counter is incremented before sampling (the original incorrect order), index 0
is skipped on the first call (counter immediately reaches threshold and advances before
the sample is taken), and the first position gets K-1 frames instead of K.

With `K=4` the position stays fixed for 4 frames. At α=0.3 and K=4:
- Frames 1-4 at position P_i: EMA integrates 4 samples of P_i → `1-(1-0.3)^4 ≈ 76%` fill
- The EMA has a strong signal from P_i before the position changes to P_{i+1}
- Total cycle: 8 positions × 4 frames = 32 frames per full pattern pass

**Default: `jitterHoldFrames=1`** — matches pre-Phase-11 behavior (advance every frame).
Setting K equal to `staggerMaxInterval` (e.g., K=8) ensures each cascade level has
rebuilt at its own scheduled interval before the jitter position changes.

---

## Issue 3 — Jitter Amplitude Too Large

### Root cause

The original amplitude was implicitly ±0.5 probe cell:

```cpp
currentProbeJitter = glm::vec3(
    halton(probeJitterIndex, 2) - 0.5f,   // [-0.5, +0.5)
    halton(probeJitterIndex, 3) - 0.5f,
    halton(probeJitterIndex, 5) - 0.5f
);
```

At `res=32` and `cellSize=0.125m`, this moves probes up to ±0.0625m per axis —
half a cell. Adjacent jitter positions can be a full cell apart, creating large
per-frame GI discontinuities that require very low alpha (≤0.1) to suppress ghosting.

### Fix: `probeJitterScale`

Multiply by a configurable scale factor (default 0.25, half the original range):

```cpp
currentProbeJitter = glm::vec3(
    (halton(probeJitterIndex, 2) - 0.5f) * probeJitterScale,
    (halton(probeJitterIndex, 3) - 0.5f) * probeJitterScale,
    (halton(probeJitterIndex, 5) - 0.5f) * probeJitterScale
);
```

At `probeJitterScale=0.25`, jitter is ±0.125 cell (±0.016m at default res=32).
This is enough spatial footprint to average out probe-grid banding while keeping
adjacent Halton positions close enough that α=0.3 can cleanly integrate them.

**Guidance on scale:**
- `0.25` (default): balances coverage vs per-frame variance; use with α=0.1–0.3
- `0.5` (original): maximum cell coverage; needs α≤0.1 and history clamp ON
- `0.1`: very tight; reduces flickering at the cost of slower banding suppression

---

## New Members (`src/demo3d.h`)

```cpp
float probeJitterScale;   // amplitude in probe-cell units; default 0.25
int   jitterPatternSize;  // wrap Halton at N; default 8
int   jitterHoldFrames;   // dwell each position K frames; default 1
int   jitterHoldCounter;  // internal: frames elapsed in current hold
```

## Constructor Additions (`src/demo3d.cpp`)

```cpp
, probeJitterScale(0.25f)
, jitterPatternSize(8)
, jitterHoldFrames(1)
, jitterHoldCounter(0)
```

## `updateRadianceCascades()` Jitter Block (full replacement, corrected order)

```cpp
if (useProbeJitter) {
    // Sample at current index first, then decide whether to advance.
    // This ensures index 0 is always sampled and every position gets exactly
    // jitterHoldFrames frames before the next position is selected.
    currentProbeJitter = glm::vec3(
        (halton(probeJitterIndex, 2) - 0.5f) * probeJitterScale,
        (halton(probeJitterIndex, 3) - 0.5f) * probeJitterScale,
        (halton(probeJitterIndex, 5) - 0.5f) * probeJitterScale
    );
    ++jitterHoldCounter;
    if (jitterHoldCounter >= jitterHoldFrames) {
        jitterHoldCounter = 0;
        probeJitterIndex = (probeJitterIndex + 1) % static_cast<uint32_t>(jitterPatternSize);
    }
} else {
    currentProbeJitter = glm::vec3(0.0f);
    probeJitterIndex = 0;
    jitterHoldCounter = 0;
}
```

Note: `probeJitterIndex` wraps at `jitterPatternSize`; `jitterHoldCounter` resets
when jitter is disabled. The sample-then-advance order is critical — reversing it
skips index 0 and gives the first position K-1 frames instead of K.

---

## UI (`renderCascadePanel()`, Temporal section)

### `dirRes` control

```cpp
// Radio buttons — enforces even-only constraint structurally
static const int kDirResOpts[] = { 2, 4, 6, 8 };
for (int k = 0; k < 4; ++k) {
    if (k > 0) ImGui::SameLine();
    char lbl[8]; snprintf(lbl, sizeof(lbl), "%d##dr%d", kDirResOpts[k], k);
    if (ImGui::RadioButton(lbl, dirRes == kDirResOpts[k]))
        dirRes = kDirResOpts[k];
}
ImGui::SameLine();
ImGui::TextDisabled("D^2=%d bins/probe", dirRes * dirRes);
```

### Jitter sub-controls (indented under checkbox, visible when `useProbeJitter`)

```cpp
ImGui::SliderFloat("Scale##jitter",          &probeJitterScale, 0.05f, 0.5f, "%.2f cell");
ImGui::SliderInt("Pattern N##jitter",        &jitterPatternSize, 2, 32);
ImGui::SliderInt("Dwell (frames/pos)##jitter", &jitterHoldFrames, 1, 8);
```

---

## Recommended Starting Points

| Goal | Scale | N | Dwell | Alpha |
|---|---|---|---|---|
| Fast convergence, low flicker | 0.25 | 8 | 1 | 0.3 |
| Stable N-tap average | 0.25 | 8 | 4 | 0.125 (≈1/N) |
| Maximum banding suppression | 0.4 | 8 | 2 | 0.2 |
| Approximate pre-Phase-11 behavior | 0.5 | 32 (max) | 1 | 0.05–0.1 |

---

## Backward Compatibility

- **Jitter OFF**: `currentProbeJitter = vec3(0)`, `probeJitterIndex` and
  `jitterHoldCounter` reset to 0. Identical to pre-Phase-11.
- **`probeJitterScale=0.5` + `jitterPatternSize=32` (UI max) + `jitterHoldFrames=1`**:
  approximates the original unbounded Halton walk for the first 32 frames; thereafter
  the pattern repeats. True unbounded behavior is no longer achievable — the UI slider
  caps `jitterPatternSize` at 32.
- **`jitterHoldFrames=1`** (default): matches pre-Phase-11 per-frame advance behavior.

---

## Verification Checklist

| Check | Procedure |
|---|---|
| Even D enforced | Clicking any radio button sets `dirRes` to 2/4/6/8; no odd value possible |
| D change triggers rebuild | Switching D: atlas reinitializes, cascade stats update |
| Jitter default is smaller | With scale=0.25, `Jitter:` readout shows values in ±0.125 range |
| Pattern wraps | After N rebuilds, `probeJitterIndex` returns to 0 (Jitter readout cycles) |
| Dwell holds | With dwell=4, jitter readout stays constant for 4 consecutive frames |
| Convergence quality | Mode 6 + jitter ON + scale=0.25 + dwell=4: banding softens within 32 frames |
| Jitter OFF | Disabling jitter: vector snaps to (0,0,0), no per-frame rebuild overhead |
