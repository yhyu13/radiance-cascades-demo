# Phase 9 — Code Improvement Plan

**Date:** 2026-05-02
**Updated:** 2026-05-02 (after review 13 + E4 results)
**Reference images:**
- C0 res=32 baseline: `tools/frame_17776513476570812.png`
- C0 res=8:  `tools/frame_17776724238845040.png`
- C0 res=64: `tools/frame_17776724345225716.png`

---

## Banding diagnosis: CONFIRMED

E4 has been run. Results:

| cascadeC0Res | Probe spacing | Band spacing | GI quality |
|---|---|---|---|
| 8³ | 50 cm | No structure (too sparse) | Very wrong |
| 32³ (baseline) | 12.5 cm | ~12.5 cm, rectangular | Usable |
| 64³ | 6.25 cm | ~6.25 cm, elliptical | Good |

**Band spacing halved when probe spacing halved** → spatial aliasing is the dominant
cause. The pattern changed from rectangular (C0 grid-dominated at 32³) to elliptical
(following true point-light iso-luminance shells at 64³), showing the artifact
accurately tracks probe spacing.

**Brute-force density increase does not converge.** At 64³ the banding persists at
half the frequency. Going to 128³ would be 64× the original cost and still band.
The GI gradient near a point light scales as 1/r² — it is arbitrarily steep near
the source and no fixed probe grid eliminates banding by density alone.

**All other candidates eliminated:**

| Eliminated cause | How tested |
|---|---|
| Display-path step quantization | Mode 7 smooth |
| SDF texture trilinear | Analytic SDF toggle: unchanged |
| Bake ray step snapping | 0.01→0.001: unchanged |
| Angular bin resolution (D4) | dirRes 4→8: unchanged |

---

## Improvement A — Fix 3D JFA SDF generation

### What is broken

`res/shaders/sdf_3d.comp` has three fundamental bugs and is never dispatched
(the demo uses `sdf_analytic.comp` exclusively, `analyticSDFEnabled` defaults true).

1. **No seed position propagation** — JFA propagates seed *positions*, then computes
   `length(self - seed_pos)` fresh. The Voronoi buffer (`oVoronoi`) that holds
   seed positions is fully commented out. `closestPos` is initialized to `pos` and
   never updated on passes 2+, so `length(pos - pos) = 0` everywhere → all voxels
   get distance 0 after the first pass.

2. **Wrong first-pass init** — Any voxel within `uMaxStep` of a surface gets
   `minDist = 0.0` because the neighbor `neighborDist` is set to 0.0 and compared
   against INF. Only actual surface voxels should start at 0.

3. **No sign computation** — Header says "signed distance" but no inside/outside
   determination exists. Output is always ≥ 0.

### Correct 3D JFA pseudocode

```
Init pass:
  if surface_voxel: store seed_pos=self (oVoronoi.w=1), dist=0
  else:             store seed_pos=invalid (oVoronoi.w=0), dist=INF

JFA passes (step=N/2, N/4, …, 1):
  for 27 neighbors at ±step:
    load neighbor.seed_pos from oVoronoi (if .w > 0)
    dist = length(vec3(self) - neighbor.seed_pos)
    if dist < best: update best_dist, best_seed
  write best to oVoronoi + oSDF

Sign pass (optional):
  flood-fill from outside boundary to mark interior voxels negative
```

### Does fixing JFA help the banding?

**No expected benefit for the current analytic scene.** The demo uses
`sdf_analytic.comp` (exact, zero quantization). The analytic SDF toggle test showed
banding persists even with perfect display-path SDF — the banding is in the probe
GI data, not in SDF quality. JFA fix enables future mesh geometry support only.

**Priority: Medium — code correctness for future mesh scenes, not a banding fix.**

---

## Improvement B — Temporal probe accumulation

### What it does and does NOT do

After each bake, blend the new atlas into a persistent history buffer:

```glsl
// temporal_blend.comp
history = mix(history, current_bake, uAlpha)   // alpha ≈ 0.05–0.1
```

**B without jitter (Improvement C): suppresses bake noise, NOT spatial aliasing.**

The banding artifact is deterministic — the same scene, same probe positions, same
ray directions produce the same probe values every rebuild. Accumulating identical
deterministic samples converges to the same biased result, not to the true GI field.

B alone is a **stability / noise stabilizer**, not a banding fix.

**B + C together (jitter): directly addresses spatial aliasing.** Each frame's probes
sample at slightly different world positions. The accumulated average integrates over
a wider spatial footprint than one fixed-grid bake, effectively increasing the spatial
sampling rate without adding probes. This is the production DDGI/Lumen approach.

### Implementation

**New resources per cascade:**
- `probeAtlasHistory`: `RGBA16F`, same dims as `probeAtlasTexture` (`(res*D)²×res`)
- `probeGridHistory`: `RGBA16F`, same dims as `probeGridTexture` (`res³`)

**New shader:** `res/shaders/temporal_blend.comp`

```glsl
layout(rgba16f, binding = 0) uniform image3D oHistory;  // R+W: running average
layout(rgba16f, binding = 1) uniform image3D uCurrent;  // R:   fresh bake output
uniform float uAlpha;
uniform ivec3 uSize;

void main() {
    ivec3 c = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(c, uSize))) return;
    vec4 cur = imageLoad(uCurrent, c);
    vec4 his = imageLoad(oHistory, c);
    imageStore(oHistory, c, mix(his, cur, uAlpha));
}
```

**CPU side (demo3d.cpp):**
- `initCascades()`: allocate history textures alongside existing atlases
- `updateSingleCascade()`: after bake+reduction, if `useTemporalAccum`, dispatch
  `temporal_blend.comp` for both atlas history and grid history
- `raymarchPass()`: when `useTemporalAccum`, bind history textures instead of
  fresh bake textures for both `uDirectionalAtlas` and `uRadiance`
- Reset: history textures are zeroed on `initCascades()` (called on structural changes)

**New UI controls:**
- Checkbox: "Temporal accumulation"
- Slider: "Temporal alpha" [0.01, 1.0] (1.0 = no accumulation)
- Checkbox: "Probe jitter" (requires accumulation to be useful)

**Priority: High (as part of B+C combined). B alone: Medium.**

---

## Improvement C — Stochastic probe jitter

### What it does

Each bake, offset all probe world positions by a sub-cell random jitter. Combined
with temporal accumulation (B), the running average integrates probes at many
slightly different world positions.

```glsl
// radiance_3d.comp — probeToWorld() modified:
vec3 probeToWorld(ivec3 probePos) {
    return uGridOrigin + (vec3(probePos) + 0.5 + uProbeJitter) * uProbeCellSize;
}
// uProbeJitter: uniform vec3 in [-0.5, 0.5]³ (probe-cell units), random each bake
```

**CPU side:**
- `uniform vec3 uProbeJitter` added to `radiance_3d.comp`
- Before each bake dispatch, generate random jitter in [-0.5, 0.5]³ (one vector per
  full rebuild cycle, same for all cascade levels)
- When `useProbeJitter` is false: send `vec3(0.0)` (no effect)

### Does B+C together help banding?

**Yes — the strongest available fix without architectural changes.** This is the
standard production approach for probe-grid GI aliasing (DDGI, RTXGI). Expected
result with alpha=0.1 and ~30 rebuild cycles: the concentric banding should soften
substantially as the temporal average integrates multiple spatial offsets.

**Priority: High — implement together with B.**

---

## Improvement D — Probe density (E4, already tested)

Running `cascadeC0Res` 32→64 halved the band spacing as predicted. It confirmed
the spatial aliasing hypothesis but did not eliminate banding. Higher density costs
8× VRAM and still bands at a finer scale. This approach alone does not converge.

**Recommended operating point:** 32³ (current default) for development. 64³ for
higher quality at a cost. Do not pursue 128³ — the B+C approach is more effective.

---

## Improvement E — DDGI visibility-weighted probe interpolation

### What it does

Weight the 8 trilinear probe corners by visibility probability (Chebyshev test
against per-probe mean/variance of bake ray hit distances). Prevents GI bleeding
from probes on the other side of walls.

### Why it requires architectural changes

The current branch has **none** of the required infrastructure:

- No `probeDepthMoments` volume (needs new `RG16F` 3D texture per cascade)
- No mean/variance accumulation in `radiance_3d.comp`
- No new texture binding or access path in `raymarch.frag`
- `sampleDirectionalGI()` must be restructured to accept and apply per-probe weights

This is a significant architectural addition, not a local tweak.

### Does it help banding?

**Partially.** DDGI visibility weighting is most effective for preventing light
bleeding through thin walls. The Cornell Box banding is primarily spatial aliasing
near the point light — not inter-probe leaking. E would help near geometry edges
but is unlikely to reduce the broad iso-contour banding seen in the screenshots.

**Priority: Low — revisit after B+C are validated.**

---

## Summary table

| Improvement | Targets banding | Expected banding reduction | Code cost | Priority |
|---|---|---|---|---|
| **B+C: Temporal + jitter** | Yes — directly | High (production standard fix) | Medium | **High — implement now** |
| **B alone** | Partly (noise only) | Low | Medium | Medium (useful with C) |
| **C alone** | No (adds noise) | Negative | Low | Only with B |
| **D: E4 density 32→64** | Partly | Bands halve, don't disappear | Zero (UI) | Tested — not sufficient alone |
| **A: Fix 3D JFA** | No (current scene) | None for analytic scene | Medium | Medium (code quality) |
| **E: DDGI visibility** | Partly | Low-medium (leaking) | High (architectural) | Low |

---

## Recommended execution order

```
Step 1 (done):  E4 run → spatial aliasing confirmed as dominant cause
Step 2 (now):   Implement B+C together
                - temporal_blend.comp (new shader)
                - probeAtlasHistory + probeGridHistory per cascade
                - uProbeJitter in radiance_3d.comp
                - UI: enable toggle + alpha slider + jitter toggle
                Test: with alpha=0.1 + jitter ON, rebuild ~30 cycles.
                      Bands should soften significantly on back wall.
Step 3:         Fix 3D JFA (A) — code quality, not banding
Step 4:         DDGI visibility (E) — only if leaking artifacts appear
```

---

## What will NOT help (tested or architecturally irrelevant to current banding)

| Approach | Why not |
|---|---|
| More bake ray steps / min step | Tested — banding unchanged. Probe DATA is the issue |
| Display-path SDF quality | Analytic SDF toggle tested — unchanged. Display path is not the source |
| Angular resolution D8/D16 | Tested — unchanged. Banding is spatial, not angular |
| B alone (no jitter) | Deterministic probe positions → same biased accumulation |
| 128³ probes | 64× cost, bands still present at finer scale. B+C is more effective |
