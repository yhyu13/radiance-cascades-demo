# Review: Sponza SDF Step 3 Plan

Review timestamp: 2026-05-06T20:07:40+08:00

Target: `doc/4/claude_plan/sponza_sdf_step3_plan.md`

Verdict: partially accepted, but only after Step 2 is corrected. The core placement of an OBJ branch at the top of `sdfGenerationPass()` is right for preventing analytic SDF overwrite. The plan still needs stronger failure handling, temporal-history reset, shader-mode handling, and a safer `meshVoxelData` handoff before it can reliably switch the rendered scene.

## What Matches Current Source

- `src/demo3d.cpp:4066-4117` currently stores OBJ voxelization in local `voxelData`, uploads `voxelGridTexture`, then sets `sceneDirty`, `useOBJMesh`, and `currentOBJPath`.
- `src/demo3d.cpp:442-458` resets local static `sdfReady` after `sceneDirty`, then calls `sdfGenerationPass()` and marks cascades stale.
- `src/demo3d.cpp:1250-1310` still has no OBJ branch; the analytic path runs first while `analyticSDFEnabled` is true.
- `src/demo3d.cpp:2138-2157` currently resets `useOBJMesh = false` and clears `currentOBJPath` in `setScene()`. The plan is correct that analytic scene buttons no longer need that specific fix.

## Findings

### 1. High - The branch marks mesh SDF ready even if the bake fails

Affected plan lines:

- `sponza_sdf_step3_plan.md:117-122`
- `sponza_sdf_step3_plan.md:194`

The proposed branch calls `generateMeshSDF()` and then sets `meshSDFReady = true` unconditionally:

```cpp
generateMeshSDF();
meshSDFReady = true;
return;
```

Step 2's proposed `generateMeshSDF()` returns `bool`, but Step 3 ignores it. The outer render loop also marks its local static `sdfReady = true` after `sdfGenerationPass()` returns, regardless of whether a texture was uploaded.

This can lock the app into a stale or empty SDF state after an empty voxelization, GL upload failure, all-INF field, or future bake error.

Recommended correction:

```cpp
if (useOBJMesh && !meshVoxelData.empty()) {
    if (!meshSDFReady) {
        if (!generateMeshSDF()) {
            std::cerr << "[ERROR] Mesh SDF bake failed; keeping SDF dirty\n";
            return;
        }
    }
    return;
}
```

Better: change `sdfGenerationPass()` to return `bool` and only set render-loop `sdfReady = true` on success. If that is too invasive, keep `meshSDFReady` assignment inside `generateMeshSDF()` only after successful upload, and make failures visibly clear the mesh mode or fall back intentionally.

### 2. High - Scene switching should reset temporal cascade history, not only `cascadeReady`

Affected plan lines:

- `sponza_sdf_step3_plan.md:38-54`
- `sponza_sdf_step3_plan.md:186`

Current `render()` sets `cascadeReady = false` when SDF changes, but it does not set `historyNeedsSeed = true` in that path:

- `src/demo3d.cpp:453-458` regenerates SDF and marks cascades stale.
- `src/demo3d.cpp:1391` uses `historyNeedsSeed` to force temporal alpha to 1.
- `src/demo3d.cpp:1327` clears `historyNeedsSeed` after cascade update.

`useTemporalAccum` defaults to true. Without a history reseed, the first OBJ cascade bake can be EMA-blended with the previous analytic scene history, and switching back can blend old OBJ lighting into the analytic scene.

Recommended correction:

- When `loadOBJMesh()` commits a new mesh scene, set `historyNeedsSeed = true`, `renderFrameIndex = 0`, and optionally `temporalRebuildCount = 0`.
- Do the same in `setScene()` for analytic scene changes, because this is a general scene-switching invariant.
- Add verification with temporal accumulation enabled, not only one-frame SDF debug.

### 3. Medium - `useAnalyticRaymarch` can still make the final render ignore the mesh SDF

Affected plan lines:

- `sponza_sdf_step3_plan.md:174`
- `sponza_sdf_step3_plan.md:179-186`

The plan says no UI or render-loop changes are needed, but final rendering has a separate analytic raymarch toggle:

- `src/demo3d.cpp:1645-1647` sets `uUseAnalyticSDF` from `useAnalyticRaymarch`.
- `src/demo3d.cpp:2724` exposes "Analytic SDF (smooth, no grid)" in the UI.
- `res/shaders/raymarch.frag:233-240` ignores `uSDF` when `uUseAnalyticSDF != 0`.

If that checkbox is on, loading OBJ can correctly bake `sdfTexture` while the final pass continues evaluating analytic primitives. That directly contradicts the goal of making the OBJ buttons switch the rendered scene.

Recommended correction:

- Set `useAnalyticRaymarch = false` when entering OBJ mode, or disable the checkbox while `useOBJMesh` is true.
- Add a verification item that final mode 0 is using the grid texture path.

### 4. Medium - "No shader changes" depends on a Step 2 plan that is currently insufficient

Affected plan lines:

- `sponza_sdf_step3_plan.md:174`
- `sponza_sdf_step3_plan.md:179-186`

As noted in the Step 2 review, the current shader hit tests are too strict for a center-site voxel UDF:

- `res/shaders/raymarch.frag:430` uses `dist < EPSILON`.
- `res/shaders/radiance_3d.comp:243` uses `dist < 0.002`.

Step 3's wiring can stop analytic overwrites, but it cannot make the rendered scene switch if the mesh SDF never produces ray hits or if albedo remains black. The "No shader changes" claim should be conditional on Step 2 producing a conservative zero band and usable albedo.

### 5. Medium - `loadOBJMesh()` should swap completed voxel data, not clear the member first

Affected plan lines:

- `sponza_sdf_step3_plan.md:77-91`
- `sponza_sdf_step3_plan.md:141-144`

The proposed code clears `meshVoxelData` before voxelization. If voxelization fails or produces an empty grid, the previous valid mesh data is gone while other mode flags may still reflect the previous OBJ scene.

Use a local staging vector and commit only after success:

```cpp
std::vector<uint8_t> newVoxelData;
objLoader.voxelize(volumeResolution, newVoxelData, volumeOrigin, volumeSize);
if (newVoxelData.empty()) return false;

meshVoxelData = std::move(newVoxelData);
meshSDFReady = false;
useOBJMesh = true;
sceneDirty = true;
```

This follows the same pattern as the Step 1 loader fix: do not destroy the previous good state until the new state is valid.

### 6. Low - Keeping mesh state through analytic scenes is not useful as described

Affected plan lines:

- `sponza_sdf_step3_plan.md:137-144`

The plan says keeping `meshVoxelData` after `setScene()` lets re-clicking an OBJ button re-bake from existing data without reloading the file. The current UI buttons always call `loadOBJMesh(...)`, so that cache path is not used by the proposed UI flow.

Keeping 8 MB of voxel data is not a big memory issue, but the state should be explicit:

- clear mesh data on analytic `setScene()` for simpler invariants, or
- keep it as a real cache keyed by path and resolution.

Do not leave it as an implied cache that no caller actually uses.

### 7. Low - The plan overstates Step 1 runtime verification

Affected plan lines:

- `sponza_sdf_step3_plan.md:196`

The risk table says "Step 1 voxelizer already verified (37757 Sponza, Cornell Box pending)." The accepted Step 1 review status still had runtime voxel-count and timing verification pending, and the current source only prints voxel counts at runtime. If 37,757 came from an external run, cite the run log and machine/context. Otherwise keep this as pending.

## Recommended Fix Order

1. Fix Step 2's conservative distance band, albedo propagation, and failure semantics.
2. Stage OBJ voxel data locally and commit only after successful voxelization.
3. Add the `sdfGenerationPass()` OBJ branch, but honor `generateMeshSDF()` failure.
4. Reset temporal cascade history on scene changes.
5. Force or verify grid-based final raymarching for OBJ mode.
