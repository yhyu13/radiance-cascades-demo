# Phase 13 — AI-Driven Quality Lift

**Date:** 2026-05-02
**Trigger:** Burst analysis from Phase 12b auto-capture run.

**Source evidence:**
- Burst analysis: `tools/frame_17777313678792269.md`
- Probe stats: `tools/probe_stats_17777313678792269.json`

**Key numbers from that run:**

| Metric | Value | Interpretation |
|---|---|---|
| `probeJitterScale` | **0.05** | 5× below Phase 11 recommended default (0.25) |
| `temporalAlpha` | **0.05** | ~13.5-frame EMA half-life; Phase 11 recommends α≈1/N=0.25 at N=4 |
| `jitterPatternSize` | **4** | Live default (not 8 — plan originally assumed 8) |
| C0 surfPct | 29% | Near-field probes — most rays too short to hit walls (expected) |
| C3 maxLum | 0.053 | Far cascade barely contributing (C0 maxLum = 0.559, 10.5× more) |
| C3 variance | 0.000158 | 36× lower than C0 (0.00568) — C3 near-static |
| `cascadeTimeMs` | **0.091 ms** | CPU wall-clock around `updateRadianceCascades()` — suggests manageable throughput at current settings; not a settled performance conclusion |
| `raymarchTimeMs` | 0.064 ms | — |

**Performance observation:** `cascadeTimeMs=0.091ms` is a CPU-side wall-clock, not a GPU
pipeline timer. It suggests throughput is manageable at the current settings, but one burst
run is not sufficient to characterize performance across all configurations. The focus of
Phase 13 is quality; the performance baseline is treated as a current observation only.

**Quality verdict:** the jitter system (Phase 11) was designed to address probe-grid banding
but the default parameters were never retuned. `probeJitterScale=0.05` (5× below the
recommended 0.25) and `temporalAlpha=0.05` (5× below the recommended 1/N=0.25 at N=4)
mean the jitter system is effectively doing almost nothing at the current defaults.

---

## Root-cause summary

### Why the banding is visible (mode 6)

The bilateral blur suppresses probe-grid banding in mode 0, but the underlying probe atlas
has spatial structure because `probeJitterScale=0.05` is too small to meaningfully dither
the probe grid. Phase 11 designed jitter for exactly this purpose — its recommended minimum
is `0.25` (5× larger). At 0.05 the jitter barely moves probes relative to their cell size
and is effectively off.

### Why mode 3 is over-smooth

`temporalAlpha=0.05` gives an EMA half-life of ~13.5 frames. With `jitterHoldFrames=1`
and the live `jitterPatternSize=4`, each jitter position is sampled for 1 frame before
moving to the next. For a proper N-tap uniform average, Phase 11 recommends `α ≈ 1/N`.
At N=4, that is `α=0.25`. Current `α=0.05` is 5× below the recommendation — the EMA
integrates each jitter position with far too little weight, producing an extremely
slow-converging average dominated by the distant past rather than the current 4-position
cycle.

### Why some GI seams are soft in mode 0

The bilateral GI blur (`gi_blur.frag`) has depth-sigma and normal-sigma edge stops but
no luminance/color edge stop. **Note:** the wall/ceiling corner specifically is NOT a
bilateral failure — those surfaces have perpendicular normals (dot product = 0), so the
existing normal edge-stop already strongly downweights samples across that seam. The
softness at the wall/ceiling corner is a probe-interpolation issue (trilinear
C1-discontinuity, "Type A" banding, documented in `phase9c_probe_spatial_banding.md`),
not addressable by the bilateral.

What the bilateral CAN miss: within-plane tonal transitions (e.g. a bright bounce
highlight fading to darker GI on the same flat surface, at the same depth and normal).
The depth and normal stops provide no signal at these intra-surface boundaries. A
luminance edge-stop would preserve these transitions.

---

## Part 13a — Parameter Retuning (quality, zero new code)

**What:** Change constructor defaults for three jitter/temporal members.

**Why:** Phase 11 shipped these sliders but left the defaults at pre-Phase-11 values.
Retuning to Phase 11's own recommended starting point is the highest-leverage, zero-risk
quality improvement available.

### Default changes (`src/demo3d.cpp` constructor)

| Member | Live default | New default | Rationale |
|---|---|---|---|
| `probeJitterScale` | `0.05f` | `0.25f` | Phase 11 recommended minimum; ±0.125 cell, enough to break grid aliasing |
| `temporalAlpha` | `0.05f` | `0.20f` | Close to `1/N=0.25` at N=4; slightly conservative to reduce noise with higher jitter |
| `jitterHoldFrames` | `1` | `2` | Dwell 2 frames per position; 4 positions × 2 frames = 8-frame cycle, matching stagger=8 interval |

`jitterPatternSize` stays at 4. With dwell=2 the full pattern cycles in 8 frames —
aligned with the stagger-8 schedule so every cascade has a chance to rebuild at each
jitter position before the position changes.

**Expected effect on burst analysis findings:**

| Finding | Expected improvement |
|---|---|
| Blocky ~50–80px probe-grid banding (mode 6) | Spatial jitter at ±0.125 cell dithers the grid pattern; 8-position pattern cycles through it |
| Mode 3 over-smooth / washed-out shadows | α=0.15 with jitter suppression: faster contrast recovery without ghosting |
| C0 surfPct 29% contributing to banding | Jitter moves probes into different sub-cell positions; spatial aliasing of the 29% hit pattern is broken |

**Risk:** higher jitter + faster alpha may introduce ghosting if scene changes. AABB history
clamp (`useHistoryClamp=true`, already ON by default) is the guard for this. Validate with
mode 6 steady-state: no persistent ghost patches after 20+ frames.

**Performance impact:** zero — these are CPU-side float defaults. GPU dispatch counts unchanged.

---

## Part 13b — Bilateral Luminance Edge-Stop (quality, shader + UI change)

**What:** Add a luminance difference term to the bilateral GI blur weight so it stops
blurring across within-plane tonal transitions.

**Why:** The analysis identified GI over-smoothing in mode 0. The wall/ceiling corner
seam is NOT the bilateral's fault — those normals are perpendicular, so the existing
`uNormalSigma` stop already strongly suppresses blur across that boundary. The seam
softness there is a probe-interpolation issue (Type A). What the bilateral CAN miss is
within-plane tonal variation: a bright GI bounce highlight fading to darker GI on a flat
wall, where depth and normal are identical across the blur kernel. The current weight:

```glsl
weight = depthWeight(depth_sigma) × normalWeight(normal_sigma)
```

gives no signal at intra-surface boundaries.

### Shader change (`res/shaders/gi_blur.frag`)

Add a uniform:
```glsl
uniform float uLumSigma;   // luminance edge-stop sigma; 0.0 = disabled
```

In the bilateral weight calculation, after the existing depth and normal terms:
```glsl
// Luminance edge-stop: prevent blurring across tonal discontinuities on coplanar surfaces
if (uLumSigma > 0.0) {
    float centerLum = dot(centerGI.rgb, vec3(0.299, 0.587, 0.114));
    float sampleLum = dot(sampleGI.rgb, vec3(0.299, 0.587, 0.114));
    float lumDiff   = centerLum - sampleLum;
    weight *= exp(-(lumDiff * lumDiff) / (2.0 * uLumSigma * uLumSigma));
}
```

`centerGI` and `sampleGI` are the GI radiance values being blurred — already available
in the bilateral pass.

### C++ uniform upload (`renderGIBlurPass()` or equivalent)

```cpp
glUniform1f(glGetUniformLocation(prog, "uLumSigma"), giBlurLumSigma);
```

### New member (`src/demo3d.h`)

```cpp
float giBlurLumSigma = 0.4f;   // 0.0 = disabled; lower = sharper tonal boundary
```

### UI (Settings panel, alongside existing Radius/DepthSigma/NormalSigma sliders)

```cpp
ImGui::SliderFloat("Lum sigma##blur", &giBlurLumSigma, 0.0f, 2.0f, "%.2f");
if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Bilateral GI blur luminance edge stop.\n"
                      "Lower = stops at tonal boundaries (sharper GI seams).\n"
                      "0.0 = disabled (same as pre-Phase-13).");
```

**Default `0.4f`:** stops blurring when GI luminance differs by ~0.4 (in the scene's
0–0.56 luminance range, this is about 70% of the max difference). Start at 0.4 and tune
lower if seams are still soft.

**Backward compat:** `uLumSigma=0.0` disables the term; bilateral is identical to Phase 9d.

**Performance impact:** 1 dot product + 1 exp per bilateral sample per pass. Negligible —
bilateral is ~0.064ms total.

---

## Part 13c — C0 Ray Coverage: Analysis and Defer

*(Note: the original 13c "Extended Stagger Range" has been removed. The stagger formula
`std::min(1 << i, staggerMaxInterval)` caps C3 at interval=8 regardless of the UI value
because `1 << 3 = 8`. Raising `staggerMaxInterval` past 8 has no runtime effect with 4
cascades. To truly extend C3's interval, the formula itself must change — e.g. a
per-cascade interval table. That is deferred to a future phase.)*

**What:** Document the C0 surfPct=29% root cause; decide whether to address structurally.

**Root cause:** C0 has `cellSize=0.125m` and probes fire rays up to approximately one cell
length. For probes in the interior of the 2×2×2 Cornell box (≥0.125m from any wall), the
rays are too short to reach a surface. This is by design — C0 captures very short-range
bounce; longer rays are handled by C1–C3.

**Effect:** 71% of C0 ray-probe pairs return sky/miss. These probes contribute ambient
rather than bounce. This is the main source of the probe-grid banding: the "hit" probes
form a sparse irregular shell near surfaces, creating a spatial pattern.

**Why jitter (13a) is the right fix for now:** With `probeJitterScale=0.25`, the shell
of hit probes is shifted differently each frame. The temporal EMA averages across these
shifted positions, spreading the hit-shell contribution more uniformly. This breaks the
grid-aligned banding without requiring a structural cascade change.

**Structural alternatives (deferred to Phase 14+):**

| Option | Benefit | Cost |
|---|---|---|
| Increase C0 resolution (32³ → 48³) | More probes near surfaces, better short-range coverage | 3.4× atlas memory, 3.4× bake cost for C0 |
| Expose C0 max ray distance as a UI parameter | Allow longer C0 rays for better surface coverage | Breaks cascade hierarchy assumptions; shader change |
| Tricubic interpolation for probe lookup | Smooth trilinear C1-discontinuity (Type A banding) | 64 fetches vs 8; ~8× raymarch cost at this step |

The tricubic option was already noted in the project memory as a known deferral. The
C0 resolution option is the next most impactful but requires measuring memory/cost trade-off
with a profiler burst run at 48³.

**Action for Phase 13:** document only. No code change. Phase 14 plan item: run burst
analysis at `probeJitterScale=0.25` after 13a lands, compare C0 surfPct distribution
in mode 6; decide whether resolution increase is warranted.

---

## Expected Burst Analysis After Phase 13a+b

After implementing 13a (retuning) and 13b (luminance edge-stop), re-run burst capture
and expect:

| Mode | Current rating | Target rating |
|---|---|---|
| 0 (Final) | Good | Excellent (sharper seams, GI still smooth) |
| 3 (Indirect ×5) | Good | Good→Excellent (faster convergence, contact shadows sharper) |
| 6 (GI only) | Fair | Good (banding softened by jitter; probe footprint less visible) |

---

## Implementation Order

1. **13a**: Change three constructor defaults in `demo3d.cpp` — `probeJitterScale` 0.05→0.25, `temporalAlpha` 0.05→0.20, `jitterHoldFrames` 1→2
2. **13b**: Add `uLumSigma` to `gi_blur.frag`; add `giBlurLumSigma` member + upload + UI slider
3. Run burst analysis to validate (compare against `frame_17777313678792269.md`)

---

## Files Changed

| File | Change |
|---|---|
| `src/demo3d.h` | + `giBlurLumSigma = 0.4f` |
| `src/demo3d.cpp` constructor | `probeJitterScale=0.25`, `temporalAlpha=0.20`, `jitterHoldFrames=2` |
| `src/demo3d.cpp` GI blur pass | `glUniform1f(..., "uLumSigma", giBlurLumSigma)` |
| `res/shaders/gi_blur.frag` | `+ uniform float uLumSigma`; luminance weight term in bilateral loop |

---

## References

| Document | What it covers |
|---|---|
| `tools/frame_17777313678792269.md` | Source burst analysis — findings that drive this plan |
| `tools/probe_stats_17777313678792269.json` | Raw stats at time of analysis |
| `doc/cluade_plan/AI/phase9d_gi_blur_mode_fixes.md` | Bilateral blur implementation (Phase 9d): FBO two-pass, depth+normal sigma, `gi_blur.frag` |
| `doc/cluade_plan/AI/phase10_temporal_perf.md` | Fused EMA, staggered cascade updates, why cascade cost is 0.091ms |
| `doc/cluade_plan/AI/phase11_jitter_dirres_fixes.md` | probeJitterScale, jitterPatternSize, jitterHoldFrames design; recommended starting points |
| `doc/cluade_plan/AI/phase12b_burst_capture_impl.md` | Burst capture implementation (the tool used to generate the source evidence) |
| `doc/cluade_plan/AI/phase9c_probe_spatial_banding.md` | Type A/B banding diagnosis; why tricubic is deferred |
