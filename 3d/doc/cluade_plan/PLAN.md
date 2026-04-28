# Claude Plan: 3D Radiance Cascades Demo

**Updated:** 2026-04-28  
**Branch:** 3d  
**Goal:** Visible Cornell-box raymarched image with a working multi-cascade radiance hierarchy

---

## Phase Status

| Phase | Goal | Status |
|-------|------|--------|
| 0 | Analytic SDF generation, window, camera | ✅ Done |
| 1 | Cornell box visible, direct Lambertian shading | ✅ Done (see `phase1_revision.md`) |
| 2 | Single 32³ cascade, indirect GI toggle | ✅ Done |
| 3 | 4-level cascade (C0–C3), merge chain, debug modes | ✅ Done (see `phase2_debug_learnings.md`) |
| 4 | Cascade quality: env fill, ray scaling, distance blend, filter verify, debug polish | ✅ Done (see `phase4a`–`phase4e` docs) |
| 5 | Directional-correct merge (per-direction upperSample) | 🔶 In Progress (5a–5c implemented, runtime validation pending — see `phase5bc_impl_learnings.md`) |

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

## Phase 3 — What Was Done

**Goal achieved:** Full 4-level cascade hierarchy (C0–C3, each 32³) with per-level ray intervals, merge chain (C3→C2→C1→C0), 7 render modes (0–6 incl. GI-only mode 6), per-cascade probe stats, and cascade panel with debug visualizations.

See `phase2_debug_learnings.md` and `shadertoy_gap_analysis.md` for details.

---

## Phase 4 — What Was Done (2026-04-24)

Five sub-phases, all complete. Root docs in `doc/cluade_plan/phase4[a-e]_*.md`.

| Sub-phase | Description | Key files |
|---|---|---|
| 4a | Env fill: out-of-volume rays return sky color; propagates down merge chain | `radiance_3d.comp`, `demo3d.cpp` |
| 4b | Per-cascade ray scaling: `Ci = base × 2^i`; default C0=8 C1=16 C2=32 C3=64 | `demo3d.cpp`, `radiance_debug.frag` |
| 4c | Distance-blend merge: lerp surface hits toward upper cascade near tMax; C3 guarded | `radiance_3d.comp` |
| 4d | Filter verification: WRAP_R, GL_LINEAR, UVW normalization — all correct, no-op | (read-only) |
| 4e | Packed-decode fix (integer arithmetic); blend-zone table; coverage bars; mean-lum chart | `demo3d.cpp` |

**4c A/B result:** No visible difference, mean-lum unchanged. Banding driven by directional mismatch, not the binary boundary switch. 4c is forward-compatible — becomes effective when Phase 5 provides per-direction `upperSample`.

**Known limitation (deferred):** RGBA16F packed hit-count encoding loses precision for mixed surf/sky probes at C3 once `skyH ≥ 9`. Stats are exact when env fill is OFF. Fix requires a separate `GL_RG32UI` buffer.

---

## Phase 5 — In Progress

**Goal:** Directional-correct merge. Replace the isotropic `upperSample` with per-direction radiance lookup so that the merge blends toward the actual far-field radiance for each specific ray direction.

**Why:** The 4c A/B confirmed that all remaining GI banding is directional mismatch. No further cleanup within the isotropic model will fix it.

**Prerequisite:** Phase 4 complete. ✅

See `phase5_plan.md` for full sub-phase breakdown (5a–5e), shader code, texture layout, and validation plan.

| Sub-phase | Description | Status |
|---|---|---|
| 5a | Octahedral direction encoding — retire Fibonacci; D×D bin grid | ✅ Implemented, compile-verified |
| 5b | Per-direction atlas texture — `(32·D)²×32` RGBA16F, GL_NEAREST | ✅ Implemented, compile-verified |
| 5b-1 | Atlas reduction pass — averages D² bins → probeGridTexture (keeps raymarch.frag valid) | ✅ Implemented, compile-verified |
| 5c | Directional upper cascade merge — `texelFetch` at exact direction bin + isotropic A/B toggle | ✅ Implemented, compile-verified |
| Debug | 6-mode atlas vis, HitType fix, Bin viewer, probe fill rate readback fix | ✅ Implemented |
| 5e | Per-cascade D scaling A/B — after 5c visual validation | ⬜ Pending |

**Runtime validation status:** All 5a–5c changes compile clean. Visual A/B (directional vs isotropic merge toggle) has not yet been run. The Bin viewer (mode 5) near a red wall should show directional color separation as the key confirmation.

See `phase5bc_impl_learnings.md` and `phase5_debug_impl_learnings.md` for implementation details and known gotchas.

---

## Definition of Done

**Phase 4 done:** All 4a–4e sub-phases implemented, A/B result documented. ✅  
**Phase 5 done when:** GI banding at cascade interval boundaries is visibly reduced with per-direction merge active vs Phase 4 baseline. Red wall and green wall show distinct directional color separation in probe atlas debug mode.
