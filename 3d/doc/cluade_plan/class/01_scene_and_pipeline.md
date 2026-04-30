# 01 — The Scene and the Render Pipeline

**Purpose:** Understand what the program renders and how the render loop is structured
before looking at cascades at all.

---

## The Scene

We render a Cornell Box: a closed room with a red left wall, green right wall, white
ceiling, white floor, white back wall, and two white boxes inside. A point light hangs
near the top center. The scene is entirely made of boxes (and an optional sphere).

The room sits in a 4 m × 4 m × 4 m cube centered at the origin, running from −2 m to +2 m
on each axis.

### How the scene is stored — Analytic SDF

Instead of triangle meshes, every object is described by a math formula. A box SDF says
"the distance from point P to this box is max(|P − center| − halfExtent, 0)". The GPU
evaluates these formulas directly.

We bake the SDF into a 64×64×64 grid of floats once per scene change:
- **sdfTexture**: 64³ R32F — distance to nearest surface at each grid cell
- **albedoTexture**: 64³ RGBA8 — surface color at each grid cell

Each grid cell is (4 m / 64) = 0.0625 m = 6.25 cm wide.

---

## The Render Pipeline — 6 passes per frame

```
Pass 1: Voxelize        — upload primitive list to GPU buffer
Pass 2: SDF bake        — sdf_analytic.comp writes sdfTexture + albedoTexture
Pass 3: Cascade bake    — radiance_3d.comp × 4 levels (C3→C2→C1→C0)
                          + reduction_3d.comp × 4 levels
Pass 4: Raymarch        — raymarch.frag shades every screen pixel
Pass 5: SDF debug       — top-left 400×400 overlay
Pass 6: Radiance debug  — top-right 400×400 overlay
```

Passes 1–2 only run when the scene changes (user paints a voxel or reloads).
Pass 3 runs only when `cascadeReady == false` (toggle changed, scene changed).
Passes 4–6 run every frame (real-time final render + debug overlays).

---

## Pass 4 — What the final image actually is

`raymarch.frag` runs once per screen pixel. For each pixel it:
1. Casts a primary ray from the camera into the scene.
2. Sphere-marches through `sdfTexture` until it hits a surface or leaves the volume.
3. At the surface hit, computes **direct light** by checking a shadow ray toward the point light.
4. Looks up **indirect light (GI)** from the nearest probe in C0's `probeGridTexture`.
5. Combines: `final = direct + GI_from_C0`.
6. Applies ACES tone mapping and gamma correction.

The GI lookup in step 4 is a simple `texture()` trilinear sample from a 32³ texture.
**The quality of GI entirely depends on what is stored in those 32³ probeGridTextures.**
Everything in Phases 3–5 is about making that stored value more accurate.

---

## Key numbers to remember

| Thing | Value |
|---|---|
| Scene size | 4 m × 4 m × 4 m |
| SDF grid | 64³ = 262,144 cells, 0.0625 m each |
| Probe grid | 32³ = 32,768 probes, 0.125 m apart (default) |
| Cascade levels | 4 (C0 near, C3 far) |
| Final GI source | C0 probeGridTexture, 32³ RGBA16F |

---

## What determines image quality

| Problem | Root cause | Fix |
|---|---|---|
| GI too dark | Probes don't see far enough | More cascade levels / wider intervals |
| Color bleeding wrong | Upper cascade value is averaged (isotropic) | Phase 5 directional merge |
| Banding at cascade boundary | Hard cutoff when switching levels | Phase 4c blend zone |
| Bin boundary banding | Nearest-bin lookup in atlas | Phase 5f bilinear |
