# 02 — Probes and the Cascade Hierarchy

**Purpose:** Understand what probes are, why we have 4 cascade levels, and how the
intervals are derived.

---

## What is a probe?

A probe is a point in 3D space that fires rays in all directions to measure how much
light arrives from each direction. Think of placing a tiny 360° camera at that point
and recording what it sees.

In our system, probes do not render to screen. They are baked ahead of time (or after
each scene change) and the result is stored in a texture. The final render (`raymarch.frag`)
then just reads from that pre-baked texture to get GI at any surface point.

### Why a grid of probes?

One probe at the center of the scene would capture average lighting but miss local
effects (a surface right next to a green wall should be green-tinted; a surface far away
should not). By placing probes in a 3D grid, each surface point interpolates between
its nearest probes and gets spatially appropriate GI.

---

## The probe grid

We place 32×32×32 = 32,768 probes in the 4 m × 4 m × 4 m scene.
Probe spacing: 4 m / 32 = **0.125 m** = 12.5 cm.

Probe at grid index (px, py, pz) sits at world position:
```
worldPos = gridOrigin + (probeIndex + 0.5) × cellSize
         = (-2, -2, -2) + (px+0.5, py+0.5, pz+0.5) × 0.125
```

The +0.5 means probes sit at cell centers, not corners.

---

## Why probes can't see everything in one pass

A probe that tried to march a ray to infinity would spend most of its computation on
far-away empty space and would still not converge cleanly. The solution is to give each
probe a **bounded interval** [tMin, tMax] — it only looks between tMin and tMax meters
from its center.

Near-field probes need fine spacing (small intervals, many probes).
Far-field probes can be coarser (large intervals, fewer probes).

This is the **cascade** idea: divide the full distance range into 4 bands, use a separate
set of probes for each band.

---

## The 4 cascade levels

**Base interval** `d = 0.125 m` (= 4 m / 32 probes = one probe cell width).

| Level | tMin | tMax | Range |
|---|---|---|---|
| C0 | 0.02 m | d = 0.125 m | ~0–12.5 cm (near contact) |
| C1 | d | 4d = 0.5 m | 12.5–50 cm (short bounce) |
| C2 | 4d | 16d = 2.0 m | 50 cm–2 m (mid range) |
| C3 | 16d | 64d = 8.0 m | 2–8 m (far field, ceiling, sky) |

The tMax of level i equals the tMin of level i+1. The bands are contiguous, covering
0 m to 8 m with no gaps.

The tMax multiplier is 4× per level: 0.125 → 0.5 → 2.0 → 8.0.

**Why 4×?** Doubling gives 8 levels to cover the same range — unnecessary overhead.
4× means each level covers 4× the distance of the previous: C0 sees 0.125 m,
C3 sees 8 m (64×), covering the full 4 m scene with room to spare.

---

## The merge — how the levels connect

Each cascade level marches rays within only its own [tMin, tMax] band. So:
- C0 can tell you what it sees at 0–12.5 cm.
- C0 cannot tell you what color comes from 5 meters away.

The **merge** fixes this. When C0's ray misses (no surface in 0–12.5 cm), it asks C1
"what did you see in direction D?" C1 similarly pulls from C2 when its rays miss, and
so on.

The merge runs from coarsest to finest, **before** C0 is used by the renderer:
```
C3 baked first (no upper — sees infinity)
C2 baked with C3 data merged in
C1 baked with C2 data merged in
C0 baked with C1 data merged in
→ C0 now carries full 0–8 m radiance per direction
```

After baking, the renderer only reads from C0. C0 contains the accumulated answer from
all 4 levels.

---

## What "baking" means physically

For each probe, for each direction, we:
1. Start a ray at the probe's world position.
2. Sphere-march through the SDF in that direction, from tMin to tMax.
3. If we hit a surface: compute direct light at that surface (albedo × light × shadow).
4. If we miss: pull from the upper cascade (C1's stored value for that direction).
5. Store the result in the probe's storage.

The stored result per probe per direction is a radiance color (RGB). That's the
"baked" value.

---

## From baking to the final GI texture

After all 4 cascades are baked, each C0 probe has radiance values for every direction.
The **reduction pass** averages all direction values into one color per probe and writes
it to `probeGridTexture` (32³ RGBA16F).

`raymarch.frag` then reads indirect GI via one of two paths:

- **Isotropic path** (default): `texture(uRadiance, uvw)` — hardware trilinear
  sample from `probeGridTexture`. Spatially smooth (8-probe blend by the GPU sampler)
  but directionally averaged (all hemisphere directions contribute equally regardless of
  the surface normal).
- **Directional GI path** (Phase 5g, optional): reads the C0 `probeAtlasTexture`
  directly, manually trilinearly blending 8 surrounding probes and weighting each
  probe's D² bins by `dot(binDir, surfaceNormal)`. Back-facing bins get zero weight,
  giving more directionally correct indirect light.

---

## Summary diagram

```
[C3 bake]  ← no upper, sees far field
     ↓ merge
[C2 bake]  ← C3 fills in misses
     ↓ merge
[C1 bake]  ← C2 fills in misses
     ↓ merge
[C0 bake]  ← C1 fills in misses; C0 now has full-range radiance
     ↓ reduction (average all directions)
[probeGridTexture 32³]           [C0 probeAtlasTexture 128×128×32]
     ↓ texture() in raymarch.frag      ↓ sampleDirectionalGI() in raymarch.frag
[Isotropic GI on screen]         [Directional GI on screen (Phase 5g)]
```

The isotropic path is the simpler baseline; the directional path (Phase 5g) reads the
atlas directly with cosine-weighted hemisphere integration, producing better directional
contrast at the cost of ~128 texelFetch calls per pixel instead of ~8.
