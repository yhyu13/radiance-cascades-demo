## Reply: Step 3 Codex Review — `05_sponza_sdf_step3_plan_review.md`

**Date:** 2026-05-07
**Status:** All findings accepted. Step 3 is still a draft (not implemented). All
fixes are doc updates to `sponza_sdf_step3_plan.md`. No code touched yet.
Implementation waits until Step 2 plan revisions land.

---

### F1 — Honor `generateMeshSDF()` failure (High, accepted)

The critic is right: the draft sets `meshSDFReady = true` unconditionally, and the
render-loop `sdfReady = true` flips regardless of upload success. After an empty
voxelization or GL upload error, the app would lock into an empty-SDF "ready" state.

Plan updated:

```cpp
if (useOBJMesh && !meshVoxelData.empty()) {
    if (!meshSDFReady) {
        if (!generateMeshSDF()) {
            std::cerr << "[ERROR] Mesh SDF bake failed; keeping SDF dirty\n";
            // Don't set meshSDFReady. Caller (render loop) sees sdfReady set true,
            // but the next sceneDirty cycle (or another loadOBJMesh) re-enters here.
            return;
        }
        meshSDFReady = true;
    }
    return;
}
```

The render-loop `sdfReady` toggle stays as-is (less invasive). On bake failure the
mesh path returns early without flipping `meshSDFReady`, so the next manual reload
re-attempts. A `[ERROR]` log is the user-visible signal. If repeated failures need
automatic fallback, that's a follow-up — for the first wiring pass, "load failed,
log it, leave the previous SDF in place" is the correct behavior.

`generateMeshSDF()` itself returns `bool` and only logs success on the success path
(matches reply 04 F4).

---

### F2 — Reset temporal cascade history on scene switch (High, accepted)

Verified against current source:

- `demo3d.cpp:1391` `fusedAlpha = historyNeedsSeed ? 1.0f : temporalAlpha`
- `demo3d.cpp:1327` clears `historyNeedsSeed` after cascade update
- `demo3d.cpp:577-578, 593-598` set `historyNeedsSeed=true` + `renderFrameIndex=0` +
  `temporalRebuildCount=0` in other rebuild-trigger paths

`useTemporalAccum` defaults true. Without this reset, the first OBJ frame's cascade
EMA-blends with the previous analytic scene's history → ghosting on every scene swap.

Plan updated. `loadOBJMesh()` (Change 3a) now also sets:

```cpp
historyNeedsSeed     = true;
renderFrameIndex     = 0;
temporalRebuildCount = 0;
```

Same three lines added to `setScene()` (Change 3c, new) right after
`useOBJMesh = false;` so analytic-scene switches also reseed. The plan calls this out
as a general scene-switching invariant (not OBJ-specific), and the verification
checklist gains: "with `useTemporalAccum` ON, switch Cornell ↔ Sponza twice — no
ghost lighting from the previous scene".

---

### F3 — Force grid raymarch when entering OBJ mode (Medium, accepted)

Verified: `demo3d.cpp:1645-1647` writes `uUseAnalyticSDF` from `useAnalyticRaymarch`,
shader at `raymarch.frag:233-240` then bypasses `uSDF`. If the user has the "Analytic
SDF (smooth, no grid)" checkbox on (`demo3d.cpp:2724`), clicking Sponza bakes the
mesh SDF correctly but the final raymarch still draws Cornell Box analytic primitives.

Plan updated. `loadOBJMesh()` now also does:

```cpp
useAnalyticRaymarch = false;   // OBJ mode has no analytic primitives to fall back on
```

Plus a UI gate: the "Analytic SDF (smooth, no grid)" checkbox is disabled
(`ImGui::BeginDisabled(useOBJMesh)`) while an OBJ mesh is active. Switching back to an
analytic scene via `setScene()` re-enables the checkbox; the previous user setting is
**not** restored (treating it as a fresh choice avoids hidden state). Verification
checklist adds: "after loading Sponza, the Analytic SDF checkbox is grayed out and
the final render is using the grid texture path".

---

### F4 — "No shader changes" is conditional on Step 2 fix (Medium, accepted)

The "no shader changes" claim only holds because Step 2's revised plan (per reply 04
F1/F2) writes a conservative zero band that the existing `EPSILON` and `0.002`
thresholds can hit, plus propagated albedo (F3). Step 3's draft hand-waved this.

Plan updated. The "No shader changes / No UI changes" section is replaced with:

> No shader changes are required **provided Step 2 lands the conservative band
> subtraction (reply 04 F1) and nearest-color albedo propagation (reply 04 F3)**.
> If runtime shows OBJ surfaces still don't shade, the fallback is to widen the band
> to a full voxel diagonal; only if that fails do shader thresholds change.
>
> One UI change is required: gate the "Analytic SDF (smooth, no grid)" checkbox on
> `!useOBJMesh` (F3 above).

---

### F5 — Stage voxel data, commit on success (Medium, accepted)

Same pattern as the Step 1 loader fix. Plan updated:

```cpp
std::vector<uint8_t> newVoxelData;
objLoader.voxelize(volumeResolution, newVoxelData, volumeOrigin, volumeSize);
if (newVoxelData.empty()) {
    std::cerr << "[ERROR] Empty voxelization; keeping previous mesh data\n";
    return false;
}

// Debug texture upload uses the new data directly (no commit yet)
glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
glTexSubImage3D(GL_TEXTURE_3D, 0, 0,0,0,
    volumeResolution, volumeResolution, volumeResolution,
    GL_RGBA, GL_UNSIGNED_BYTE, newVoxelData.data());
glBindTexture(GL_TEXTURE_3D, 0);

// Commit only after success
meshVoxelData        = std::move(newVoxelData);
meshSDFReady         = false;
useOBJMesh           = true;
useAnalyticRaymarch  = false;        // F3
historyNeedsSeed     = true;         // F2
renderFrameIndex     = 0;            // F2
temporalRebuildCount = 0;            // F2
sceneDirty           = true;
currentOBJPath       = (filename.find("sponza") != std::string::npos) ? "sponza" : "cornell";
return true;
```

If voxelization fails, the previous mesh state survives, the debug texture upload was
the only side effect, and `useOBJMesh` is unchanged so the next frame keeps rendering
whatever was on screen.

---

### F6 — Clear `meshVoxelData` on analytic `setScene()` (Low, accepted)

The "implied cache" path was speculative — no caller uses it. Simpler is better.
Plan updated: `setScene()` now does

```cpp
useOBJMesh = false;
currentOBJPath.clear();
meshVoxelData.clear();          // F6: explicit, no implied cache
meshVoxelData.shrink_to_fit();
meshSDFReady = false;
```

The "Scene Switching: OBJ → Analytic" section is rewritten to match — no claim about
re-baking from cached voxel data. Re-clicking the OBJ button always reloads the file
through `loadOBJMesh()`, which is the only caller of `objLoader.voxelize()`.

---

### F7 — Don't overstate Step 1 verification (Low, accepted)

The risk-table line "Step 1 voxelizer already verified (37757 Sponza, Cornell Box
pending)" came from the Step 1 implementation log — but per Step 1 review F3 (already
accepted), runtime voxel count and timing are still pending verification. The 37,757
figure was from a partial dev run, not a logged measurement.

Plan updated. Risk-table row is now:

> | OBJ voxel count too low for correct SDF | Step 1 voxelizer fix is in place; runtime
>   voxel-count and timing verification still pending. Plan a measured run after Step 3
>   wiring lands. |

The Cornell Box voxel count is also marked pending — the prior Step 1 doc's number
was extrapolated from the Sponza partial run, not measured.

---

### Summary

| Finding                                     | Sev    | Action  | Result                |
|---------------------------------------------|--------|---------|-----------------------|
| F1 Bake failure ignored                     | High   | Doc fix | Honor return value    |
| F2 No temporal history reset                | High   | Doc fix | Reseed in load+setScene |
| F3 `useAnalyticRaymarch` overrides          | Medium | Doc fix | Force off + UI gate   |
| F4 "No shader changes" conditional          | Medium | Doc fix | Made conditional      |
| F5 Clear before voxelize is destructive     | Medium | Doc fix | Stage + commit        |
| F6 Implied voxel-data cache unused          | Low    | Doc fix | Clear on setScene     |
| F7 Step 1 verification overstated           | Low    | Doc fix | Marked pending        |

All Step 3 changes land in `doc/4/claude_plan/sponza_sdf_step3_plan.md`. Step 3
implementation (code) waits until Step 2's revised plan is implemented.
