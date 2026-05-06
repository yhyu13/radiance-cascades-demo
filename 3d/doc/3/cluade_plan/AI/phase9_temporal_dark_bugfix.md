# Phase 9 — Temporal Dark Bug: Root Cause & Fix

**Date:** 2026-05-02
**Symptom:** Enabling "Temporal accumulation" makes GI fully dark. Direct lighting
remains visible; only indirect (probe-sampled) GI is zero.

---

## Diagnosis method

### Observed runtime symptom
RDC thumbnail extracted via `renderdoccmd.exe thumb` — confirmed direct lighting
present, GI absent. Not a render-path crash; specifically, probe texture reads return
zero. (The Python RDC API was unavailable: RenderDoc ships Python 3.6, incompatible
with the system Python.)

### Code-proven cause
Traced display reads to `probeGridHistory` / `probeAtlasHistory` when
`useTemporalAccum == true`. Traced when `temporal_blend.comp` dispatch fires: inside
`updateSingleCascade()`, only when `updateRadianceCascades()` runs, only when
`cascadeReady == false`. Inspected the `update()` change-detector block — no entry
exists for `useTemporalAccum`. Confirmed: toggling temporal never sets
`cascadeReady = false`.

### Follow-on design consequence
Even with Bug 1 fixed (one warm-up rebuild fires), a static scene accumulates only
one EMA sample unless something keeps `cascadeReady = false` every frame. This is
Bug 2, a separate convergence failure described below.

---

## Bug 1 — Dark-screen symptom: history never populated

**Failure class:** display reads zero-initialized texture before it is ever written.

**Root cause:** `update()` contains a list of change-detectors that set
`cascadeReady = false` to trigger a cascade rebuild. `useTemporalAccum` is absent
from this list.

**Timeline when temporal is enabled for the first time:**
```
Frame N:   user enables "Temporal accumulation" checkbox
           useTemporalAccum = true
           cascadeReady still true (no trigger exists)
           raymarchPass() reads probeGridHistory (zero at allocation) → dark GI
           updateRadianceCascades() NOT called (cascadeReady == true)
           → temporal_blend.comp never fires
           → probeGridHistory stays zero
Frame N+1: same → still dark
...
```

The scene is static. Nothing else changes. History stays zero forever.

**Fix:** detect `useTemporalAccum` toggle and force one warm-up rebuild:
```cpp
static bool lastTemporalAccum = false;
if (useTemporalAccum != lastTemporalAccum) {
    lastTemporalAccum = useTemporalAccum;
    if (useTemporalAccum) cascadeReady = false;  // warm-up on first enable
}
```

---

## Bug 2 — Temporal convergence failure: not enough rebuilds in static scene

**Failure class:** temporal accumulation design works correctly per-rebuild, but the
rebuild never fires again after the warm-up in a static scene.

**Separate from Bug 1:** Bug 1 causes fully dark GI. Bug 2 causes temporal+jitter to
accumulate only one sample — never converging to the ~22-sample average needed to
soften banding. Even with Bug 1 fixed, a user enabling temporal+jitter in a static
scene would see GI warm up to ~10% brightness after one rebuild, then stop improving.

**Root cause:** with jitter ON, each rebuild samples probes at a different world
position. The EMA integrates these over time. But `cascadeReady` becomes `true` after
the first rebuild and stays `true` in a static scene — so no further rebuilds fire,
no new jitter positions are sampled, and history holds a single `alpha * current` value
indefinitely.

**Fix:** force `cascadeReady = false` every frame when both temporal and jitter are
active, plus a rebuild on jitter toggle:
```cpp
static bool lastProbeJitter = false;
if (useProbeJitter != lastProbeJitter) {
    lastProbeJitter = useProbeJitter;
    cascadeReady = false;
}
if (useTemporalAccum && useProbeJitter) {
    cascadeReady = false;  // continuous rebuild to accumulate distinct jitter samples
}
```

**Why not continuous rebuild without jitter:** accumulating identical samples
(same probe positions every frame) converges to the same biased result as a single
bake. No banding improvement, just wasted GPU time. The continuous-rebuild path is
gated on `useProbeJitter` only.

---

## Combined fix — inserted before `if (!cascadeReady)` in `update()`

```cpp
// Phase 9: temporal accumulation rebuild triggers.
static bool lastTemporalAccum = false;
if (useTemporalAccum != lastTemporalAccum) {
    lastTemporalAccum = useTemporalAccum;
    if (useTemporalAccum) cascadeReady = false;  // Bug 1 fix: warm-up on first enable
}
static bool lastProbeJitter = false;
if (useProbeJitter != lastProbeJitter) {
    lastProbeJitter = useProbeJitter;
    cascadeReady = false;
}
if (useTemporalAccum && useProbeJitter) {
    cascadeReady = false;  // Bug 2 fix: continuous rebuild for jitter accumulation
}
```

---

## Convergence after fix

With `alpha=0.1` and jitter ON, after the warm-up rebuild:

| Frame after enable | History brightness | Band softening |
|---|---|---|
| 1  | 10% | none yet |
| 5  | 41% | beginning |
| 10 | 65% | moderate |
| 22 | 90% | substantial |
| 44 | 99% | near-converged |

---

## Why direct lighting was unaffected

`raymarch.frag` computes direct lighting (shadow ray + Lambertian) using `uSDF` and
`uLightPos` only, independent of probe textures. Probe textures (`uRadiance`,
`uDirectionalAtlas`) contribute only the indirect GI term. Switching to zero-initialized
history textures killed only the indirect term — matching the symptom exactly.

---

## Follow-on fix: `readonly` qualifier in `temporal_blend.comp`

During analysis, a secondary spec violation was identified:

```glsl
// before fix:
layout(rgba16f, binding = 1) uniform image3D uCurrent;

// after fix:
layout(rgba16f, binding = 1) readonly uniform image3D uCurrent;
```

The C++ binds `uCurrent` as `GL_READ_ONLY`. The OpenGL spec says using imageLoad on
an image uniform NOT declared `readonly` when bound as `GL_READ_ONLY` is undefined
behavior. On conformant drivers the imageLoad produces correct results; on stricter
implementations it may silently return zero — which would compound Bug 1 and make
the root cause harder to isolate.

**Not the current leading cause** of the dark-screen symptom (Bug 1 alone fully
explains it). But it is a real spec violation worth fixing for correctness.

---

## Eliminated-candidates table (updated)

| Candidate | Status |
|---|---|
| `temporal_blend.comp` compile failure | Eliminated — `loadShader` prints errors on failure; shader GLSL is valid 4.3 |
| `probeGridHistory` texture incomplete (mipmap) | Eliminated — `gl::createTexture3D` defaults to `GL_LINEAR`; verified in `setTexture3DParameters` |
| Image format mismatch (`GL_RGBA16F`) | Eliminated — all textures and image bindings use `GL_RGBA16F` consistently |
| Uniform `uAlpha` not set (`glGetUniformLocation` returns -1) | Eliminated — uniform names match exactly between shader and C++ query |
| Uniform `uSize = (0,0,0)` not set | Eliminated — same |
| Memory barrier ordering | Eliminated — barrier after reduction covers atlas and grid writes; barrier between atlas and grid blend dispatches present |
| `atlasAvailable` false (no atlas binding) | Eliminated — condition checks `probeAtlasTexture != 0`, not history; remains true |
| `GL_READ_ONLY` / `readonly` mismatch | **Not the current leading cause** — Bug 1 alone explains the symptom. But this is spec-defined UB; fixed by adding `readonly` to `uCurrent` in `temporal_blend.comp`. |
