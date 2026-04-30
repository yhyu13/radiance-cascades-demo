# 05 — Phase 5b: The Per-Direction Atlas

**Purpose:** Understand how per-direction radiance is stored in the atlas texture and
why GL_NEAREST is required.

---

## The problem Phase 5b solves

After Phase 5a, each probe fires 16 rays (D=4, 4×4 bins). But the result was still
direction-averaged: all 16 results were summed and stored as one color in `probeGridTexture`.

To do a directional merge (Phase 5c), we need to retrieve the radiance for a specific
direction after the bake. That means storing one color *per direction bin*, not one per probe.

---

## The atlas texture layout

**One atlas texture per cascade level.** Size: `(probeRes × D) × (probeRes × D) × probeRes` RGBA16F.

At 32³ probes and D=4: **128 × 128 × 32** RGBA16F = 128×128×32×8 bytes = **4 MB**.

Each probe "owns" a D×D tile within the atlas. Probe at grid index (px, py, pz):
- X range: `[px×D, px×D + D)`
- Y range: `[py×D, py×D + D)`
- Z slice: `pz`

Direction bin (dx, dy) for probe (px, py, pz) → atlas texel:
```
ivec3(px × D + dx,  py × D + dy,  pz)
```

Visually (one Z-slice, 32×32 probes, D=4):
```
+-----+-----+-----+-----+  ← probe row y=0
|0,0  |1,0  |2,0  | ... |
|4×4  |4×4  |4×4  |     |
+-----+-----+-----+-----+
|0,1  |1,1  |2,1  | ... |
| ... | ... | ... |     |
```

Each inner 4×4 block = 16 direction bins for one probe.

---

## What each atlas texel stores

- **RGB**: Radiance (color/brightness) for that direction at that probe.
- **A (alpha)**: Hit sentinel:
  - `> 0`: surface hit; value = distance along ray in meters
  - `< 0`: sky hit (ray exited the volume)
  - `= 0`: miss (no hit within [tMin, tMax])

---

## Why GL_NEAREST is mandatory

The atlas uses `GL_NEAREST` texture filtering. Here is why `GL_LINEAR` would break:

Probe (px=5) occupies X range [20, 24). Probe (px=6) occupies X range [24, 28).
The boundary between them is at X = 24.

With `GL_LINEAR`, a sample at the boundary would blend texels from X=23 (probe 5's bin 3)
and X=24 (probe 6's bin 0). This mixes two different probes' data — **cross-probe contamination**.

The directional merge must read one probe's specific direction bin with no blending into
neighboring probes' tiles. `GL_NEAREST` guarantees this.

**Consequence:** We cannot get smooth directional interpolation "for free" from the GPU sampler.
Any directional blending must be done manually in shader code (→ Phase 5f).

**Setup in code:**
```cpp
cascades[i].probeAtlasTexture = gl::createTexture3D(atlasXY, atlasXY, probeRes,
    GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, nullptr);
// Override to GL_NEAREST — GL_LINEAR would cross tile boundaries
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
```

---

## Writing the atlas — imageStore

The compute shader writes to the atlas using `imageStore` (a write path that bypasses
the sampler entirely, addresses by exact integer texel):

```glsl
layout(rgba16f) uniform image3D oAtlas;

// Inside the per-direction loop:
ivec3 atlasTxl = ivec3(probePos.x * uDirRes + dx,
                        probePos.y * uDirRes + dy,
                        probePos.z);
imageStore(oAtlas, atlasTxl, vec4(rad, hit.a));
```

One `imageStore` call per direction bin per probe. The 4×4×4 thread workgroup processes
64 probes at once, each writing 16 bins = 1024 writes per workgroup.

---

## Phase 5b-1: The reduction pass

`raymarch.frag` still reads from `probeGridTexture` (32³, isotropic) to look up GI.
The atlas cannot be sampled by name from the fragment shader in a general way.

So after the atlas bake, a second compute shader (`reduction_3d.comp`) reads all D²
bins for each probe and averages them, writing the result back to `probeGridTexture`.

```
radiance_3d.comp → writes atlas (per-direction)
barrier()
reduction_3d.comp → reads atlas, averages D² bins → writes probeGridTexture
barrier()
```

This keeps the isotropic `probeGridTexture` up to date (for the final render) while
also maintaining the per-direction atlas (for the directional merge).

**Note:** The reduction pass zeroes the alpha channel of `probeGridTexture` (hit count
convention was retired in Phase 5b).

---

## Memory at each configuration

| Mode | D | Atlas dims | Per cascade | 4 cascades |
|---|---|---|---|---|
| co-located, D=4 | 4 | 128×128×32 | 4 MB | 16 MB |
| co-located, D=8 | 8 | 256×256×32 | 16 MB | 64 MB |
| co-located, D=16 | 16 | 512×512×32 | 64 MB | 256 MB |
| non-co-located C0, D=4 | 4 | 128×128×32 | 4 MB | — |
| non-co-located C1, D=4 | 4 | 64×64×16 | 1 MB | — |
| non-co-located C2, D=4 | 4 | 32×32×8 | 0.25 MB | — |
| non-co-located C3, D=4 | 4 | 16×16×4 | 0.0625 MB | — |

---

## Dispatch ordering (per cascade, not per frame)

Cascades are baked **C3 first, C0 last**, so each cascade can read its upper cascade's
already-computed atlas during the merge. But the barrier must be per-cascade, not global:

```
dispatch radiance_3d for C3 → barrier → dispatch reduction for C3 → barrier
dispatch radiance_3d for C2 → barrier → dispatch reduction for C2 → barrier
dispatch radiance_3d for C1 → barrier → dispatch reduction for C1 → barrier
dispatch radiance_3d for C0 → barrier → dispatch reduction for C0 → barrier
```

Wrong: dispatching all radiance passes then all reduction passes — C2's bake would
read C3's atlas before C3's reduction has written `probeGridTexture`.

---

## Summary

After Phase 5b:
- Each cascade has an atlas texture storing one RGBA16F per (probe, direction bin).
- The atlas is indexed by exact integer coordinates (texelFetch, GL_NEAREST).
- A reduction pass keeps the isotropic `probeGridTexture` in sync.
- The stored alpha encodes hit type (surface / sky / miss).

What is still missing: the **merge** still uses the isotropic `probeGridTexture`, not
the directional atlas. That is Phase 5c.
