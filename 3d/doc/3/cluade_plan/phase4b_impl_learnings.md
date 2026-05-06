# Phase 4b Implementation Learnings — Per-Cascade Ray Count Scaling

**Date:** 2026-04-24  
**Branch:** 3d  
**Status:** Code complete + debug vis added  
**Depends on:** Phase 4a (commit `72e4f01`)

---

## Implemented

### Core scaling (`initCascades()` + constructor)

`baseRaysPerProbe` (default 8) is a new runtime member. At init time, each cascade receives `baseRaysPerProbe * (1 << i)` rays:

```cpp
cascades[i].initialize(probeRes, cellSz, volumeOrigin, baseRaysPerProbe * (1 << i));
```

Default configuration: C0=8, C1=16, C2=32, C3=64. Total rays dispatched = 120 vs the flat-8 baseline of 32 (3.75×).

### Runtime sentinel (`render()`)

No `initCascades()` call needed on slider change — texture allocation is ray-count-independent. The sentinel updates `raysPerProbe` directly and invalidates the cascade:

```cpp
static int lastBaseRays = -1;
if (baseRaysPerProbe != lastBaseRays) {
    lastBaseRays = baseRaysPerProbe;
    for (int i = 0; i < cascadeCount; ++i)
        cascades[i].raysPerProbe = baseRaysPerProbe * (1 << i);
    cascadeReady = false;
}
```

### Shader wiring — radiance integration (no changes needed)

No changes to `radiance_3d.comp`. `uRaysPerProbe` is already pushed from `c.raysPerProbe` in `updateSingleCascade()` (line 872), and the shader loop already iterates exactly `uRaysPerProbe` times and divides by the same count.

### Debug visualization shader — extended

`radiance_debug.frag` was extended for 4b debug support:
- Added `uniform int uRaysPerProbe`
- Added mode 4 (hit-type heatmap) decoding packed alpha into surf/sky/miss fractions
- `renderRadianceDebug()` now pushes `cascades[selC].raysPerProbe` as `uRaysPerProbe`

### UI

- **Slider** in Cascades panel: `SliderInt("Base rays/probe", 4, 8)` with live `C0=N C1=2N C2=4N C3=8N` readout
- **Total rays** display: "Total rays dispatched: 120 (flat-8 baseline: 32)"
- **Settings panel** probe grid line updated from single `cascades[0].raysPerProbe` to full per-level breakdown
- **Tutorial panel** 4b entry is always green with live ray counts

### Dead constant removed

`BASE_RAY_COUNT = 4` removed from `demo3d.h` — it was declared but never referenced.

---

## Codex Critic Corrections Applied Before Implementation

Three issues were caught in `04_phase4b_plan_review.md` and fixed before writing any code:

### 1. Slider ceiling lowered from 32 → 8

**Root cause:** The packed alpha decode `static_cast<int>(packed / 255.0f + 0.5f)` gives wrong `skyH` when `surfH >= 128`. Example: packed=128 (128 surface hits) → `128/255 = 0.502 → +0.5 = 1.002 → int = 1` (wrong; should be 0).

At base=8, C3 fires 64 rays max → surfH ≤ 64 → decode is monotone-correct (64/255 ≈ 0.25 < 0.5, rounds to 0). Safe. Python-verified for four boundary cases before implementation.

**Fix:** `SliderInt` range capped at `4, 8`.

### 2. `renderMainPanel()` → `renderSettingsPanel()`

The plan named a non-existent function. Confirmed via grep: the correct function is `renderSettingsPanel()` at line 1763.

### 3. "No shader changes" narrowed

Changed to "No shader changes required for radiance integration" throughout.

---

## Debug Visualizations Added

### A. Per-cascade probe-luminance distribution histogram (ImGui)

During probe readback, a 16-bin histogram is computed per cascade over the spatial distribution of probe luma values:

```cpp
// 16-bin probe-luminance distribution — spatial histogram across all res^3 probes.
float histMax = std::min(mean * 4.0f, maxLum);  // adaptive range
// bin all res³ probes into 16 buckets, normalize so tallest bin = 1.0
```

Section header in UI: **"Probe-luminance distribution (16 bins, range [0, mean*4])"** — labeled to distinguish it from a per-probe noise histogram. A hover tooltip explains the heuristic nature. Displayed with `ImGui::PlotHistogram` under the stats rows, labeled `C0 r=8` through `C3 r=64`.

### B. Cascade-wide luminance distribution variance

```cpp
// Cascade-wide luminance distribution variance: E[X^2] - E[X]^2 over all res^3 probes.
// This is NOT per-probe Monte Carlo variance — it captures scene spatial structure
// (light gradients, wall colours) as well as sampling noise. Use as a heuristic only.
probeVariance[ci] = sumLum2 / N - mean * mean;
```

Shown in the stats row as `dist_var=0.00000`. The label `dist_var` (not `var`) signals it is a distribution metric. A hover tooltip in the UI spells out the distinction: captures scene spatial structure as well as sampling noise, not a per-probe Monte Carlo estimate.

### C. Hit-type heatmap (radiance_debug.frag mode 4)

New mode 4. Decodes packed alpha into spatial fractions per probe slice:

```glsl
float skyF  = floor(packed / 255.0 + 0.5) / N;
float surfF = mod(packed + 0.5, 255.0) / N;
float missF = max(0.0, 1.0 - surfF - skyF);
fragColor = vec4(missF, surfF, skyF, 1.0);  // R=miss, G=surf, B=sky
```

### D. Radiance debug mode controls

`radianceVisualizeMode` was never settable at runtime despite the UI claiming `[F] Cycle`. Fixed:
- Added `KEY_F` handler in `processInput()` cycling modes 0–4
- Added radio buttons inline in Cascades panel when radiance debug is open

---

## Expected / How to Interpret

> These are predictions based on how the implementation works, not confirmed runtime observations.

**Histogram and variance are spatial distribution metrics, not per-probe Monte Carlo noise metrics.** `probeVariance[ci]` is `E[lum²] - E[lum]²` computed across all `res³` probes. This captures the scene's luminance gradient (bright ceiling vs dark floor, coloured walls) as well as sampling noise. A wide histogram or large variance can legitimately arise from real scene structure with zero sampling noise.

The metrics are **heuristic indicators**: when comparing the *same scene* at base=4 vs base=8, scene structure cancels out, so a distribution tightening is *consistent with* reduced sampling noise — but it is not proof. A true per-probe Monte Carlo variance would require storing `E[X²]` and `E[X]` per probe independently (a dedicated buffer not currently in scope).

**What to watch for:**
- Histogram shape is likely dominated by the scene's bright/dark probe split regardless of ray count
- Variance difference between base=4 and base=8 may be small compared to spatial variation
- Hit-type heatmap (mode 4) is a more direct verification: at base=8, C3 should show more uniform green coverage vs base=4

---

## Observed at Runtime

- Build succeeded: 0 errors, 30 pre-existing warnings (unchanged from Phase 4a)
- Console logs `[4b] baseRaysPerProbe=8 C0=8 C1=16 C2=32 C3=64 total=120` on startup sentinel fire
- No formal before/after histogram captures or bake-time measurements taken

---

## Open Questions

- Does the probe-luminance variance actually decrease measurably between base=4 and base=8 for the Cornell Box scene, given the dominant spatial gradient?
- Would a per-probe `E[X²]` estimate (stored in a separate texture) be worth adding for 4c validation?
- Is 30 pre-existing warnings worth a cleanup pass before 4c?

---

## Key Invariants Confirmed (by code inspection)

| Invariant | Status |
|---|---|
| Texture allocation is ray-count-independent | ✓ `RadianceCascade3D::initialize()` allocates `resolution^3` only |
| Shader divides by `uRaysPerProbe` (not hardcoded) | ✓ Line 179 of radiance_3d.comp |
| Sentinel fires once per slider change, not every frame | ✓ Static local `lastBaseRays = -1` |
| Packed alpha decode safe for N ≤ 64 (base=8 max) | ✓ Python-verified four boundary cases |
| `BASE_RAY_COUNT = 4` dead constant removed | ✓ Gone from demo3d.h |

---

## Performance Note

| Config | Total rays | vs flat-8 |
|---|---|---|
| flat-8 (old default) | 32 | 1× |
| base=4 | 60 | 1.9× |
| base=8 (new default) | 120 | 3.75× |
| base=32 (reference only — unsafe for current debug decode) | 480 | 15× |

Shadow ray cost (`inShadow()` — 32-step march) dominates. For a static Cornell Box that bakes once, 120 rays is acceptable.

---

## Relation to 4c

Phase 4c changes `raymarchSDF()` to return actual hit distance `t` in `.a` instead of `1.0`. The sentinel-based ray count update in 4b is a uniform change only — no conflict. More rays in 4b reduce per-probe sampling noise, making the blend in 4c more effective.

The hit-type heatmap (mode 4) reads the packed alpha `surfHits + skyHits * 255.0` from `imageStore`. Phase 4c does **not** change the stored probe alpha — only the function's return sentinel. The heatmap will remain valid after 4c lands.
