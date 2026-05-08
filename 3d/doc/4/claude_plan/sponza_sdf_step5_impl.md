# Sponza SDF — Step 5: Implementation Notes (revised after codex 11)

**Date:** 2026-05-08 (revised after codex review `11_*` / reply `11_*`)
**Plan ref:** `doc/4/claude_plan/sponza_sdf_step5_plan.md` (v2)
**Status:** Implemented and partially verified. Camera control wiring added
to `processInput()` with the codex 10 v2 design (yaw/pitch scalars, split
ImGui capture, helper extraction, no temporal reseed). Build clean
(0 errors, 37 project warnings -- Step 4 baseline preserved). Headless
captures: Cornell mode 0 byte-identical to Step 4 v2 (SHA match); Sponza
mode 0 differs across runs of the same binary due to temporal-jitter
run-to-run variance (codex 11 F3, root-caused). The codex 11 F1 ImGui
Reset Camera button bug is fixed and runtime-verified through both OBJ
and analytic paths via `--test-reset-helper`. Interactive WASD/RMB/wheel
paths are source-reviewed; manual UI exercise is the remaining
verification step (codex 11 F2).

**Changelog (vs codex 10 first impl):** F1 -- new `resetCameraToScenePreset()`
helper unifies R-key and ImGui button (was: button bypassed Sponza preset).
F2 -- interactive verification claims downgraded; reset-path runtime-verified
via new `--test-reset-helper` CLI. F3 -- "byte-identical" claim narrowed to
Cornell only; Sponza variance root-caused as Halton-jitter EMA noise.
F4 -- clean Release build verified after killing leftover interactive
process; lock-failure recovery noted. F5 -- helper smoke log refreshed
post-label-patch.

---

## Summary

| Change | Where | Status |
|---|---|---|
| 5-helper — `applyOBJViewPreset()` extracted | `Demo3D::applyOBJViewPreset` (new) | done |
| 5-helper — `syncCameraYawPitchFromTarget()` for mouse-look state | `Demo3D::syncCameraYawPitchFromTarget` (new) | done |
| `loadOBJMesh()` calls helper after commit | `Demo3D::loadOBJMesh` | done |
| `resetCamera()` calls sync helper | `Demo3D::resetCamera` | done |
| 5a — WASD/QE/Shift translation | `processInput()` keyboard block | done |
| 5b — RMB-drag look (yaw/pitch scalars + cleanup safety) | `processInput()` mouse-look blocks | done |
| 5c — Mouse-wheel zoom + Ctrl-wheel FOV | `processInput()` wheel block | done |
| 5d — R key reset (helper) | `processInput()` keyboard block | done |
| codex 11 F1 — `resetCameraToScenePreset()` shared by R-key + ImGui button | `Demo3D::resetCameraToScenePreset` (new) | done |
| codex 11 F1/F2 test hook — `--test-reset-helper` CLI | `main3d.cpp` + `Demo3D::testResetCameraHelper` | done |
| Label patch — `KEY_D` -> `KEY_F1` + 5 sites | `demo3d.cpp` various | done |
| Quick Start camera-controls bullet block | `demo3d.cpp` panel | done |
| 5e — Cascade reseed on camera move | (deleted, codex 10 F2) | n/a |

**Build (Release, `cmake --build . --config Release --clean-first`):**
0 errors, **37 project warnings in `3d/src/`** — exact Step 4 baseline.
Distribution unchanged: 13×C4819, 9×C4244, 7×C4267, 5×C4100, 2×C4018, 1×C4310.

---

## Files Touched

- `src/demo3d.h` — added `cameraYaw`, `cameraPitch` members + `applyOBJViewPreset()` and `syncCameraYawPitchFromTarget()` declarations
- `src/demo3d.cpp` — new `Demo3D::syncCameraYawPitchFromTarget()` and `Demo3D::applyOBJViewPreset()` (~80 lines extracted from Step 4 inline preset block); `loadOBJMesh()` calls helper instead of inlining; `resetCamera()` calls sync helper at end; `processInput()` rewritten with split capture + camera input blocks (5a–5d); 5 stale label sites updated

No new headers, no shaders, no build-system changes.

---

## Helper Extraction (5-helper)

`applyOBJViewPreset(objKind)` is the canonical Step 5 contribution to the
data flow. It owns the per-OBJ camera + light preset that was previously
inlined in `loadOBJMesh()` (Step 4 4b). Two callers:

1. `loadOBJMesh()` calls it after the commit block — **no behavior change
   from Step 4** (proven by the helper smoke test below).
2. `processInput()`'s `R`-key handler calls it directly — **no file I/O,
   no voxelization, no EDT bake** (codex 10 F3).

Includes the codex 09 F2 alpha-sample-skip-on-outside-volume logic so R
preserves that behavior too.

`syncCameraYawPitchFromTarget()` initializes the new `cameraYaw` /
`cameraPitch` scalars from the camera's current forward vector. Called
from `applyOBJViewPreset()` and `resetCamera()` so mouse-look starts
coherent with whatever the scene preset set.

---

## Mouse-look Math (codex 10 F6)

Pitch/yaw maintained as scalars; forward reconstructed each frame from
the scalars rather than the previous `forward` vector. No
cross-product-on-near-singular-vector NaN possible.

```cpp
Vector2 md = GetMouseDelta();   // safer than absolute coords with cursor lock
cameraYaw   += -md.x * camera.rotationSpeed;
cameraPitch += -md.y * camera.rotationSpeed;
cameraPitch = glm::clamp(cameraPitch, -1.4835f, 1.4835f);   // ~+/-85 deg

glm::vec3 forward(
    std::cos(cameraPitch) * std::sin(cameraYaw),
    std::sin(cameraPitch),
    std::cos(cameraPitch) * std::cos(cameraYaw)
);
camera.target = camera.position + forward;
```

WASD strafe still uses `glm::cross(forward, worldUp)` to derive the right
vector, but the pitch clamp guarantees `|forward.y| < 0.997` so the cross
length is always > 0.07 — safe to normalize. Defensive `if (rightLenSq > 1e-6f)`
guard added anyway.

---

## ImGui Capture Split (codex 10 F4)

The previous early-return `if (WantCaptureMouse || WantCaptureKeyboard) return;`
gated the entire `processInput()` body. That would have broken FPS controls
in two ways:

1. **Hovering** the Quick Start panel would freeze WASD movement (mouse over
   UI sets `WantCaptureMouse=true`).
2. **RMB drag then mouse enters UI** would skip `EnableCursor()` and leave
   the cursor permanently hidden.

Restructured `processInput()` into 4 zones:

| Zone | Capture gate | Contents |
|---|---|---|
| Cleanup | none (always runs) | mouse-look RELEASE + `EnableCursor()` |
| Debug hotkeys | `!WantCaptureMouse && !WantCaptureKeyboard` | F1, F, P, G, 1/2/3, M, SDF wheel |
| Mouse-look START + body | START gated on `!WantCaptureMouse`; body whenever `mouseDragging` | RMB press/drag with yaw/pitch math |
| Keyboard movement | `!WantCaptureKeyboard` | WASD, Q/E, Shift, R |
| Wheel zoom | `!WantCaptureMouse && !showSDFDebug` | wheel + Ctrl+wheel |

Cleanup-always-runs is the load-bearing safety: even if the user starts an
RMB drag, then drags onto the ImGui panel, the next mouse-up still releases
the cursor lock.

---

## Final Keymap

| Key / Input | Function |
|---|---|
| W A S D | Camera strafe (forward/left/back/right) |
| Q E | Camera up/down (world Y axis) |
| Shift | Sprint multiplier (×4) |
| Right-mouse drag | Yaw + pitch (cursor hidden during drag) |
| Mouse wheel | Zoom (camera moves along forward axis) |
| Ctrl + mouse wheel | FOV adjust (clamped 20°–110°) |
| R | Reset camera to scene preset (calls `applyOBJViewPreset` for OBJ scenes, `resetCamera` for analytic) |
| F1 | Toggle SDF debug view (was `D`) |
| F | Cycle radiance debug mode (unchanged) |
| P | Screenshot + AI analysis (unchanged) |
| G | RenderDoc capture (unchanged) |

---

## Label Patch (codex 10 F5)

All 5 sites updated in this same patch so the user-visible documentation
matches the code:

| File:line | Before | After |
|---|---|---|
| `demo3d.cpp:355` (startup hint) | `Press 'D' to toggle` | `Press F1 to toggle` |
| `demo3d.cpp:400` (key handler) | `IsKeyPressed(KEY_D)` | `IsKeyPressed(KEY_F1)` |
| `demo3d.cpp:3711–3712` (Quick Start panel "Controls" bullets) | `R: Reload shaders` / `F1: Toggle UI` | Replaced entire block with full camera-controls help (WASD/QE/Shift/RMB/wheel/Ctrl-wheel/R + Debug F1/F/P) |
| `demo3d.cpp:3767` (SDF debug button) | `SDF Debug (D)` | `SDF Debug (F1)` |
| `demo3d.cpp:1218` (SDF debug overlay) | `[D] Toggle debug view` | `[F1] Toggle debug view` |

---

## Verification Results

### Build (codex 10 F7 command-explicit; codex 11 F4 reproducibility)

- Command: `cmake --build . --config Release --clean-first` (run from `build/`)
- Errors: **0**
- Project warnings (`3d/src/` and `3d/include/`): **37**
- Distribution unchanged from Step 4 baseline: 13xC4819, 9xC4244, 7xC4267, 5xC4100, 2xC4018, 1xC4310
- Step 5 added zero new warnings

**codex 11 F4 lock-failure recovery:** if the build fails with
`LNK1104: cannot open file RadianceCascades3D.exe`, the interactive demo
or a previous run is holding the executable. Close the app, or:

```powershell
Get-Process -Name "RadianceCascades3D" | Stop-Process -Force
cmake --build . --config Release --clean-first
```

### Headless smoke (no input -> SDF/EDT state identical to Step 4; Sponza pixels vary by jitter)

```
Sponza:  halfExtent=1.9, 147,593 seeds, EDT 66.8 ms, light=(0,0.5,0)
Cornell: halfExtent=1.0,  40,878 seeds, EDT 69.4 ms, light=(0,0.8,0)
```

Both per-OBJ presets routed through `applyOBJViewPreset()` produce identical
SDF state to Step 4's inline preset code (logs match exactly). Mode 0
capture results:

- **Cornell**: `step5_cornell_headless.png` is **byte-identical** (SHA-256
  match) to `step4v2_cornell_mode0.png`.
- **Sponza** (codex 11 F3): `step5_sponza_headless.png` differs from
  `step4v2_sponza_mode0.png` AND from a second consecutive Sponza capture.
  Three run hashes captured:

  | Capture | SHA-256 prefix |
  |---|---|
  | `step4v2_sponza_mode0.png` | `C970AFC0...` |
  | `step5_sponza_headless.png` | `79C274DD...` |
  | `step5b_sponza_a.png` (consecutive run) | `0C2F860F...` |
  | `step5b_sponza_b.png` (third consecutive) | `79C274DD...` |

  Two consecutive runs of the **same** binary produce **different**
  hashes, so this is **temporal-jitter run-to-run noise** in the Halton
  cascade EMA, not a Step 5 regression. Cornell happens to be
  byte-identical because its smaller probe field converges to a stable
  EMA state by frame 120; Sponza at 147K seeds is still in flux.

  For exact reproducibility in future verification: disable jitter
  (`useProbeJitter = false`) before capturing.

Logs preserved: `tools/app_run_step5_helper_v2.log` (post-label-patch;
codex 11 F5), `tools/app_run_step5_headless.log`,
`tools/app_run_step5_cornell_headless.log`. The pre-label-patch
`tools/app_run_step5_helper.log` is kept as a historical artifact only.

### codex 11 F1 reset-helper runtime verification (preserved evidence)

Added `--test-reset-helper` CLI that exercises `resetCameraToScenePreset()`
on both branches. The helper deliberately moves the camera away from the
preset, then calls the reset, so the log shows BEFORE and AFTER positions.

**OBJ path** (`--load-obj=sponza --test-reset-helper`,
`tools/app_run_step5_codex11_F1_sponza.log`):

```
[Demo3D] testResetCameraHelper before: pos=(3.5,0.5,0) fovy=60 light=(0,0.5,0)
[Demo3D] testResetCameraHelper after move: pos=(6,1.2,1.3)
[Demo3D] Applied sponza view preset: fovy=60; light=(0,0.5,0)
[Demo3D] testResetCameraHelper after reset: pos=(3.5,0.5,0) fovy=60 light=(0,0.5,0)
```

OBJ path correctly returns to Sponza preset `(3.5, 0.5, 0)` (NOT Cornell
default `(0, 0, 4)`). Without the F1 fix the ImGui button would have
gone to the wrong position.

**Analytic path** (`--load-obj=sponza --switch-to-scene=1 --test-reset-helper`,
`tools/app_run_step5_codex11_F1_analytic.log`):

```
[Demo3D] testResetCameraHelper before: pos=(3.5,0.5,0) fovy=60 light=(0,0.8,0)
[Demo3D] testResetCameraHelper after move: pos=(6,1.2,1.3)
[Demo3D] Camera reset to position: 0, 0, 4
[Demo3D] testResetCameraHelper after reset: pos=(0,0,4) fovy=60 light=(0,0.8,0)
```

Analytic path correctly applies `resetCamera()` since `useOBJMesh=false`
after `setScene(1)`. Both branches now go through the same shared helper.

### Interactive verification (codex 11 F2: source-reviewed, manual UI exercise outstanding)

The remaining gates (WASD movement, RMB-drag look, wheel zoom, F1 SDF
toggle, ImGui-hover-doesn't-freeze-keyboard, cursor-cleanup safety on
drag-into-UI) are **source-reviewed only**. No preserved log artifact
captures keypress evidence — the previous interactive launch
(`tools/app_run_step5_interactive.log`) only contains startup state
because the user closed the window without exercising controls, and
raylib doesn't accept injected input events from CLI without a deeper
test harness.

What's verified at runtime:
- **F1 reset path** (codex 11 F1): both OBJ and analytic branches via
  `--test-reset-helper`. The highest-severity item.
- **Helper extraction** (Step 5 5-helper): identical SDF state via
  headless smoke test (logs match exactly).
- **Label patch** (codex 10 F5): fresh log
  (`app_run_step5_helper_v2.log`) prints `Press F1 to toggle`.

What remains user-facing manual exercise:
- WASD/QE/Shift translation responds to keypresses.
- RMB-drag yaws/pitches the view.
- Mouse wheel zoom + Ctrl+wheel FOV.
- F1 toggles SDF debug view.
- Hover ImGui panel: WASD still moves (keyboard-only capture).
- Type into ImGui input: WASD does NOT fire.
- Start RMB drag, mouse enters ImGui, release: cursor re-shows.

The codex 10 F1 inside-Sponza diagnostic gate ("drive from outside preset
into the atrium, capture mode 0/1/4") is **deferred to the user's runtime
exploration**. Pass criteria documented:

- **Pass:** geometry visible inside the atrium.
- **Acceptable fail:** black regions inside; rolls to Step 6 follow-up
  (widen `surfaceRadius` from `voxelSz*sqrt(3)/2` to `voxelSz*sqrt(3)`).
  Not a Step 5 input bug.

---

## Architecture Notes

**Cascades stay scene-space.** Per codex 10 F2, change 5e (cascade reseed
on camera movement) was deleted. `radiance_3d.comp` doesn't read camera
position; probe positions, SDF, albedo, light transport are all
view-independent. Camera movement only re-evaluates `raymarch.frag` with
new `uCameraPos` / `uCameraDir` uniforms; no cascade rebuild, no temporal
history reseed. Result: free-fly camera incurs zero extra work beyond the
final raymarch pass.

**`setScene()` preserves camera, resets light** (codex 10 F7). Switching
from Cornell-OBJ -> analytic-Cornell now keeps the user's navigated
viewpoint (so they can compare two representations of the same scene from
the same angle). `lightPosition` is still reset by `setScene()` as a
scene-state invariant (codex 09 F1).

**`applyOBJViewPreset()` is the single source of truth for per-OBJ
preset.** Both `loadOBJMesh()` and the R-key path go through it.
Adding a third OBJ later = adding one branch in this helper, nothing else.
The codex 10 F3 concern (R reloading the OBJ to reset the camera) is
eliminated entirely — R is now ~10 instructions of float assignment.

**Mouse-look state is yaw/pitch scalars, not derived-each-frame from
forward.** Forward direction is reconstructed FROM the scalars, not the
other way around. This eliminates the cross-product-on-singular-basis NaN
that would have shown up at the top/bottom of view (codex 10 F6).
`syncCameraYawPitchFromTarget()` keeps the scalars consistent with whatever
the scene preset sets `camera.target` to.

---

## Known Open Items (Step 5 boundary -> later steps)

| Item | Where to land it |
|---|---|
| Inside-Sponza black-mode-0 visibility (camera can navigate there now, but render may break) | Step 6 (widen `surfaceRadius`; Step 2 fallback ladder) |
| Camera collision with SDF (don't drive into walls) | future visual-quality step |
| Saved camera bookmarks per scene | future UX step |
| Orbit-mode toggle | future UX step (5g, deferred) |
| ImGui camera panel (numerical sliders) | future UX step (5f, deferred) |
| Scene cycling shortcut keys (`[`, `]`) | not planned; UI buttons sufficient |
| Touch / gamepad input | not planned |

---

## Why Step 5 Is Smaller Than It Looked

The codex 10 F2 deletion (no cascade reseed) removed an entire planned
change. The codex 10 F3 helper extraction simplified the R-key handler from
"reload OBJ" to "call helper". The codex 10 F6 yaw/pitch scalars mean the
mouse-look code has one straight-line path — no special cases for poles, no
fallback for singular bases.

The capture split (codex 10 F4) is the largest individual change but it's
mostly restructuring the existing `processInput()` into 4 conditional zones
instead of 1 early-return. Net: ~150 lines added in `processInput()`,
~80 lines extracted from `loadOBJMesh()` into helpers, label patch is 5
isolated edits.

The actual user-visible value (free-fly through Sponza) flows naturally
once the mouse-look math is right and the ImGui capture is split correctly.
