# Sponza SDF — Step 0: UI Entry Point

**Date:** 2026-05-06  
**Plan ref:** `doc/4/claude_plan/sponza_sdf_impl_plan_v2.md`  
**Status:** Build + startup verified; UI buttons require manual runtime check

---

## What Was Done

Added the Sponza OBJ scene entry point: UI button + `currentOBJPath` tracking so the
Cornell Box and Sponza OBJ buttons each have unambiguous active-state display, and
`loadOBJMesh()` knows which mesh is loaded.

---

## Changes

### `src/demo3d.h` — new member

```cpp
// After: bool useOBJMesh;
/** Which OBJ was last loaded: "cornell" or "sponza" */
std::string currentOBJPath;
```

Not initialized in the header — `useOBJMesh` defaults to `false` at construction,
so `currentOBJPath` is only read when `useOBJMesh == true`.

### `src/demo3d.cpp` — UI buttons (~line 3448)

Before this change, the Cornell Box OBJ button used `useOBJMesh` alone as its
active-state guard. That becomes ambiguous once a second OBJ scene exists.

```cpp
// Before (ambiguous with two OBJ scenes):
if (ImGui::Button(useOBJMesh ? "[ACTIVE] Cornell Box (OBJ)" : "Cornell Box (OBJ)"))

// After:
if (ImGui::Button(useOBJMesh && currentOBJPath == "cornell"
                      ? "[ACTIVE] Cornell Box (OBJ)" : "Cornell Box (OBJ)"))
```

New button inserted immediately after:

```cpp
if (ImGui::Button(useOBJMesh && currentOBJPath == "sponza"
                      ? "[ACTIVE] Sponza (OBJ)" : "Sponza (OBJ)")) {
    if (loadOBJMesh("res/scene/sponza.obj")) {
        std::cout << "[Demo3D] Loaded Sponza OBJ mesh!" << std::endl;
    } else {
        std::cerr << "[ERROR] Failed to load Sponza OBJ!" << std::endl;
    }
}
```

### `src/demo3d.cpp` — `loadOBJMesh()` (~line 4102)

```cpp
// After: useOBJMesh = true;
currentOBJPath = (filename.find("sponza") != std::string::npos) ? "sponza" : "cornell";
```

Classification is filename-based — robust for the two known OBJ paths
(`res/scene/sponza.obj`, `res/scene/cornell_box.obj`).

---

## Build Result

`cmake --build . --config Release` — clean. `RadianceCascades3D.exe` produced.

All warnings are pre-existing (C4819 Unicode encoding, C4244 int→float, C4100 unused
param in `obj_loader.h`, C4018 signed/unsigned). Zero new warnings from Step 0 changes.

---

## Key Learnings

- **`useOBJMesh` alone was not sufficient** as an active-state guard once a second OBJ
  scene was added. `currentOBJPath` is the disambiguator; both must be checked.
- **`build.ps1` cannot be invoked directly** under the system's PowerShell execution
  policy. Use `cmake --build build --config Release` from the repo root instead, or
  `cd build; cmake --build . --config Release`.
- **Execution policy workaround:** `powershell -ExecutionPolicy Bypass -File build.ps1`
  also works if the script's own logic is needed.

---

## Verification Checklist (Step 0)

**Automatically verified (2026-05-06):**

- [x] Build succeeds; no new warnings (`cmake --build build --config Release`, 17:40:57)
- [x] App launches without crash — entered main loop, no step 0 errors
- [x] Working directory confirmed as `D:\GitRepo-My\radiance-cascades-demo\3d` (startup log)
- [x] `res/scene/sponza.obj` is present and accessible from working dir (22.75 MB)
- [x] `loadOBJMesh()` first search path is the literal filename — `res/scene/sponza.obj` resolves directly
- [x] `sdf_3d.comp` compile error is pre-existing (not introduced by step 0)

**Runtime verified (2026-05-06, manual run):**

- [x] "Sponza (OBJ)" button works — `[Demo3D] Loaded Sponza OBJ mesh!` printed
- [x] 262267 faces loaded correctly (matches expected count)
- [x] OBJLoader clear-on-reload: Cornell (64v/32f) → Sponza (145185v/262267f) — no geometry accumulation
- [x] `loadOBJMesh()` resolves file via `../res/scene/` fallback (app launched from `build/`)
- [ ] `[ACTIVE]` / active label — not confirmed from log (UI state only)
- [ ] setScene() OBJ exit — not confirmed from log

**Voxelizer defect confirmed by runtime log:**

```
Cornell Box  32 faces  → 48303 voxels  (1509 vox/face)
Sponza   262267 faces  → 40856 voxels  (0.16 vox/face)  ← 2% fill rate
```

Sponza's axis-aligned walls/floors/ceilings degenerate in the 2D `pointInTriangle`
projection test. Step 1 voxelizer fix is the critical next action.

---

## What Is Next

**Step 1 — Fix CPU Voxelizer (`src/obj_loader.h`)**

Two bugs block correct Sponza voxelization:

1. `voxelizeTriangle()` uses `pointInTriangle()` — a 2D projection test that misses
   voxels near thin, axis-aligned Sponza walls and floors.
2. Triangle bbox is not expanded by the marking threshold before `worldToVoxel()`, so
   voxels adjacent to flat surfaces are never tested.

Fix: replace with `closestPointOnTriangle()` (Ericson §5.1.5) + expand bbox by
`threshold = voxelSize * sqrt(3) * 0.5` before the coordinate conversion.

**Step 2 — Felzenszwalb 3-pass Separable EDT (`src/demo3d.cpp`)**

Exact O(N³) Euclidean distance transform — adds `edt1d()` + `generateMeshSDF()`.
Operates at explicit `meshSDFResolution = 64` (not `volumeResolution = 128`).
Uploads both `sdfTexture` (R32F, world-space meters) and `albedoTexture` (RGBA8).

**Step 3 — Wire into `sdfGenerationPass()`**

Add `useOBJMesh` branch at top of `sdfGenerationPass()` so the mesh SDF is not
overwritten by the analytic path on the next `sceneDirty` frame.
