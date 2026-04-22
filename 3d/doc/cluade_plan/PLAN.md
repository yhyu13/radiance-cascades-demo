# Claude Plan: 3D Radiance Cascades Demo

**Updated:** 2026-04-22  
**Branch:** 3d  
**Goal:** Visible Cornell-box raymarched image with one working radiance cascade

---

## Phase Status

| Phase | Goal | Status |
|-------|------|--------|
| 0 | Analytic SDF generation, window, camera | ✅ Done |
| 1 | Cornell box visible, direct Lambertian shading | ✅ Done (see `phase1_revision.md`) |
| 2 | Single 32³ cascade, indirect GI toggle | ✅ Implemented — pending visual smoke test |
| 3 | Second cascade (optional) | 🔲 Blocked on Phase 2 visual confirmation |

---

## Phase 1 — What Was Fixed (2026-04-22)

Seven bugs found and fixed. See `doc/cluade_plan/phase1_revision.md` for full details.

| Bug | File | Fix |
|-----|------|-----|
| `glBindImageTexture GL_FALSE` — only z=0 layer written | `demo3d.cpp` | `GL_FALSE` → `GL_TRUE` |
| Cornell Box walls 0.05 units thin (sub-voxel at 64³) | `analytic_sdf.cpp` | Redesigned: centered `[-1,1]`, walls 0.4 thick |
| `estimateNormal` eps=0.001 (~0 gradient in texture space) | `raymarch.frag` | eps 0.001 → 0.06 |
| Camera aimed at old [0,1] box origin | `demo3d.cpp` | pos `(0,0,4)` target `(0,0,0)` |
| Light at old ceiling surface `(0.5,1.0,0.5)` | `demo3d.cpp` | `(0, 0.8, 0)` inside room |
| 13 Phase 1 debug fields uninitialized | `demo3d.cpp` | Added all initializers |
| `DEFAULT_VOLUME_RESOLUTION = 128` | `demo3d.h` | Changed to 64 |

---

## Phase 2 — What Was Implemented (2026-04-22)

Eight bugs found and fixed. See `doc/cluade_plan/phase2_changes.md` for full details.

| Bug | File | Fix |
|-----|------|-----|
| `sampleSDF()` used probe-space coords (wrong UV) | `radiance_3d.comp` | UV = (worldPos - gridOrigin) / gridSize |
| `raymarchSDF()` returned white vec4 on hit | `radiance_3d.comp` | Lambertian shading with SDF gradient normal |
| `main()` sampled unbound `uPrevRadiance`, `uCoarseCascade` | `radiance_3d.comp` | Simplified: average over rays, no temporal |
| `initialize()` stub — never created probe texture | `demo3d.cpp` | Creates 32³ GL_RGBA16F texture |
| `initCascades()` created 6 stubs, `cascadeCount=6` | `demo3d.cpp` | Single 32³ cascade, `cascadeCount=1` |
| `updateSingleCascade()` stub — never dispatched | `demo3d.cpp` | Full dispatch: uniforms, SDF, image, barriers |
| `injectDirectLighting()` spammed every frame | `demo3d.cpp` | Early return (frozen for Phase 2) |
| Cascade dispatched every frame for static scene | `demo3d.cpp` | `cascadeReady` flag, dispatch only on SDF change |

**Also wired:**
- `uniform bool uUseCascade` + indirect sampling block in `raymarch.frag`
- `bool useCascadeGI` in `demo3d.h`; cascade texture bound to unit 1 in `raymarchPass()`
- "Cascade GI" ImGui checkbox in Settings panel

---

## Phase 2 Stop Conditions

| Condition | Status |
|-----------|--------|
| `probeGridTexture` non-zero after init | ✅ Confirmed (`active=1` in log) |
| `radiance_3d.comp` compiles | ✅ Confirmed |
| `raymarch.frag` compiles | ✅ Confirmed |
| No crash, no console spam | ✅ Confirmed (runtime clean) |
| "Cascade GI" checkbox visible in UI | ✅ Implemented |
| Toggle changes image brightness/color | ⬜ Needs visual smoke test |

---

## Current Verified Runtime State

```
[Demo3D] Cascade 0: 32^3 probes, cellSize=0.125, active=1
[Demo3D] Analytic SDF generation complete.
  → cascade dispatched once, silent thereafter
  → raymarchPass() runs 60 fps, no log spam
```

GPU: RTX 2080 SUPER, OpenGL 3.3 (NVIDIA), compute via ARB extensions.  
Shaders that failed: `sdf_3d.comp` (imageLoad bindless — frozen, not needed).  
All Phase 2 shaders compiled successfully.

---

## Phase 2 Parameter Defaults

```
volumeResolution       = 64      // SDF grid
cascades[0].resolution = 32      // 32^3 = 32K probes
cascades[0].raysPerProbe = 4     // increase to 8 if too noisy
cascades[0].cellSize   = 0.125   // 4.0 (volumeSize) / 32 = 0.125
baseInterval           = 0.125   // passed as uBaseInterval to shader
Cornell Box room       = [-1, 1] // interior, walls at ±1.2 center
Light                  = (0, 0.8, 0)  // inside room near ceiling
Ray interval           = length(uGridSize) ≈ 6.93  // full volume diagonal
```

---

## Phase 3 — Next (optional)

**Goal:** A second cascade (64³) merging into cascade 0 for longer-range indirect.  
**Prerequisite:** Phase 2 visual smoke test confirms toggle changes image.  
**Status:** Blocked.

---

## Definition of Done

**Phase 2 done when:**  
ImGui "Cascade GI" toggle changes the image visibly (brightness shift or color tint from red/green wall bounce).

**Project done when Phase 2 is confirmed.**  
Phase 3 is optional polish.
