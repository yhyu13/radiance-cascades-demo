# Review: sponza_sdf_step5_plan.md

Reviewed: 2026-05-07T23:48:21+08:00

Target: `doc/4/claude_plan/sponza_sdf_step5_plan.md`

Verdict: good next milestone, but revise before implementation. Interactive camera control is the right user-facing step after fixed Sponza visibility. The plan should not force cascade temporal reseeds on camera movement, should not reload OBJ files just to reset the camera, and should tighten the input-capture/key-binding design before coding.

## Evidence Checked

- `src/demo3d.cpp:389-464`: `processInput()` handles debug keys only; camera controls are still the placeholder comment.
- `src/main3d.cpp:223-225`: `processInput()` is already called once per frame.
- `src/demo3d.cpp:1861-1870`: the final raymarch camera uniforms are built from `camera.position`, `camera.target`, and `camera.fovy`.
- `res/shaders/radiance_3d.comp`: the cascade bake has no camera dependency; probes are world/volume-space.
- `src/demo3d.cpp:1644-1648`, `src/demo3d.cpp:1721-1730`: temporal history seeding only matters during cascade update work.
- `src/demo3d.cpp:507-711`: `cascadeReady` is a local static in `render()` and is not invalidated by camera changes.
- `src/demo3d.cpp:2427-2432`: current source has already fixed the Step 4 light reset in `setScene()`.
- `src/demo3d.cpp:4481-4508`: current source has already fixed outside-volume camera alpha validation.
- `src/main3d.cpp:234-265`: current source has already moved CLI screenshots to a clean pre-ImGui capture point.
- `doc/4/claude_plan/codex_critic/claude_reply/reply_09_sponza_sdf_step4_impl_review.md`: Step 4 reply confirms the inside-atrium camera remains an explicit follow-up.
- Current incremental MSBuild of `build/RadianceCascades3D.vcxproj` completed with 0 errors and 0 warnings because outputs were up to date; this is not a clean warning baseline.

## What I Agree With

- Step 5 should be interactive camera control, not another hardcoded Sponza camera iteration.
- `processInput()` is the right integration point because it already runs once per frame before update/render.
- Free-fly movement is enough for the first implementation; orbit mode can wait.
- `KEY_D` cannot remain both "SDF debug toggle" and "strafe right" without annoying UX.
- Suppressing camera wheel zoom while SDF debug slicing is active is the right ownership split.

## Findings

### F1 - Camera movement alone may not satisfy "walk through Sponza"

Severity: high.

Plan refs: lines 11-14, 85-98, 281-291, 363-373.

The plan frames Step 5 as letting the user "walk through Sponza" and inspect the atrium from any angle. Step 4 found a more specific blocker: the first inside-atrium camera was in free space but produced black mode 0/1/4 captures, likely because the conservative-band UDF was not robust enough for that view. That is recorded as an open follow-up in the Step 4 reply.

Adding camera movement will let the user move the camera, but it will not by itself make inside-atrium raymarching reliable. If the user drives from the outside preset into the atrium, the renderer may hit the same grazing/thin-band failure that made the original inside preset black.

Recommendation: keep Step 5 as "free-fly camera control" but make "walk through Sponza interior works" an explicit verification gate, not an assumed result. Capture clean mode 0/1/4 from at least one manually moved inside-atrium view. If it fails, that is not a Step 5 input bug; it rolls back to the Step 2/4 SDF-band follow-up.

### F2 - `historyNeedsSeed` on camera movement is conceptually wrong and may be ineffective

Severity: high.

Plan refs: lines 193-216, 302-305, 324.

The cascades are scene-space, not view-space. `radiance_3d.comp` does not use camera position. Camera movement only changes final ray generation in `raymarch.frag`; it does not change SDF, albedo, light transport, probe positions, or the baked radiance field.

The proposed 5e block also does not force any work by itself. `historyNeedsSeed` is consumed only during cascade updates. If `cascadeReady` is already true, setting `historyNeedsSeed=true` from `processInput()` will not rebuild cascades because `cascadeReady` is a local static in `render()` and is not exposed to `processInput()`.

If the implementation also starts forcing `cascadeReady=false` every movement frame, that would be both expensive and mostly unnecessary. The plan already notes cascade rebuild cost risk; the better fix is to not rebuild the scene-space GI for camera-only movement.

Recommendation: remove 5e for camera movement. If a later feature moves the light, scene geometry, SDF parameters, or probe settings, route that through an explicit scene-lighting dirty path that invalidates cascades and reseeds temporal history. Camera movement should just update the final raymarch view.

### F3 - R-key reset should not reload OBJ assets

Severity: high.

Plan refs: lines 168-185, 263-267, 352.

The proposed `R` behavior calls `loadOBJMesh()` for OBJ scenes. That is too heavy and changes too much state for a camera reset:

- Re-parses the OBJ file.
- Re-normalizes and re-voxelizes.
- Replaces `meshVoxelData`.
- Sets `meshSDFReady=false`, `sceneDirty=true`, and temporal counters.
- Triggers a CPU EDT bake on the next frame.
- Can fail due file/path issues even though the current mesh is already loaded.

Resetting a camera should be a small state assignment, not a full scene reload.

Recommendation: extract the Step 4 preset code into a helper such as `applyOBJViewPreset(objKind)` or `applySceneViewPreset()`. `loadOBJMesh()` should call it after commit, and `R` should call the helper without touching mesh data. If Sponza light reset is coupled to the view preset, keep that in the helper too, but only mark cascades dirty if the light actually changes.

### F4 - ImGui capture handling is too coarse for navigation

Severity: medium.

Plan refs: lines 35-37, 96-104, 307-310, 327.

The current early return is:

```cpp
if (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard) {
    return;
}
```

That is fine for debug hotkeys, but it is too coarse for FPS-style camera controls. Because the quick-start panel covers a large part of the viewport, merely having the mouse over UI can block keyboard movement even when the user is not typing. It can also make right-mouse-drag state fragile: if `mouseDragging` is true and ImGui captures the mouse before the release is processed, `EnableCursor()` can be skipped and the drag state can stick.

Recommendation: split keyboard and mouse ownership. Keyboard movement should be gated by `WantCaptureKeyboard`, not by `WantCaptureMouse`. Right-mouse camera look should start only when `!WantCaptureMouse`, but release/cursor cleanup should be processed even if ImGui later captures mouse. Treat cursor cleanup as a safety path, not optional UI input.

### F5 - The F1/D/R key plan needs UI-label cleanup

Severity: medium.

Plan refs: lines 82-87, 188-191, 256-257, 307-310.

Rebinding SDF debug off `D` is necessary if `D` becomes strafe-right. But `F1` is already presented to users as "Toggle UI" in the Quick Start panel (`src/demo3d.cpp:3712`), even though the actual `KEY_F1` toggle is not wired today. SDF debug is also labeled as `(D)` in the button and overlay (`src/demo3d.cpp:3767`, `src/demo3d.cpp:1218`).

`R` is similar: it is not wired as shader reload, but the Quick Start text still says "R: Reload shaders" (`src/demo3d.cpp:3711`). If Step 5 makes `R` reset the camera, that visible text must change.

Recommendation: choose the whole key map before implementation and update all visible labels in the same patch. Options:

- Use `F1` for UI and pick another SDF key, such as `Z`, `C`, or `Ctrl+D`.
- Or use `F1` for SDF debug and remove/replace the "F1: Toggle UI" text everywhere.
- Change the Quick Start `R` text to "Reset camera" if `R` becomes camera reset.

### F6 - Mouse-look math needs safer basis handling

Severity: medium.

Plan refs: lines 96-131, 140-141, 325-326.

The pitch/yaw snippet can produce bad vectors when the camera looks near world-up or world-down:

```cpp
glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0,1,0)));
```

If `forward` is nearly parallel to world-up, the cross product is near zero. The pitch clamp happens after this, so it does not protect the `normalize()` call.

The code also uses `GetMouseX()/GetMouseY()` plus `lastMousePos` while `DisableCursor()` is active. In cursor-locked mode, Raylib's `GetMouseDelta()` is the more direct API and avoids absolute-position surprises.

Recommendation: implement mouse look with `GetMouseDelta()` and a safe camera basis. Either maintain yaw/pitch scalars with a pitch clamp, or guard the `right` vector length before normalizing and fall back to the previous right vector.

### F7 - Some source facts and verification gates are stale or undecided

Severity: low to medium.

Plan refs: lines 41-44, 277-300.

`loadOBJMesh()` applies OBJ camera presets, but `setScene()` does not reset the camera for analytic scenes. It resets scene state and now resets `lightPosition`, but it does not call `resetCamera()`. The plan says both `setScene()` and `loadOBJMesh()` reset camera per-scene, then later leaves analytic scene behavior undecided: "camera unchanged from previous (or reset?)".

The warning gate is also stale. The plan says "Step 4 baseline (37 in `3d/src/`)"; that number is Release/config-specific from the Step 4 reply. A normal incremental Debug MSBuild can report 0 warnings when no files rebuild, while the prior full Debug compile reported 39. A verification gate should name the exact configuration and force a rebuild when counting warnings.

Recommendation: decide analytic scene camera behavior before implementation. For build verification, write "Debug/Release, clean/incremental" explicitly, and do not compare against a bare number without the command that produced it.

## Recommended Revision Before Coding

1. Keep 5a/5b/5c, but implement keyboard and mouse capture separately.
2. Replace 5d's OBJ reload with a preset helper shared by `loadOBJMesh()` and `R`.
3. Remove 5e for camera-only movement; do not invalidate cascades for view changes.
4. Pick a final hotkey map and update all Quick Start, debug overlay, and button labels in the same patch.
5. Add a clean inside-Sponza verification capture after manual movement; that is the real acceptance test for "walk through Sponza."
6. Make the build-warning gate command-specific.

