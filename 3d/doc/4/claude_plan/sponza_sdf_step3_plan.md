# Sponza SDF — Step 3: Wire `useOBJMesh` into the Render Pipeline (revised)

**Date:** 2026-05-06 (revised 2026-05-07 per codex review `05_*` / reply `05_*`)
**Plan ref:** `doc/4/claude_plan/sponza_sdf_impl_plan_v2.md`
**Status:** Draft v2 — pending implementation (waits on Step 2 v2 landing first)
**Changelog:** v2 — accepted all seven findings from `05_sponza_sdf_step3_plan_review.md`.
`generateMeshSDF()` failure now blocks `meshSDFReady`; temporal cascade history
reseeded on every scene switch (OBJ and analytic); `useAnalyticRaymarch` forced
off + UI gated when OBJ mesh is active; voxel data staged locally and committed
on success only; `meshVoxelData` cleared on `setScene()`; Step 1 verification
status marked pending.

---

## Goal

Make clicking "Cornell Box (OBJ)" or "Sponza (OBJ)" actually switch the rendered
scene. Currently `loadOBJMesh()` sets `useOBJMesh = true` but `sdfGenerationPass()`
ignores it and always overwrites `sdfTexture` with the analytic Cornell Box.

Step 3 lands four targeted edits: stage-and-commit in `loadOBJMesh()`,
mesh-branch + failure-honor in `sdfGenerationPass()`, history-reseed +
voxel-clear in `setScene()`, and a UI gate on `useAnalyticRaymarch`.

---

## Root Cause (confirmed)

```
loadOBJMesh()
  → voxelizes into local voxelData (discarded after upload to voxelGridTexture)
  → sets sceneDirty = true, useOBJMesh = true

render() next frame:
  → sceneDirty → voxelizationPass() (stub, clears sceneDirty, resets sdfReady = false)
  → !sdfReady → sdfGenerationPass()
       → analyticSDFEnabled == true
       → dispatches sdf_analytic.comp with Cornell Box primitives  ← OVERWRITES mesh SDF
```

`meshVoxelData`, `meshSDFReady`, and `generateMeshSDF()` do not exist yet —
Step 2 introduces them. Step 3 assumes Step 2 is in place.

---

## Render Loop — No Changes Needed

```cpp
// demo3d.cpp ~442 — already correct
static bool sdfReady = false;
if (sceneDirty) {
    voxelizationPass();
    sdfReady = false;        // ← already resets; Step 3 relies on this
}
if (!sdfReady) {
    sdfGenerationPass();     // ← Step 3 adds the mesh branch here
    sdfReady = true;
}
```

`sdfReady` is reset whenever `sceneDirty` fires, so the pipeline calls
`sdfGenerationPass()` on the frame after `loadOBJMesh()`. No render-loop changes.

---

## Change 3a — `loadOBJMesh()` stages voxels, commits on success

**File:** `src/demo3d.cpp`, lines ~4101–4119

**Pattern:** stage in a local vector, commit only after voxelization succeeds.
Mirrors the Step 1 file-open / clear-after-success pattern.

```cpp
// 1. Voxelize into a local staging vector. Previous mesh state is untouched
//    until we commit at the end.
std::vector<uint8_t> newVoxelData;
objLoader.voxelize(volumeResolution, newVoxelData, volumeOrigin, volumeSize);
if (newVoxelData.empty()) {
    std::cerr << "[ERROR] Empty voxelization for " << filename
              << "; keeping previous mesh data\n";
    return false;
}

// 2. Debug-display upload uses the new data directly. This is the only side
//    effect before commit; if any later step were to fail, the worst case is
//    that voxelGridTexture briefly shows the new mesh while meshVoxelData
//    still holds the old data — acceptable.
glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0,
    volumeResolution, volumeResolution, volumeResolution,
    GL_RGBA, GL_UNSIGNED_BYTE, newVoxelData.data());
glBindTexture(GL_TEXTURE_3D, 0);

// 3. Commit. All scene-switch invariants set together.
meshVoxelData        = std::move(newVoxelData);
meshSDFReady         = false;            // signal sdfGenerationPass to bake
useOBJMesh           = true;
useAnalyticRaymarch  = false;            // F3: OBJ mode has no analytic primitives
historyNeedsSeed     = true;             // F2: temporal reseed
renderFrameIndex     = 0;                // F2
temporalRebuildCount = 0;                // F2
sceneDirty           = true;
currentOBJPath       = (filename.find("sponza") != std::string::npos) ? "sponza" : "cornell";

std::cout << "[Demo3D] OBJ committed; SDF will be baked next frame\n";
return true;
```

Key points:

- `newVoxelData` (local) → `meshVoxelData` (member via `std::move`): voxels survive
  to `sdfGenerationPass`, and the previous member is only replaced after success.
- `meshSDFReady = false`: signals EDT has not run for this OBJ.
- `useAnalyticRaymarch = false`: the final raymarch shader has a separate analytic
  path (`uUseAnalyticSDF`) that, if true, ignores `uSDF` entirely. Force off here.
- `historyNeedsSeed / renderFrameIndex / temporalRebuildCount`: temporal cascade
  reseed — without this, the first OBJ frame EMA-blends with the previous scene's
  history and ghosts visibly.

---

## Change 3b — `sdfGenerationPass()` gains a mesh branch that honors failure

**File:** `src/demo3d.cpp`, line ~1250

Add at the very top of `sdfGenerationPass()`, before the `analyticSDFEnabled` check:

```cpp
void Demo3D::sdfGenerationPass() {

    // --- OBJ mesh branch: EDT → sdfTexture, bypasses analytic shader ---
    if (useOBJMesh && !meshVoxelData.empty()) {
        if (!meshSDFReady) {
            if (!generateMeshSDF()) {
                // F1: bake failed (empty seeds, GL error, etc.). Don't flip
                // meshSDFReady — next sceneDirty cycle re-attempts the bake.
                // Render-loop sdfReady still flips to true (less invasive),
                // so the user sees whatever was previously in sdfTexture.
                std::cerr << "[ERROR] Mesh SDF bake failed; keeping meshSDFReady false\n";
                return;
            }
            meshSDFReady = true;
        }
        return;   // analytic path never runs while OBJ is active
    }

    // --- Analytic SDF branch (unchanged below this line) ---
    if (analyticSDFEnabled) {
        ...
```

The early return guarantees the analytic compute dispatch never overwrites the
mesh SDF. On `generateMeshSDF()` failure the early return still fires (so analytic
also doesn't run), but `meshSDFReady` stays false — re-clicking the OBJ button
calls `loadOBJMesh()` which sets `sceneDirty` and re-enters here.

---

## Change 3c — `setScene()` resets temporal history and clears mesh data

**File:** `src/demo3d.cpp`, line ~2154

Current source (verified):

```cpp
currentScene = sceneType;
sceneDirty = true;
useOBJMesh = false;
currentOBJPath.clear();
```

Updated:

```cpp
currentScene         = sceneType;
sceneDirty           = true;
useOBJMesh           = false;
currentOBJPath.clear();

// F6: clear mesh voxel data — no implied cache, no caller uses it.
meshVoxelData.clear();
meshVoxelData.shrink_to_fit();
meshSDFReady         = false;

// F2: scene-switch invariant — reseed temporal accumulation so the previous
// scene's history doesn't EMA-blend into the new one.
historyNeedsSeed     = true;
renderFrameIndex     = 0;
temporalRebuildCount = 0;
```

The remainder of `setScene()` (analytic primitive setup) is unchanged.

---

## Change 3d — UI gate on `useAnalyticRaymarch` while OBJ mesh active

**File:** `src/demo3d.cpp`, line ~2724

Current:

```cpp
ImGui::Checkbox("Analytic SDF (smooth, no grid)", &useAnalyticRaymarch);
```

Updated:

```cpp
ImGui::BeginDisabled(useOBJMesh);
ImGui::Checkbox("Analytic SDF (smooth, no grid)", &useAnalyticRaymarch);
if (useOBJMesh) {
    ImGui::SameLine();
    ImGui::TextDisabled("(disabled in OBJ mode)");
}
ImGui::EndDisabled();
```

When the user switches back to an analytic scene via `setScene()`, the checkbox
re-enables. The previous user setting is **not** restored — treating it as a
fresh choice avoids hidden state.

---

## Scene Switching: OBJ → Analytic / Analytic → OBJ

Both directions now share the same invariants (Change 3c covers analytic side,
Change 3a covers OBJ side):

- `meshVoxelData` cleared/replaced
- `meshSDFReady = false`
- `historyNeedsSeed = true`, `renderFrameIndex = 0`, `temporalRebuildCount = 0`
- `useAnalyticRaymarch` forced off when entering OBJ; user-controllable in analytic

Switching OBJ ↔ OBJ (Cornell OBJ → Sponza OBJ) goes through `loadOBJMesh()` for
both, which already runs all OBJ-side invariants.

---

## `voxelizationPass()` — No Changes Needed

```cpp
void Demo3D::voxelizationPass() {
    if (!sceneDirty) return;
    // stub — prints message, clears sceneDirty
    sceneDirty = false;
}
```

For the OBJ path, voxelization already happened in `loadOBJMesh()`. The stub clears
`sceneDirty` so the render loop resets `sdfReady = false`, which then triggers
`sdfGenerationPass()` where the EDT bake runs.

---

## Shader Changes

**No shader changes are required**, **provided** Step 2 v2 lands:

1. The conservative band subtraction (Step 2 reply F1) — so the existing
   `EPSILON = 1e-6` (final raymarch) and `0.002` (cascade raymarch) thresholds
   actually hit OBJ surfaces.
2. The albedo flood-fill (Step 2 reply F3) — so band-region samples don't
   trilinear-blend with empty black neighbors and produce dark surfaces.

If runtime shows OBJ surfaces still don't shade after Step 2 v2 lands, the
fallback ladder is:

1. Widen Step 2's `surfaceRadius` from `voxelSz * sqrt(3)/2` to `voxelSz * sqrt(3)`
   (full diagonal).
2. Iterate the albedo flood-fill 6 times instead of 3.
3. Only if both fail: change shader hit thresholds.

UI changes are limited to Change 3d (analytic checkbox gate).

---

## Summary of All Changes

| Location | Change | Lines (approx) |
|---|---|---|
| `demo3d.h` | Add `meshSDFReady`, `meshVoxelData`, declaration of `generateMeshSDF` | private section |
| `demo3d.cpp loadOBJMesh` | Stage-and-commit; set scene-switch invariants | ~4101–4119 |
| `demo3d.cpp sdfGenerationPass` | Mesh-branch early return; honor `generateMeshSDF()` failure | ~1250 |
| `demo3d.cpp setScene` | Clear `meshVoxelData`; reset temporal history | ~2154 |
| `demo3d.cpp` UI | Gate "Analytic SDF" checkbox on `!useOBJMesh` | ~2724 |
| `demo3d.cpp` (Step 2) | `edt1d()` static helper + `generateMeshSDF()` | new |

No shader changes.

---

## Verification Checklist

- [ ] Build succeeds; no new warnings
- [ ] Click "Cornell Box (OBJ)": console shows `[OBJLoader] Loaded: ... faces` then
      `[Demo3D] OBJ committed` then `[Demo3D] Mesh SDF: EDT complete`
- [ ] **Mode 0**: scene visually changes from analytic Cornell to OBJ-based geometry,
      surfaces actually shaded (not black)
- [ ] SDF debug (press `D`): conservative band from OBJ surfaces, not analytic primitives
- [ ] Click "Cornell Box" (analytic): switches back, analytic SDF regenerates correctly,
      `useAnalyticRaymarch` checkbox re-enables
- [ ] Click "Sponza (OBJ)": Sponza geometry visible, walls/floor/ceiling shaded
- [ ] **Temporal**: with `useTemporalAccum` ON, switch Cornell ↔ Sponza twice — no
      ghost lighting from the previous scene
- [ ] **UI gate**: while an OBJ mesh is loaded, "Analytic SDF (smooth, no grid)"
      checkbox is grayed out with "(disabled in OBJ mode)" label
- [ ] **Failure path**: rename `res/scene/sponza.obj` temporarily, click Sponza —
      confirm `[ERROR]` log and previous scene state preserved on screen
- [ ] **GI regression**: cascade output still updates after scene switch

---

## Risks

| Risk | Notes |
|---|---|
| `meshSDFReady` not reset when same OBJ reloaded | `loadOBJMesh()` always sets `meshSDFReady = false`, so re-clicking re-bakes. ✓ |
| `generateMeshSDF()` failure leaves stale SDF on screen | By design (3b): less invasive than tearing down render state. The `[ERROR]` log is the user signal; previous SDF stays visible. |
| SDF texture dimension mismatch | Step 2 uses `volumeResolution` consistently. ✓ |
| OBJ voxel count too low for correct SDF | Step 1 voxelizer fix is in place; runtime voxel-count and timing verification still pending (F7). Plan a measured run after Step 3 wiring lands; record values in a follow-up note. |
| `analyticSDFEnabled` flag interfering | Step 3b early-return executes before `analyticSDFEnabled` check. ✓ |
| `useAnalyticRaymarch` toggled before OBJ load | Forced false in `loadOBJMesh()` (Change 3a) and gated in UI (Change 3d). ✓ |
| Temporal ghosting on scene switch | `historyNeedsSeed` / `renderFrameIndex` / `temporalRebuildCount` reset in both `loadOBJMesh()` and `setScene()`. ✓ |

---

## Implementation Order

1. Land Step 2 v2 (`generateMeshSDF()`, conservative band, flood-fill, failure semantics).
2. Add `meshSDFReady` / `meshVoxelData` members (`demo3d.h`).
3. Apply Change 3c (setScene) — smallest, safest edit, validates temporal reseed in isolation.
4. Apply Change 3a (loadOBJMesh stage-and-commit) and Change 3d (UI gate) together —
   one screen-visible commit.
5. Apply Change 3b (sdfGenerationPass mesh branch) — the moment the OBJ scene
   actually appears on screen.
6. Run the verification checklist.
