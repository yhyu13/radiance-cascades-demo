# Phase 9b — Implementation Detail

**Date:** 2026-05-02
**Branch:** 3d
**Motivation:** Phase 9 self-critique (`doc/cluade_plan/AI/codex_critic/16_phase9_self_critique.md`)
identified four correctible problems: dark warmup, inefficient jitter, no debug text
readouts, and persistent directional banding from D=4.

---

## Overview

Four changes. All C++ only. No new shader files.

| Step | Change | File(s) |
|---|---|---|
| 1 | Debug observability: rebuild counter, EMA coverage %, jitter readout in UI | `demo3d.h`, `demo3d.cpp` |
| 2 | D=8 default (was D=4) — halves directional bin angular step | `demo3d.cpp` constructor |
| 3 | Startup bias fix: seed history = current on first temporal enable | `demo3d.cpp` |
| 4 | Halton(2,3,5) jitter replaces uniform RNG | `demo3d.cpp` |

---

## Step 1 — Debug text readouts

### New members (`src/demo3d.h`, Phase 9 section)

```cpp
/** Phase 9b: Halton sequence index only — increments when jitter is enabled, resets when jitter disabled. */
uint32_t probeJitterIndex;

/** Phase 9b: EMA blend dispatch counter — increments unconditionally at cascadeIndex==0, regardless of jitter. */
uint32_t temporalRebuildCount;

/** Phase 9b: seed history textures = current bake on next warm-up rebuild (eliminates dark warmup). */
bool historyNeedsSeed;
```

### Constructor init (`src/demo3d.cpp`)

```cpp
, probeJitterIndex(0)
, temporalRebuildCount(0)
, historyNeedsSeed(false)
```

### UI readout (`src/demo3d.cpp`, inside `if (useTemporalAccum)` block)

```cpp
// Phase 9b: debug text readouts
float emaFill = 1.0f - std::pow(1.0f - temporalAlpha, (float)temporalRebuildCount);
ImGui::Text("Rebuilds: %u  EMA fill: %.0f%% (heuristic)", temporalRebuildCount, emaFill * 100.0f);
if (useProbeJitter)
    ImGui::TextDisabled("Jitter: (%.3f, %.3f, %.3f)",
        currentProbeJitter.x, currentProbeJitter.y, currentProbeJitter.z);
```

**What each display means:**
- `Rebuilds:` — `temporalRebuildCount`, incremented at `cascadeIndex == 0` every time the temporal blend dispatch fires, regardless of whether jitter is on. Honest when temporal is on and jitter is off.
- `EMA fill (heuristic):` — `1 - (1-alpha)^N`. EMA settling estimate assuming constant alpha. Not a pixel-accurate readout of history content.
- `Jitter:` — current frame's Halton jitter vector in probe-cell units. Only visible when jitter is enabled. Values should look structured (evenly spaced), not random.

**EMA fill formula derivation:** EMA with constant signal C converges as `C * (1 - (1-alpha)^N)`. The factor `(1 - (1-alpha)^N)` is the fraction of the asymptotic value reached after N steps. Its complement, `(1-alpha)^N`, is the residual weight of the zero-initialized history. EMA fill = 1 minus that residual.

**Phase 9b does NOT add:**
- New render modes (render mode enum unchanged)
- History texture viewer in the radiance debug panel
- Current-vs-history residual or error visualization
- Reset-history button

---

## Step 2 — D=8 default

### Constructor change (`src/demo3d.cpp`, line ~113)

```cpp
, dirRes(8)   // was dirRes(4) — D=8 gives 64 bins vs 16, substantially finer directional quantization
```

### Effect with `useScaledDirRes=true` (default)

`cascadeDirRes[i] = useScaledDirRes ? std::min(16, dirRes << i) : dirRes;`

| Cascade | D at dirRes=4 (old) | D at dirRes=8 (new) |
|---|---|---|
| C0 | 4  (16 bins) | 8  (64 bins) |
| C1 | 8  (64 bins) | 16 (256 bins) |
| C2 | 16 (256 bins) | 16 (256 bins, capped) |

**Why directional banding on box faces:** The octahedral-mapped direction atlas discretizes incoming radiance into D×D bins. As a box face's normal tilts relative to the light direction, the dominant bin changes discretely — a visible color step follows. D=4 gives 16 bins total; coarse quantization produces visible color steps at normal viewing distances. D=8 quadruples the bin count to 64, substantially reducing visible stepping. The exact angular improvement depends on surface normal and light direction geometry — octahedral bins are not uniform solid-angle wedges, so no single degree figure applies.

**Memory cost:** C0 atlas `(32×8)×(32×8)×32 RGBA16F = 256×256×32 × 8 bytes = 16 MB`. At D=4 it was 4 MB. Two active cascades: 32 MB total atlas memory (was 8 MB). Acceptable for modern GPUs.

**Compute cost:** `sampleProbeDir()` in `raymarch.frag` iterates D² bins. D=8 → 64 iterations instead of 16. Display GI pass is ~4× slower. The loop is fully unrolled by the driver for small D; still well within interactive frame budget.

**No shader changes:** All loop bounds read `uAtlasDirRes` uniform. Bake loops in `radiance_3d.comp` use `uDirRes` uniform. Both are set per-cascade from `cascadeDirRes[i]`. D=8 works with existing shaders.

---

## Step 3 — Startup bias fix

### Problem

History textures (`probeAtlasHistory`, `probeGridHistory`) are zero-initialized at allocation. When temporal accumulation is first enabled:

1. Warm-up rebuild fires: `updateSingleCascade()` bakes fresh probe data into `probeAtlasTexture`
2. `temporal_blend.comp` runs: `history = mix(history, current, alpha)` = `mix(0, current, 0.1)` = **10% of final brightness**
3. Display reads history immediately → GI appears at 10% intensity
4. Over ~22 frames at alpha=0.1, brightness climbs to 90%, then 99% at ~44 frames

This makes low alpha values appear broken on enable, and makes comparisons between alpha values misleading.

### Fix

Seed `probeAtlasHistory` = `probeAtlasTexture` and `probeGridHistory` = `probeGridTexture` **before** the first temporal blend dispatch. After seeding:

```
mix(history=current, current, alpha) = current * (1-alpha) + current * alpha = current
```

GI appears at 100% brightness on the very first rebuild. EMA then accumulates from full brightness, converging toward the jittered EMA asymptote for the current kernel (rather than upward from zero).

### Implementation

**`update()` — set seed flag when temporal is newly enabled:**
```cpp
if (useTemporalAccum != lastTemporalAccum) {
    lastTemporalAccum = useTemporalAccum;
    if (useTemporalAccum) {
        cascadeReady = false;     // warm-up rebuild
        historyNeedsSeed = true;  // Phase 9b: seed before first blend
        probeJitterIndex = 0;     // reset Halton counter
    }
}
```

**`updateSingleCascade()` — copy before blend:**
```cpp
if (historyNeedsSeed && c.probeAtlasHistory != 0 && c.probeGridHistory != 0) {
    glCopyImageSubData(c.probeAtlasTexture, GL_TEXTURE_3D, 0, 0,0,0,
                       c.probeAtlasHistory,  GL_TEXTURE_3D, 0, 0,0,0,
                       axyz, axyz, c.resolution);
    glCopyImageSubData(c.probeGridTexture, GL_TEXTURE_3D, 0, 0,0,0,
                       c.probeGridHistory,  GL_TEXTURE_3D, 0, 0,0,0,
                       c.resolution, c.resolution, c.resolution);
}
```

`glCopyImageSubData` is a GPU-side texture copy — zero CPU overhead, no readback.

**`updateRadianceCascades()` — clear flag after all cascades seeded:**
```cpp
// after cascade loop
historyNeedsSeed = false;
```

The flag is cleared in `updateRadianceCascades()` rather than inside `updateSingleCascade()` so that all cascades (C0, C1, ...) are seeded in the same rebuild before the flag clears. If it were cleared inside `updateSingleCascade()` on the first call, subsequent cascade levels would not be seeded.

### What `Coverage:` shows after the fix

With seeding, the display starts at 100% brightness. The Coverage formula `1 - (1-alpha)^N` still correctly reflects how many distinct jitter samples have been accumulated — it is a measure of jitter coverage, not a brightness meter. On rebuild 1, Coverage = alpha = 10% (one sample contributing) but brightness = 100%.

---

## Step 4 — Halton(2,3,5) jitter sequence

### Problem with uniform RNG

`std::uniform_real_distribution` generates independent random samples. Over the first N frames, coverage of `[-0.5, 0.5]³` has expected variance `O(1/N)` — but with high probability of clustering. Two consecutive frames may sample nearly the same world position (wasted) while leaving other regions of the cell unsampled.

### Halton sequence

The van der Corput sequence in base b generates values in `[0,1)` by reflecting the base-b representation of the index about the decimal point. For bases 2, 3, 5 (first three primes), the 3D Halton sequence fills the unit cube with low discrepancy:

| Frame | Halton(2) | Halton(3) | Halton(5) | Jitter x | Jitter y | Jitter z |
|---|---|---|---|---|---|---|
| 0 | 0.5   | 0.333 | 0.2   | 0.0   | −0.167 | −0.3   |
| 1 | 0.25  | 0.667 | 0.4   | −0.25 | 0.167  | −0.1   |
| 2 | 0.75  | 0.111 | 0.6   | 0.25  | −0.389 | 0.1    |
| 3 | 0.125 | 0.444 | 0.8   | −0.375| −0.056 | 0.3    |
| 4 | 0.625 | 0.778 | 0.04  | 0.125 | 0.278  | −0.46  |

After 4 frames in x (base-2): {0.0, −0.25, 0.25, −0.375} — already well-spread across `[-0.5, 0.5]`. Random RNG after 4 frames may cluster anywhere.

### Helper function (`src/demo3d.cpp`, file scope)

```cpp
static float halton(uint32_t idx, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (idx > 0) { f /= base; r += f * (idx % base); idx /= base; }
    return r;
}
```

O(log_base(idx)) per call. For typical frame counts (< 10,000), effectively O(1).

### Jitter block in `updateRadianceCascades()`

```cpp
if (useProbeJitter) {
    currentProbeJitter = glm::vec3(
        halton(probeJitterIndex, 2) - 0.5f,
        halton(probeJitterIndex, 3) - 0.5f,
        halton(probeJitterIndex, 5) - 0.5f
    );
    ++probeJitterIndex;
} else {
    currentProbeJitter = glm::vec3(0.0f);
    probeJitterIndex = 0;   // reset when disabled
}
```

`probeJitterIndex` is the Halton sequence index only — not the rebuild counter. `temporalRebuildCount` is the separate counter incremented at `cascadeIndex == 0` regardless of jitter state. Resetting `probeJitterIndex` on jitter disable ensures the next enable starts from the beginning of the Halton sequence (deterministic, reproducible behavior).

### Why probeJitterIndex is reset on temporal enable (Step 3)

When temporal is newly enabled, history is seeded and then EMA accumulates from scratch. Resetting the Halton index ensures the sequence starts from its most evenly-distributed initial segment, not from an arbitrary mid-sequence point.

---

## Files changed

| File | Nature of change |
|---|---|
| `src/demo3d.h` | +3 members: `probeJitterIndex`, `temporalRebuildCount`, `historyNeedsSeed` |
| `src/demo3d.cpp` | +halton() fn; constructor +3 inits, dirRes 4→8; update() seed trigger + reset temporalRebuildCount; updateRadianceCascades() Halton jitter + seed clear; updateSingleCascade() glCopyImageSubData before blend + increment temporalRebuildCount; UI Rebuilds/EMA-fill/Jitter readouts |

No shader changes. No new shader files.

---

## Verification checklist

| Check | Expected |
|---|---|
| Cold start, default settings | D=8 in effect; box-face color steps visibly reduced |
| Enable Temporal accumulation | GI appears at full brightness immediately (no dark warmup) |
| UI shows `Rebuilds: 0  EMA fill: 0% (heuristic)` | Correct — no blends fired yet |
| Enable Probe jitter, watch readout | Jitter values cycle in structured order, not random |
| Wait 10 frames | `Rebuilds: 10  EMA fill: 65%` (alpha=0.1) |
| Wait 22 frames | `Rebuilds: 22  EMA fill: 90%` |
| Disable jitter | Jitter readout disappears; `probeJitterIndex` reset to 0; `temporalRebuildCount` continues |
| Re-enable jitter | Halton x restarts at 0.0 (probeJitterIndex reset); temporalRebuildCount keeps incrementing |
| Mode 6 (GI-only) | No over-brightness; energy-correct `albedo * indirect` |
