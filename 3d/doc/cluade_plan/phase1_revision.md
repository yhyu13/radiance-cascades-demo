# Phase 1 Revision — Root-Cause Analysis & Fixes

**Date:** 2026-04-22  
**Branch:** 3d  
**Status:** All fixes applied; build + smoke-test required

---

## What Was Already Done Before This Session

From the previous `[Lingma] phase 1.5` commit the following Phase 1 work existed:

| Item | State |
|------|-------|
| `res/shaders/raymarch.vert` | Created (fullscreen quad, passes UV) |
| `raymarchPass()` in demo3d.cpp | Implemented (matrices, uniforms, draws debugQuadVAO) |
| `raymarch.frag` | Updated with Lambertian lighting block |
| `sdfGenerationPass()` | Implemented (dispatches sdf_analytic.comp) |
| `initDebugQuad()` / `debugQuadVAO` | Working fullscreen quad reused by raymarchPass |

All the scaffolding for Phase 1 was in place. The scene was still black.

---

## Root Causes Found (in Priority Order)

### Bug 1 — `glBindImageTexture` with `GL_FALSE` (CRITICAL)

**File:** `src/demo3d.cpp` in `sdfGenerationPass()`  
**Symptom:** SDF texture only had correct values in z=0 layer; all other layers uninitialized (→ 0.0f on most drivers).  
**Mechanism:** `GL_FALSE` for the `layered` parameter makes OpenGL treat the 3D texture as a 2D image at layer 0. The compute shader's `imageSize(sdfVolume)` returns `(64, 64, 1)`, so only z=0 invocations pass the bounds check and store values. All other voxels have SDF=0.  
**Effect:** Raymarcher sees SDF=0 everywhere except z=0, takes minimum 0.01 steps for 256 iterations (2.56 units max travel), cannot reach geometry.  
**Fix:** `GL_FALSE` → `GL_TRUE`

---

### Bug 2 — Cornell Box walls sub-voxel thin (CRITICAL)

**File:** `src/analytic_sdf.cpp` `createCornellBox()`  
**Symptom:** Even after Bug 1 fix, the Cornell Box walls would not be detectable.  
**Mechanism:** Old design placed walls with `wallThickness = 0.05` world units in a 4-unit volume at 64³ resolution. Voxel size = 4/64 = 0.0625 units. Wall thickness = 0.05 < 0.0625 voxel. No voxel center falls inside any wall → SDF never goes negative → `dist < EPSILON` (1e-6) never fires → no hit detected.  
**Fix:** Redesigned Cornell Box:
- Room centered at origin, interior `[-1, 1]`
- Wall half-thickness `wt = 0.2` → full thickness 0.4 units = **6.4 voxels** at 64³
- Back wall spans z = `[-1.4, -1.0]`, confirmed 6 voxel centers inside

---

### Bug 3 — Normal estimation epsilon too small (CRITICAL)

**File:** `res/shaders/raymarch.frag` `estimateNormal()`  
**Symptom:** Surface normals would be `(0, 0, 0)` or random, causing broken shading even when a hit is detected.  
**Mechanism:** `eps = 0.001` world units. Voxel size = 0.0625. The 6 SDF samples around `worldPos ± (0.001, 0, 0)` fall in the same voxel → trilinear interpolation returns identical values → gradient ≈ 0 → `normalize(0,0,0)` = undefined.  
**Fix:** `eps = 0.06` (~1 voxel width)

---

### Bug 4 — Camera aimed at old Cornell Box origin

**File:** `src/demo3d.cpp` `resetCamera()`  
**Old:** position `(0.5, 0.5, 3.0)` target `(0.5, 0.5, 0.5)` — centered on the old [0,1] Cornell Box  
**Fix:** position `(0, 0, 4)` target `(0, 0, 0)` — looks into new centered Cornell Box from +z

---

### Bug 5 — Light position at old ceiling surface

**File:** `src/demo3d.cpp` `raymarchPass()`  
**Old:** `lightPos = (0.5, 1.0, 0.5)` — on the ceiling surface of old [0,1] box  
**Fix:** `lightPos = (0, 0.8, 0)` — inside new room, 0.2 below ceiling inner face (y=1.0)

---

### Bug 6 — Missing constructor initializers for Phase 1 debug fields

**File:** `src/demo3d.cpp` constructor  
**Mechanism:** `showRadianceDebug`, `showLightingDebug`, `radianceSliceAxis`, and 10 other Phase 1 debug fields had no initializer. On most compilers, uninitialized `bool` members = indeterminate → debug overlays could appear randomly or crash.  
**Fix:** Added all 13 missing initializers

---

### Bug 7 — DEFAULT_VOLUME_RESOLUTION = 128 (Performance)

**File:** `src/demo3d.h`  
**Issue:** 128³ = 2M voxels vs 64³ = 262K voxels. 8× slower SDF dispatch for no benefit at this stage.  
**Fix:** Changed to 64

---

## Files Changed

| File | Change |
|------|--------|
| `src/demo3d.cpp` | GL_FALSE→GL_TRUE; camera; light; missing initializers |
| `src/demo3d.h` | DEFAULT_VOLUME_RESOLUTION 128→64 |
| `src/analytic_sdf.cpp` | createCornellBox() full redesign |
| `res/shaders/raymarch.frag` | estimateNormal eps 0.001→0.06 |

---

## Expected Post-Fix Behavior

A straight-ahead ray from `(0,0,4)` toward `(0,0,-1.2)` (back wall):

1. Enters volume at z=2, SDF ≈ 0.8 (distance to ceiling corner)
2. Steps in ~8 SDF-guided strides to reach back wall at z ≈ -1.0
3. Enters wall (SDF goes negative), `dist < 1e-6` fires
4. Normal computed via gradient → ~`(0, 0, 1)` pointing toward camera
5. NdotL ≈ 0.78 with light at `(0, 0.8, 0)` → back wall ≈ 83% bright warm white

Rays at angles will reveal floor, ceiling, red left wall, green right wall, and the two interior boxes.

---

## Phase 1 Stop Condition (from PLAN.md)

- [x] `raymarch.vert` exists and links  
- [x] `raymarchPass()` implemented  
- [ ] **Build succeeds** — needs verification  
- [ ] **Scene shows non-background geometry** — needs smoke test
