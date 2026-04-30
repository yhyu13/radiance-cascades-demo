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
3. At the surface hit, computes **direct light** by checking a shadow ray toward the point light (Phase 5h; binary hard shadow or optional cone soft shadow from Phase 5i).
4. Looks up **indirect light (GI)** from the probe system. Two paths exist:
   - **Isotropic path** (default baseline): `texture(uRadiance, uvw)` — hardware trilinear sample from the selected cascade's `probeGridTexture` (a 32³ direction-averaged texture written by the reduction pass).
   - **Directional GI path** (Phase 5g, optional): `sampleDirectionalGI(pos, normal)` — reads the C0 `probeAtlasTexture` directly, integrating only bins facing the surface normal over 8 surrounding probes with manual trilinear blending. More directionally correct than the isotropic path.
5. Combines: `final = direct + indirect`.
6. Applies ACES tone mapping and gamma correction.

In the isotropic path, GI quality depends on what is stored in `probeGridTexture`. In the
directional path, quality depends on the C0 `probeAtlasTexture` content and how many bins
face the surface. Everything in Phases 3–5 is about making those stored values more accurate
and reading them in a more directionally correct way.

---

## Key numbers to remember

| Thing | Value |
|---|---|
| Scene size | 4 m × 4 m × 4 m |
| SDF grid | 64³ = 262,144 cells, 0.0625 m each |
| Probe grid | 32³ = 32,768 probes, 0.125 m apart (default) |
| Cascade levels | 4 (C0 near, C3 far) |
| Final GI source (isotropic) | Selected cascade `probeGridTexture`, 32³ RGBA16F |
| Final GI source (directional, Phase 5g) | C0 `probeAtlasTexture`, 128×128×32 RGBA16F (D=4) |

---

## What determines image quality

| Problem | Root cause | Fix |
|---|---|---|
| GI too dark | Probes don't see far enough | More cascade levels / wider intervals |
| Color bleeding wrong | Upper cascade value is averaged (isotropic) | Phase 5c directional merge |
| Banding at cascade boundary | Hard cutoff when switching levels | Phase 4c blend zone |
| Bin boundary banding | Nearest-bin lookup in atlas | Phase 5f bilinear |
| No direct shadows | Direct term had no occlusion check | Phase 5h shadow ray |
| Indirect washed-out (back-facing bins included) | Isotropic average ignores surface normal | Phase 5g directional GI path |
| Hard shadow edge on binary point light | Binary shadow is visually harsh | Phase 5i soft shadow approximation |
