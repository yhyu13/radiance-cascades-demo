# Review: Sponza SDF Step 0 Implementation Note

Review timestamp: 2026-05-06T17:36:42+08:00

Target: `doc/4/claude_plan/sponza_sdf_step0_impl.md`

Verdict: Step 0 is directionally correct but incomplete as an implementation record. The Sponza OBJ button and `currentOBJPath` member exist in the current source, but the new two-OBJ workflow exposes existing state bugs in `OBJLoader` and scene selection. Fix those before building later mesh-SDF steps on top of this UI entry point.

## What Matches Current Source

- `src/demo3d.h:621-624` has `useOBJMesh` and `currentOBJPath`.
- `src/demo3d.cpp:3448` gates the Cornell OBJ button with `useOBJMesh && currentOBJPath == "cornell"`.
- `src/demo3d.cpp:3456-3460` adds the `Sponza (OBJ)` button and calls `loadOBJMesh("res/scene/sponza.obj")`.
- `src/demo3d.cpp:4111-4112` sets `useOBJMesh=true` and classifies the loaded OBJ as `"sponza"` or `"cornell"`.

## Findings

### 1. High - OBJ loads accumulate old geometry

The Step 0 note makes switching between Cornell OBJ and Sponza OBJ a normal workflow, but `OBJLoader::load()` does not clear its member arrays before parsing a new file.

Current code evidence:

- `src/obj_loader.h:48` starts `bool load(const std::string& filename)`.
- Searches for `vertices.clear`, `faces.clear`, `normals.clear`, `texcoords.clear`, and `faceMaterials.clear` return no matches.
- `src/demo3d.cpp:4062` reuses the same `objLoader` member for every `loadOBJMesh()` call.

Impact:

- Loading Cornell OBJ, then Sponza OBJ appends Sponza data onto the old Cornell data.
- Re-clicking Sponza appends Sponza again.
- `objLoader.normalize()` then normalizes the accumulated mesh, not the newly loaded mesh.
- Voxelization and future mesh-SDF generation will operate on mixed or duplicated geometry.

Recommended fix:

Clear loader state at the start of `OBJLoader::load()`:

```cpp
vertices.clear();
normals.clear();
texcoords.clear();
faces.clear();
faceMaterials.clear();
```

If materials are added later, clear those too.

### 2. High - Analytic scene buttons do not leave OBJ mode

The note focuses on disambiguating OBJ active buttons, but current analytic scene buttons call `setScene()` without resetting OBJ state.

Current code evidence:

- `src/demo3d.cpp:2138` begins `Demo3D::setScene(int sceneType)`.
- `src/demo3d.cpp:2155` sets `currentScene = sceneType` and `sceneDirty = true`.
- There is no `useOBJMesh = false` or `currentOBJPath.clear()` in `setScene()`.
- `src/demo3d.cpp:4111-4112` sets `useOBJMesh=true` and `currentOBJPath` after OBJ load.

Impact:

- Click `Sponza (OBJ)`, then click `Cornell Box`: `useOBJMesh` remains true.
- The Sponza OBJ button can still show active even after an analytic scene was selected.
- When Step 3 adds a `useOBJMesh` branch to `sdfGenerationPass()`, this stale flag can make analytic scene selection keep using the previous mesh-SDF path.

Recommended fix:

At the top of `setScene()` for non-OBJ scene selection, clear OBJ state:

```cpp
useOBJMesh = false;
currentOBJPath.clear();
```

When future mesh voxel/SDF buffers are added, clear or invalidate them there too.

### 3. Medium - The visible "Active:" text is still wrong for Sponza

The note says the new tracking gives unambiguous active-state display. The buttons are disambiguated, but the summary label below the buttons still hardcodes Cornell.

Current code evidence:

- `src/demo3d.cpp:3467-3468` says:

```cpp
if (useOBJMesh)
    ImGui::TextColored(..., "Active: Cornell Box (OBJ)");
```

Impact:

- After loading Sponza OBJ, the Sponza button can say `[ACTIVE] Sponza (OBJ)`, but the summary line still says `Active: Cornell Box (OBJ)`.
- This directly contradicts the Step 0 goal of unambiguous active-state display.

Recommended fix:

Drive the active text from `currentOBJPath`:

```cpp
if (useOBJMesh) {
    const char* objName = (currentOBJPath == "sponza") ? "Sponza (OBJ)" : "Cornell Box (OBJ)";
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active: %s", objName);
}
```

### 4. Medium - "Complete" overstates the runtime verification state

The note status says `Complete - build clean`, but the runtime checklist is entirely unchecked:

- `sponza_sdf_step0_impl.md:93-96` leaves button visibility, click behavior, active label, and Cornell/Sponza exclusivity unchecked.

The build result is useful, but Step 0 is a UI entry point. Runtime verification is the main acceptance criterion.

Recommended fix:

Change the status to `Build complete, runtime verification pending` until the UI checklist is run. After runtime verification, include the exact executable/build config used and whether the app was launched from project root.

### 5. Low - Filename classification is acceptable for two OBJs but brittle

The note says the filename-based classification is robust for the two known paths. That is true for `res/scene/sponza.obj` and `res/scene/cornell_box.obj`, but the code falls back to `"cornell"` for any non-Sponza OBJ:

- `src/demo3d.cpp:4112` uses `(filename.find("sponza") != std::string::npos) ? "sponza" : "cornell"`.

This is fine for Step 0's two-button scope, but future OBJ scenes should use an enum or explicit scene id passed into `loadOBJMesh()` rather than substring classification.

## Recommended Fix Order

1. Clear `OBJLoader` arrays at the start of `OBJLoader::load()`.
2. Clear `useOBJMesh` and `currentOBJPath` inside `setScene()` when selecting analytic scenes.
3. Fix the summary `Active:` label to use `currentOBJPath`.
4. Run the runtime checklist and downgrade or update the note's status accordingly.
5. Leave filename-based classification until a third OBJ scene exists, then replace it with explicit scene identity.

## Bottom Line

Step 0 added the right visible entry point, but it is not a safe foundation yet. The UI now lets users switch OBJ scenes, while the underlying loader still accumulates geometry and the analytic scene path does not exit OBJ mode.
