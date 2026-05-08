# Critic Review 12 - sponza_sdf_step6_impl.md

Reviewed: 2026-05-08T15:28:33+08:00

Target: `doc/4/claude_plan/sponza_sdf_step6_impl.md`

## Verdict

Step 6 is a useful implementation pass, but the implementation note overstates the evidence in a few important places. The local assets now exercise material parsing, negative vertex indices, and n-gon fan triangulation, and the Release build completes with 0 errors. However, the new four-way OBJ identity keys broke the scene-aware reset path for the newly added variants, and the Sponza-master density story does not match the current files or logs.

## Evidence Checked

- Source diff in `src/obj_loader.h`, `src/demo3d.cpp`, and `src/main3d.cpp`.
- Step 6 runtime logs in `tools/app_run_step6_*.log`.
- Step 6 screenshots in `tools/step6_*.png`, compared against Step 4/5 baseline screenshots.
- Asset shape for `res/scene/sponza.obj`, `res/scene/Sponza-master/sponza.obj`, and `res/scene/CornellBox-Original/CornellBox-Original.obj`.
- Clean Release rebuild with `cmake --build . --config Release --clean-first` from `build/`: completed successfully with 0 errors.

## What Looks Correct

- `OBJLoader::load()` now handles `mtllib`, clears material state per load, and resolves MTL paths relative to the OBJ file directory.
- The parser now accepts faces with more than three vertices and fan-triangulates them.
- Negative vertex indices are resolved for the Cornell-Original OBJ.
- Cornell-Original is now loaded as 32 triangles and voxelizes to 39,648 mesh SDF seeds, which matches the Step 6 log.
- Sponza-master MTL parsing is intentionally minimal but sufficient for the current claim: the local `sponza.mtl` has 25 materials and the visible `Kd` values are uniform mid-gray.
- The old Cornell capture is pixel-identical to the Step 5 Cornell capture, so legacy Cornell behavior was preserved in that tested path.

## Findings

### 1. New Step 6 OBJ variants break scene-aware camera reset

Severity: High

`loadOBJMesh()` now stores four-way keys in `currentOBJPath`: `cornell`, `cornell_orig`, `sponza`, or `sponza_master`. But `resetCameraToScenePreset()` passes `currentOBJPath` directly to `applyOBJViewPreset()`, and `applyOBJViewPreset()` only accepts `cornell` or `sponza`.

Evidence:

- `src/demo3d.cpp`: `resetCameraToScenePreset()` calls `applyOBJViewPreset(currentOBJPath)`.
- `src/demo3d.cpp`: `applyOBJViewPreset()` has branches only for `sceneKey == "sponza"` and `sceneKey == "cornell"`, then warns on unknown keys.
- `src/demo3d.cpp`: Step 6 changed active OBJ keys to include `cornell_orig` and `sponza_master`.

Impact: after loading Cornell-Original or Sponza-master, the `R` key and ImGui `Reset Camera` button no longer reset to the scene preset. This is a direct regression against the Step 5 camera-control behavior that was just fixed.

Suggested fix: either make `applyOBJViewPreset()` accept the aliases, or store both a four-way display/source key and a two-way preset key, then pass the two-way key to the camera preset helper.

### 2. The Sponza-master density comparison is not supported by the current files

Severity: High

The implementation note claims the old root Sponza path is about 75K faces while Sponza-master is 262,267 faces, and it interprets equal seed counts as evidence that extra face density does not add more occupied voxels. The current logs do not support that.

Evidence:

- `tools/app_run_step6_sponza_old.log`: old root `sponza.obj` loads 145,185 vertices and 262,267 faces.
- `tools/app_run_step6_sponza_master.log`: Sponza-master loads 145,185 vertices and 262,267 faces.
- `res/scene/sponza.obj` and `res/scene/Sponza-master/sponza.obj` have the same apparent size and layout in this workspace.

Impact: the equal seed count is not evidence of saturation under a 3.5x face-count increase. In the current workspace it is the same geometry being loaded through two different material-path situations. The old Sponza path lacks adjacent MTL resolution in the tested layout, while the Sponza-master path resolves the MTL.

Suggested fix: correct the note and, if the older 75K asset matters, verify against an asset that is actually distinct in this workspace.

### 3. The old-Sponza visual regression claim is too strong

Severity: Medium

The note says the old Sponza mode-0 capture visually matches the Step 4 v2 baseline. Pixel comparison shows it is not close enough to use as strong regression evidence.

Evidence:

- `tools/step4v2_sponza_mode0.png` vs `tools/step6_sponza_old_mode0.png`: 450,754 changed pixels out of 921,600, with summed absolute RGB difference 48,013,484.
- `tools/step5_sponza_headless.png` vs `tools/step6_sponza_old_mode0.png`: 597,384 changed pixels, with summed absolute RGB difference 89,181,500.
- By contrast, `tools/step5_cornell_headless.png` vs `tools/step6_cornell_old_mode0.png` is byte/pixel identical.

Impact: Cornell legacy preservation is well supported. Sponza legacy preservation should be described as qualitative/manual at most, unless a camera, asset, and render-mode-matched baseline is used.

### 4. Runtime verification logs are not clean

Severity: Medium

Every inspected Step 6 runtime log still contains the existing `res/shaders/sdf_3d.comp` shader compile failure around `imageLoad(...)`, after which the app continues and later screenshots are captured.

Impact: this may be an existing unrelated shader issue, but the implementation note should not imply the runtime logs are clean. The logs should explicitly call this out as a known pre-existing warning/error path if it is accepted.

### 5. OBJ face parsing is broader, but still lacks resolved-index validation

Severity: Medium

The new parser resolves negative vertex indices, but it still does not validate that resolved vertex indices are in range before later voxelization dereferences `vertices[face.v[i]]`.

Impact: the current Cornell-Original and Sponza-master assets are fine, but malformed OBJ files can still produce undefined behavior or crashes. Since Step 6 is framed as making future OBJ ingestion more robust, this should be tightened.

Suggested fix: reject or skip triangles with any resolved index outside `[0, vertices.size())`, and log a bounded warning count.

### 6. Unknown-material logging is misleading for legacy fallback materials

Severity: Low

The old Cornell log reports messages such as unknown material `Light`, `Khaki`, `BloodyRed`, and `DarkGreen`, but several of those are handled by the legacy hardcoded material-color fallback rather than true default-gray fallback.

Impact: this makes the logs look like a failure path even when legacy fallback is working as intended.

Suggested fix: distinguish parsed-MTL hits, legacy fallback hits, and true default-gray misses in the log text.

### 7. CLI documentation/comment is stale

Severity: Low

`src/main3d.cpp` still describes `--load-obj=NAME` as `(cornell|sponza)`, while the parser now also accepts `cornell-orig` and `sponza-master`.

Impact: minor, but it is easy to fix and prevents future verification scripts from missing the new entry points.

## Bottom Line

The parser and material implementation is directionally correct for the two new local assets, and the Cornell-Original result is well demonstrated. The Step 6 write-up should be corrected before being treated as a clean milestone: fix the new camera reset regression, revise the Sponza-master comparison, and document the shader-log caveat.
