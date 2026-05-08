# Review: sponza_sdf_step5_impl.md

Reviewed: 2026-05-08T13:18:47+08:00

Target: `doc/4/claude_plan/sponza_sdf_step5_impl.md`

Verdict: mostly aligned with the revised Step 5 architecture, but not fully clean. The important source-level choices are right: camera movement does not reseed cascades, `R` no longer reloads OBJ files, mouse look uses yaw/pitch plus `GetMouseDelta()`, and visible `D`/`F1`/`R` shortcut labels were mostly cleaned up. The remaining problems are a real UI reset-path bug and several verification overclaims in the implementation note.

## Evidence Checked

- `src/demo3d.cpp:389-554`: `processInput()` now owns F1 debug toggling, RMB mouse look, WASD/QE movement, wheel zoom/FOV, and `R` scene-preset reset.
- `src/demo3d.cpp:523-530`: `R` applies `applyOBJViewPreset(currentOBJPath)` for OBJ scenes and `resetCamera()` for analytic scenes.
- `src/demo3d.cpp:3016-3018`: the ImGui "Reset Camera" button still calls `resetCamera()` unconditionally.
- `src/demo3d.cpp:4402-4418`: `resetCamera()` is still the Cornell/default `(0,0,4)` camera reset.
- `src/demo3d.cpp:4425-4439`: `syncCameraYawPitchFromTarget()` derives yaw/pitch from the current forward vector.
- `src/demo3d.cpp:4441-4506`: `applyOBJViewPreset()` applies Sponza/Cornell camera and light presets without file I/O or voxelization.
- `src/demo3d.cpp:4535-4628`: `loadOBJMesh()` still owns OBJ loading/normalization/voxelization and now calls the preset helper after commit.
- `src/demo3d.cpp:2510-2520`, `src/demo3d.cpp:4615-4617`: scene/OBJ changes still reseed temporal history; camera movement in `processInput()` does not.
- Step 5 logs checked: `tools/app_run_step5_helper.log`, `tools/app_run_step5_headless.log`, `tools/app_run_step5_cornell_headless.log`, and `tools/app_run_step5_interactive.log`.
- Screenshot hashes and pixel comparisons checked for `tools/step4v2_*_mode0.png` versus `tools/step5_*_headless.png`.
- I attempted the claimed clean Release build command, `cmake --build . --config Release --clean-first`, from `build/`; it failed at link because `RadianceCascades3D.exe` is currently locked by a running `RadianceCascades3D` process.

## What I Agree With

- The implementation correctly deleted the camera-motion temporal reseed idea. I found no `historyNeedsSeed` update in the camera movement path.
- The `R` key path fixes the worst Step 5 plan risk: it applies a preset helper instead of calling `loadOBJMesh()`.
- The yaw/pitch scalar approach is materially safer than normalizing a cross product near the poles.
- The split ImGui capture design is directionally right: keyboard movement is not blocked just because the mouse is over a UI panel, while RMB start and wheel remain mouse-capture aware.
- The main visible key-label cleanup landed in current source: startup text, SDF overlay, SDF button, and Quick Start text now say `F1`/camera reset instead of stale `D`/shader reload text.

## Findings

### F1 - The ImGui Reset Camera button bypasses OBJ scene presets

Severity: medium-high.

Impl refs: lines 20-28, 83-88, 193-195.

Code refs: `src/demo3d.cpp:3016-3018`, `src/demo3d.cpp:523-530`, `src/demo3d.cpp:4441-4506`.

The `R` key does the right thing for OBJ scenes:

```cpp
if (useOBJMesh && !currentOBJPath.empty()) {
    applyOBJViewPreset(currentOBJPath);
} else {
    resetCamera();
}
```

But the settings-panel button still does this:

```cpp
if (ImGui::Button("Reset Camera")) {
    resetCamera();
}
```

In a Sponza OBJ scene, that button sends the user to the Cornell/default camera `(0,0,4)` instead of the Sponza preset `(3.5,0.5,0)`. It also bypasses the helper that syncs Sponza's camera/light preset semantics. This leaves two reset behaviors in the UI: keyboard reset is scene-aware, button reset is not.

Recommendation: add a single `resetCameraToScenePreset()` helper and call it from both the `R` key and the ImGui button. If the button intentionally means "reset to default Cornell camera," rename it; otherwise it should use the same scene-aware path as `R`.

### F2 - Interactive verification is asserted but not preserved

Severity: medium.

Impl refs: lines 180-202.

The implementation note says the interactive gates were exercised by user-driven keypress and that the preserved log contains per-action lines for F1, R, Sponza reset, and analytic reset. The preserved log does not show that. `tools/app_run_step5_interactive.log` contains startup/load lines and one initial `Applied sponza view preset`, but no:

- `SDF Debug View: ON` or `OFF`
- `Camera reset to scene preset (R key)`
- second `Applied sponza view preset` after pressing `R`
- analytic-scene `Camera reset to position` after pressing `R`
- movement, wheel/FOV, ImGui-hover, or cursor-cleanup evidence

This does not prove the controls are broken, but it means the implementation note's verification claim is stronger than the evidence. The hardest parts of Step 5 are interactive state transitions, and the preserved artifact does not capture them.

Recommendation: either downgrade the note to "source-reviewed, launched for manual testing" or preserve a real manual verification log/screenshot set. For this codebase, a useful minimum would be: F1 toggle log, R reset log in Sponza, R reset in analytic, before/after camera position logs for movement/zoom, and clean mode 0/1/4 captures after manually driving into Sponza.

### F3 - The Sponza headless capture is not byte-identical to Step 4 v2

Severity: medium.

Impl refs: lines 8-10, 164-174.

The note claims "headless captures byte-identical to Step 4 baseline." That is true for Cornell, but false for Sponza.

Checked hashes:

- `step4v2_cornell_mode0.png` and `step5_cornell_headless.png`: identical SHA-256.
- `step4v2_sponza_mode0.png` and `step5_sponza_headless.png`: different SHA-256.

Pixel comparison for Sponza:

- Dimensions: both `1280x720`.
- Changed pixels: `465168 / 921600`.
- Sum absolute RGBA difference: `81369312`.

The two Sponza runs still have the same OBJ scale, seed count, preset camera, and final screenshot path, so this may be temporal/render-run variance or a difference in capture side effects. But it is not byte-identical, and it is far larger than a few metadata bytes.

Recommendation: replace the byte-identical claim with the actual result: Cornell is byte-identical; Sponza differs and needs either a root cause or a visual/statistical tolerance statement. If Step 5 intends to prove "no input means no behavior change," re-run both baselines under matching capture conditions and compare those artifacts.

### F4 - The claimed clean Release build is not currently reproducible

Severity: medium.

Impl refs: lines 7-8, 31-33, 156-162.

I attempted the exact command from the note:

```powershell
cmake --build . --config Release --clean-first
```

from `build/`. The build recompiled the dependency stack and project sources, but failed at link:

```text
LINK : fatal error LNK1104: cannot open file "...\build\RadianceCascades3D.exe"
```

`Get-Process` shows a running `RadianceCascades3D` process (`Id 26320`), so the likely cause is that the interactive app launched for Step 5 is still holding the executable. That makes the current state different from the note's "0 errors" claim, even if an earlier build succeeded.

Recommendation: close the running app, rerun the exact clean Release build, and preserve the build log. Until then, the implementation note should say the local clean build is not presently reproducible because the executable is locked.

### F5 - One preserved Step 5 smoke log predates the final label patch

Severity: low.

Impl refs: lines 142-150, 176-177.

`tools/app_run_step5_helper.log` still prints:

```text
[Demo3D] SDF Debug View: Press 'D' to toggle
```

The later Step 5 headless and interactive logs print `Press F1 to toggle`, and current source is updated, so this is not a current source bug. It is an evidence hygiene issue: the helper smoke log cannot be used to prove the final label patch because it was captured before that patch landed.

Recommendation: either refresh the helper log or annotate it as a pre-label-patch helper smoke. Keep the F1 label proof tied to current source and the newer headless logs.

## Residual Risk

The core input implementation can be accepted after fixing the UI reset path and tightening the verification record. The bigger Sponza renderer risk remains unchanged from the Step 5 plan review: moving the camera into the atrium is not proof that inside-Sponza raymarching works. The implementation note correctly defers that to a later surface-band follow-up, but the Step 5 acceptance story should not call "free-fly through Sponza" complete until at least one moved inside-atrium mode 0/1/4 capture is preserved.

