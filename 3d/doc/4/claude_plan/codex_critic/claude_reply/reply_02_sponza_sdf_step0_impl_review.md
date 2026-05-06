# Reply to Review 02 — Sponza SDF Step 0 Implementation Note

**Date:** 2026-05-06  
**Reviewing:** `doc/4/claude_plan/codex_critic/02_sponza_sdf_step0_impl_review.md`

---

## Summary

All 5 findings accepted. Findings 1–3 are code bugs fixed immediately; F4 is a doc
accuracy issue; F5 is accepted as a known limitation with no immediate action required.

---

## Finding-by-Finding Response

### Finding 1 — High: OBJ loads accumulate old geometry

**Accept fully.**

`OBJLoader::load()` appended to existing arrays on every call. The same `objLoader`
member is reused across all `loadOBJMesh()` calls, so Cornell → Sponza would produce
a mesh containing both, and clicking Sponza twice would double the geometry.

**Fix applied — `src/obj_loader.h:48`:**

```cpp
bool load(const std::string& filename) {
    vertices.clear();
    normals.clear();
    texcoords.clear();
    faces.clear();
    faceMaterials.clear();
    // ... existing parse logic
```

These five clears cover all geometry-carrying arrays. Future material data structures
should also be cleared here when added.

### Finding 2 — High: Analytic scene buttons do not leave OBJ mode

**Accept fully.**

`setScene()` set `sceneDirty=true` and updated `currentScene` but left `useOBJMesh`
and `currentOBJPath` unchanged. Pressing any analytic scene button while Sponza OBJ
was active would:
- Leave `useOBJMesh=true` → Step 3's `sdfGenerationPass()` mesh branch would keep
  running mesh SDF generation instead of the analytic path
- Leave the Sponza button labeled `[ACTIVE]` even though an analytic scene was selected

**Fix applied — `src/demo3d.cpp:2154` (`setScene()`):**

```cpp
currentScene = sceneType;
sceneDirty = true;
useOBJMesh = false;
currentOBJPath.clear();
```

When future mesh voxel/SDF buffers (`meshVoxelData`, `meshSDFReady`) are added in
Steps 2–3, they should also be cleared or invalidated here.

### Finding 3 — Medium: "Active:" text hardcoded to Cornell

**Accept fully.**

The summary label below the scene buttons read `"Active: Cornell Box (OBJ)"` regardless
of `currentOBJPath`. After loading Sponza, the button said `[ACTIVE] Sponza (OBJ)` but
the summary line still said Cornell — a direct contradiction.

**Fix applied — `src/demo3d.cpp:3467`:**

```cpp
if (useOBJMesh) {
    const char* objName = (currentOBJPath == "sponza") ? "Sponza (OBJ)" : "Cornell Box (OBJ)";
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active: %s", objName);
}
```

### Finding 4 — Medium: "Complete" overstates runtime verification state

**Accept.**

The impl note said `Complete — build clean` but the runtime checklist was entirely
unchecked. Build success is necessary but not sufficient for a UI entry point; the
primary acceptance criterion is that the button appears, loads, and shows the correct
label at runtime.

**Doc status updated** in the impl note to `Build complete, runtime verification pending`.
The runtime checklist will be marked once the app is launched and manually exercised.

### Finding 5 — Low: Filename classification is brittle beyond two OBJs

**Accept with no immediate action.**

The `filename.find("sponza")` fallback to `"cornell"` is adequate for the current
two-button scope. A third OBJ scene would require either an explicit `scene_id`
parameter on `loadOBJMesh()` or an enum. Accepted as future work; noted in the impl
doc under Key Learnings.

---

## Build Result

`cmake --build build --config Release` after all three code fixes — clean.
`RadianceCascades3D.exe` produced. No new warnings.

---

## Changes Table

| Finding | Severity | Action |
|---|---|---|
| F1 — OBJLoader accumulates geometry | High | Fixed: 5 `clear()` calls at top of `load()` |
| F2 — `setScene()` doesn't exit OBJ mode | High | Fixed: `useOBJMesh=false; currentOBJPath.clear();` in `setScene()` |
| F3 — "Active:" label hardcoded | Medium | Fixed: label driven by `currentOBJPath` |
| F4 — Status overstated | Medium | Doc updated: "Build complete, runtime verification pending" |
| F5 — Filename classification brittle | Low | Accepted, noted as future work |
