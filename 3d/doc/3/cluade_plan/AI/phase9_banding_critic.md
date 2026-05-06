# Phase 9 Self-Critique: Why Banding Persists

**Date:** 2026-05-02
**Trigger:** Banding still visible on Cornell Box inner boxes after temporal accumulation +
probe jitter (Phase 9). Question: is the math wrong? What threshold eliminates it?

---

## Short answer

The Phase 9 math is not wrong. But it is solving the wrong problem for the banding
the user sees on the box faces. There are **two distinct banding types** in this renderer.
Phase 9 addresses one of them. The other requires a different fix entirely.

---

## Two banding types — not the same problem

### Type A: Spatial banding (probe-grid stepping)

**Cause:** Probe grid at 32³, cellSize=0.125 world units. The trilinear interpolation in
`sampleDirectionalGI()` creates a visible gradient at each probe-cell boundary. Surfaces
near the center of a probe cell look different from surfaces near its edge.

**What Phase 9 targets:** Jitter shifts all probes by a shared random vector each rebuild.
The EMA history integrates samples from slightly different world positions. In the limit
this converges to a box-filter of the radiance field over one probe cell — smoothing the
cell-to-cell stepping artifact.

**Verdict:** Phase 9 correctly addresses spatial banding. It just takes ~44 frames at
alpha=0.1 to converge to the filtered result (see convergence table in `phase9_temporal_dark_bugfix.md`).

### Type B: Directional banding (angular bin quantization)

**Cause:** Each probe stores D×D directional bins via octahedral mapping. With D=4, each
probe has 4×4=16 bins, covering the full sphere. The angular step per bin is roughly:

```
360° / 4 = 90° per octahedral row
```

In practice the octahedral mapping is not uniform solid angle, but the visible bins from
the upper hemisphere (roughly 8-12 bins) cover ~30-45° each.

**Symptom:** A box face that happens to straddle the boundary between two directional bins
(e.g., the light direction shifts from bin [1,2] to bin [2,2] as you cross the face) shows
a hard color step. The step follows the probe grid's directional quantization, not its
spatial position.

**What Phase 9 does to this:** Nothing. Jitter moves probes in world space. It does not
change the directional binning. A probe at position X+jitter still has exactly the same
16 bins in the same 16 angular directions as a probe at position X. The directional
banding is structural — it requires changing D or the basis function.

**Verdict: the banding on the Cornell Box inner boxes is Type B, not Type A.** This is
why Phase 9 does not eliminate it regardless of alpha or convergence.

---

## Is the jitter math wrong?

**The jitter generation** (`demo3d.cpp:1073-1075`):
```cpp
static std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
currentProbeJitter = glm::vec3(dist(rng), dist(rng), dist(rng));
```
Correct. Uniform, independent per axis, symmetric around 0.

**The jitter application** (`radiance_3d.comp:82`):
```glsl
return uGridOrigin + (vec3(probePos) + 0.5 + uProbeJitter) * uProbeCellSize;
```
Correct. Probe (i,j,k) samples at `gridOrigin + (i + 0.5 + jitter.x) * cellSize`.

**The display-side mismatch** (`raymarch.frag:300`):
```glsl
vec3 pg = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5, ...);
```
This trilinear reads probe (i,j,k) as if it were at `gridOrigin + (i + 0.5) * cellSize`.
It does NOT account for the jitter offset used in the bake.

**Is this a bug?** Not exactly. On any single frame the trilinear weights are off by
`jitter * cellSize` in world space — a small error. But across EMA frames this averages
out because jitter is symmetric around 0. The converged EMA value is:

```
E_j[radiance(probe_i + j)] = integral_{-0.5}^{0.5} radiance((i + 0.5 + t) * cellSize) dt
```

This is the **box-filter** of the radiance field over one probe cell. The display then
trilinears between box-filtered values. This is mathematically sound — just not the same
as the true radiance at the probe center.

**Summary:** The jitter math is not wrong. The architecture (jitter-in-bake, unaccounted-
in-display) is the standard approach for temporal antialiasing and converges correctly.

---

## Why alpha=0.1 does not eliminate banding

Alpha controls convergence speed, not the converged result. All alpha values (0 < alpha < 1)
converge to the same limit: the box-filtered radiance averaged over the probe cell.

| alpha | Convergence half-life | Effective samples |
|---|---|---|
| 0.1  | ~6.6 frames | ~10 |
| 0.05 | ~13.5 frames | ~20 |
| 0.01 | ~69 frames | ~100 |

`N_eff ≈ 1/alpha` — lower alpha → more effective samples in the EMA, smoother estimate,
but slower temporal response to scene changes.

**The converged value at any alpha is the box-filtered radiance.** Even with infinite frames
at alpha→0, the result is not the true radiance field at probe centers — it is the spatial
average over one cell. This is better than a single sample but still has probe-cell-boundary
discontinuities in its first derivative (C0 but not C1).

**For directional banding (Type B), no alpha eliminates it.** The converged history
texture has nothing to do with directional quantization — it stores the EMA of isotropic
and directional atlas values, and the directional bins within those atlases never change.

---

## Global jitter vs per-probe jitter

Current implementation: **one shared jitter vector for all probes per rebuild.**

This is the correct approach for temporal antialiasing. All probes shift together, so the
grid topology is preserved — adjacent probes still cover adjacent, non-overlapping cells.
The trilinear interpolation between them remains valid (just offset from the nominal grid).

**Per-probe independent jitter** would break the topology: adjacent probes would sample
arbitrary non-adjacent world positions, making the trilinear blend in `sampleDirectionalGI()`
undefined. This would not reduce banding — it would just make the GI spatially incoherent.

**The global-shift approach is correct.**

---

## What threshold completely eliminates banding?

There is no alpha value that eliminates banding. But there ARE parameters that control it:

### For spatial banding (Type A)

| Parameter | Effect | Complete elimination? |
|---|---|---|
| Lower alpha | Slower convergence, smoother | No — converged value is still box-filtered |
| Higher probe resolution (32→64³) | Halves cell size, halves banding scale | Reduces; never fully eliminates |
| Stratified jitter (Halton sequence) | Faster convergence, lower variance | Reduces |

The only complete cure for spatial banding is infinite probe resolution.

### For directional banding (Type B) — the actual box-face banding

| Parameter | Effect | Complete elimination? |
|---|---|---|
| Increase D (4→8) | 64 bins instead of 16, ~22.5° steps instead of ~45° | Reduces by 4× |
| Increase D (4→16) | 256 bins, ~11° steps | Nearly eliminated perceptually |
| Spherical harmonics (L2) | 9 coefficients, C∞ smooth angular function | Yes — eliminates discrete steps |
| Screen-space GI blur | Post-process blur, hides probe-grid and angular artifacts | Pragmatic hide |

**D is the lever that matters for the Cornell Box box faces.** Phase 9 (temporal+jitter)
does not touch D.

---

## What the user is seeing — diagnosis

The "banding still on box" is angular bin boundary artifacts (Type B). Evidence:

1. The banding appears on the surfaces of the inner Cornell Box boxes — flat faces with
   uniform normals. The normal is constant across the face, but as the camera moves or
   as different parts of the face lie in the probe's Voronoi cell, the quantized bin
   changes.

2. The banding is not at the probe grid frequency (0.125 world units = ~3% of scene).
   It appears as broader color steps on the box faces — consistent with ~45° angular bins.

3. Temporal accumulation + jitter ran for enough frames to converge spatially (the wall
   banding improved as noted in the user screenshot). The box banding persisted —
   confirming it is not spatial.

4. Jitter shifts probe world positions. It does not change which direction bin receives
   the light. A probe 5cm away from the nominal position still classifies the light
   direction into the same bin. So EMA over jittered frames still shows the same bin
   boundaries.

---

## Convergence analysis for spatial banding only

With alpha=0.1, random jitter ∈ U[-0.5, 0.5]³, N frames accumulated:

```
history_N = alpha * sum_{k=0}^{N-1} (1-alpha)^k * current_{N-k}
```

Expected squared error from box-filter estimate:
```
E[||history_N - box_filter||²] = (1-alpha)^{2N} * ||history_0 - box_filter||² + alpha²/(2-alpha) * sigma²
```

where sigma² is the variance of radiance over the probe cell (how much radiance varies
within one cellSize).

**Residual geometric series term**: at N=22 frames with alpha=0.1: `0.9^44 ≈ 0.01` —
old data contributes less than 1% of the residual error. After ~44 frames the estimate
has essentially converged to the box-filtered radiance.

**Noise floor**: `alpha²/(2-alpha) * sigma² ≈ 0.01/1.9 * sigma² ≈ 0.005 * sigma²` —
the EMA estimate has about 0.7% relative standard deviation from the box-filter value
(0.005 = relative variance → sqrt ≈ 7% relative std for sigma/mean = 1 case).

This noise floor is irreducible with this alpha — it's the trade-off of using an EMA
instead of a running sum. Lower alpha → lower floor, slower convergence.

---

## Summary of findings

| Claim | Verdict |
|---|---|
| Jitter math is wrong | **No** — uniform random in [-0.5,0.5]³, correctly applied |
| Display-side mismatch is a bug | **Not exactly** — it introduces per-frame bias that EMA averages out |
| alpha=0.1 too high to converge | **No** — the issue is not convergence speed but what it converges TO |
| Temporal+jitter eliminates box banding | **No** — that banding is directional (Type B), jitter doesn't address it |
| Some alpha value eliminates banding | **No** — alpha controls speed, not the converged value |
| Phase 9 is solving the right problem | **Partially** — correct for spatial banding, wrong tool for directional |

---

## Recommended fixes for persistent banding

### Immediate (single parameter change)

Increase `cascadeDirRes[0]` from 4 to 8. This changes the directional atlas from
(32×4)²×32 = 128²×32 to (32×8)²×32 = 256²×32 — 4× more bins, 4× finer angular
resolution. Banding on box faces should decrease dramatically. Cost: 4× bake atlas
storage and 4× bake compute time for C0.

### Correct (architectural)

Replace octahedral D×D bins with SH coefficients (L1=4, L2=9 per probe). This gives
a smooth angular basis with no discrete steps. The sampleProbeDir() becomes an SH
evaluation, which is 9 MADs instead of D² texture fetches. Much cheaper for high D.

### Pragmatic (display-side)

Screen-space bilateral blur on the indirect GI term. Cheap, immediate, does not require
rebaking. Blurs both Type A and Type B artifacts. Downside: smears fine GI detail.

---

## What Phase 9 is actually good for

Phase 9 is the correct implementation for **spatial antialiasing of probe-grid stepping**.
In scenes where the radiance field is spatially varying at the probe-cell scale (different
lighting on adjacent cells), the EMA of jittered bakes removes the step-function
appearance and replaces it with a smooth box-filtered gradient.

The Cornell Box has large uniform color regions (wall faces). Spatial banding there is
minimal because the radiance field is nearly constant across many probe cells. The banding
that IS visible is directional — driven by the 16-bin angular quantization, not spatial.

Phase 9 is therefore most useful in scenes with complex spatial radiance variation (multiple
light sources, complex geometry creating varied shadows). For the Cornell Box demo, fixing
D is more impactful.
