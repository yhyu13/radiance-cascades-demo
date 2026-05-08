# Sponza SDF — Step 5: Interactive Camera + Scene Control (v2)

**Date:** 2026-05-07 (revised 2026-05-08 after codex review `10_*` / reply `10_*`)
**Plan ref:** `doc/4/claude_plan/sponza_sdf_step4_impl.md` (Step 4 follow-ups)
**Status:** Draft v2 — pending implementation
**Changelog (v1 -> v2):** Codex 10 findings absorbed.
- F1: inside-Sponza visibility is now a verification gate, not an assumed result.
- F2: change 5e (temporal reseed on camera movement) DELETED — cascades are
  scene-space; camera movement doesn't invalidate them.
- F3: R-key no longer reloads OBJ; new `applyOBJViewPreset()` helper extracted.
- F4: ImGui capture split — keyboard movement gated on `WantCaptureKeyboard`,
  mouse-look START gated on `WantCaptureMouse`, mouse-look END/cleanup ALWAYS
  runs (cursor can't get stuck).
- F5: final keymap chosen; 5 stale label sites updated in same patch.
- F6: mouse-look uses maintained yaw/pitch scalars + `GetMouseDelta()` (no
  basis singularity).
- F7: analytic-scene camera unchanged on `setScene()`; build warning gate
  is command-explicit.

---

## Goal

Make the camera **moveable at runtime** so the user can inspect Sponza from
any angle without recompiling. Scene switching already works via ImGui
buttons.

**Important caveat (codex 10 F1):** moving the camera doesn't fix the Step 4
inside-atrium black-mode-0 problem. The conservative-band UDF was thin
enough at grazing angles that the inside-Sponza first preset failed even
with `alpha=0`. If the user drives from the outside preset into the atrium
and sees black regions, that's the same Step 2/4 follow-up (widen
`surfaceRadius`), not a Step 5 input bug. Step 5's success is "user can
move the camera"; whether the inside view RENDERS is a separate gate.

---

## Confirmed Facts From Current Source

- `demo3d.h:146-167` `Camera3DConfig` exists with all fields plus
  `moveSpeed = 5.0`, `rotationSpeed = 0.003` (set in `resetCamera()`).
- `demo3d.cpp:389-464` `processInput()` handles debug keys; ends with comment
  `// Basic camera controls would go here`.
- `demo3d.cpp:167, 242` `mouseDragging` and `lastMousePos` member vars are
  declared and initialized but **never read** elsewhere — earmarked but
  never wired.
- `demo3d.cpp:395-397` early-exit `if (WantCaptureMouse || WantCaptureKeyboard)`
  is currently too coarse (codex F4); will be split in Step 5.
- `setScene()` and `loadOBJMesh()` both reset camera per-scene today (4b).
  Step 5 changes setScene to **preserve** camera but reset light (codex F7).
- `radiance_3d.comp` does not read camera position; cascades are scene-space.
- `cascadeReady` is a local static inside `render()` (not visible to
  `processInput()`) — confirms why setting `historyNeedsSeed` from input
  would have been a no-op (codex F2).
- Quick Start label sites (codex F5):
  - `demo3d.cpp:355` startup hint `Press 'D' to toggle`
  - `demo3d.cpp:400` `IsKeyPressed(KEY_D)` for SDF debug
  - `demo3d.cpp:3711` panel bullet `R: Reload shaders` (not wired)
  - `demo3d.cpp:3712` panel bullet `F1: Toggle UI` (not wired)
  - `demo3d.cpp:3767` button label `SDF Debug (D)`
  - `demo3d.cpp:1218` overlay label

---

## Architecture Notes (read first)

**Cascades are scene-space, not view-space.** Camera movement only
re-evaluates `raymarch.frag` from a new viewpoint using the same probe
field, same SDF, same albedo. **No cascade invalidation, no temporal
history reseed.** Future changes that DO require it (light moves, scene
geometry changes, SDF parameters) must route through `setScene()`-style
invalidation paths that mark `sceneDirty=true` and reseed temporal state.

This is why **change 5e (camera-movement reseed) was deleted in v2**:
- It would have been a no-op as written (`cascadeReady` is local-static in
  `render()`, not reachable from `processInput()`).
- If "fixed" by exposing `cascadeReady`, it would have caused per-frame
  cascade rebuilds during movement — ~30 ms wasted on output that didn't
  change.

**Camera state is user-state, light state is scene-state.** Decision (codex
F7): `setScene()` preserves camera position (so switching from Cornell-OBJ
to analytic-Cornell keeps the user's viewpoint for comparison) but resets
light to `(0, 0.8, 0)` (already done by codex 09 F1). `R` key always
explicit-resets to scene preset.

---

## Final Keymap (codex F5)

All listed sites updated in the **same Step 5 patch**:

| Key / Input | Function | Was |
|---|---|---|
| W A S D | Camera strafe (forward/left/back/right) | unbound |
| Q E | Camera up/down (world axis) | unbound |
| Shift | Sprint multiplier (4x) | unbound |
| Right-mouse drag | Camera look (yaw + pitch, cursor hidden) | unbound |
| Mouse wheel | Zoom (move along forward axis) | SDF slice (when SDF debug active) |
| Ctrl + mouse wheel | FOV adjust (clamped 20-110 degrees) | unbound |
| R | Reset camera to scene preset | label "reload shaders" (was never wired) |
| F1 | SDF debug toggle | label "toggle UI" (was never wired) |
| F | Radiance debug cycle | unchanged |
| P, G | Screenshot, RenderDoc | unchanged |

Label patch list:

| File:line | Old | New |
|---|---|---|
| `demo3d.cpp:355` | `Press 'D' to toggle` | `Press F1 to toggle` |
| `demo3d.cpp:400-403` | `IsKeyPressed(KEY_D)` | `IsKeyPressed(KEY_F1)` |
| `demo3d.cpp:3711` | `R: Reload shaders` | `R: Reset camera to scene preset` |
| `demo3d.cpp:3712` | `F1: Toggle UI` | `F1: Toggle SDF debug` |
| `demo3d.cpp:3767` | `SDF Debug (D)` button | `SDF Debug (F1)` |
| `demo3d.cpp:1218` | SDF debug overlay | match `(F1)` |

Quick Start panel gains new bullet items (camera controls block):

```
Camera (when not in UI):
  WASD          strafe
  Q / E         up / down
  Shift         sprint (x4)
  RMB drag      look around
  Wheel         zoom
  Ctrl+Wheel    FOV
  R             reset to scene preset
```

---

## Five Changes (in dependency order)

### Change 5-helper — Extract `applyOBJViewPreset()` (codex F3)

Refactor the Step 4 4b camera+light preset block into a standalone helper
**before** doing the input changes. This unblocks both `loadOBJMesh()` and
the new `R` key handler.

```cpp
// demo3d.h private section:
void applyOBJViewPreset(const std::string& objKind);
void syncCameraYawPitchFromTarget();   // codex F6

// demo3d.cpp, new function:
void Demo3D::applyOBJViewPreset(const std::string& objKind) {
    glm::vec3 camPos, camTarget, lightPos(0.0f, 0.8f, 0.0f);
    float fovy = 60.0f;
    bool found = false;

    if (objKind == "sponza") {
        camPos    = glm::vec3(3.5f, 0.5f, 0.0f);
        camTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        fovy      = 60.0f;
        lightPos  = glm::vec3(0.0f, 0.5f, 0.0f);
        found = true;
    } else if (objKind == "cornell") {
        camPos    = glm::vec3(0.0f, 0.0f, 4.0f);
        camTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        fovy      = 60.0f;
        lightPos  = glm::vec3(0.0f, 0.8f, 0.0f);
        found = true;
    }
    if (!found) return;

    camera.position = camPos;
    camera.target   = camTarget;
    camera.up       = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fovy     = fovy;
    syncCameraYawPitchFromTarget();   // codex F6: keep scalars consistent
    lightPosition   = lightPos;
    std::cout << "[Demo3D] Applied " << objKind << " view preset (camera + light)\n";
}
```

`loadOBJMesh()` after the existing commit block calls
`applyOBJViewPreset(objKind)` instead of the inline preset code.

### Change 5a — WASD/QE translation + Shift sprint (codex F4 capture split)

```cpp
// In processInput(), within !io.WantCaptureKeyboard block:
glm::vec3 forward = glm::normalize(camera.target - camera.position);
glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0,1,0)));
glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

float dt    = GetFrameTime();
float speed = camera.moveSpeed * dt;
if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) speed *= 4.0f;

glm::vec3 delta(0.0f);
if (IsKeyDown(KEY_W)) delta += forward;
if (IsKeyDown(KEY_S)) delta -= forward;
if (IsKeyDown(KEY_A)) delta -= right;
if (IsKeyDown(KEY_D)) delta += right;
if (IsKeyDown(KEY_E)) delta += worldUp;
if (IsKeyDown(KEY_Q)) delta -= worldUp;

if (glm::length(delta) > 1e-6f) {
    delta = glm::normalize(delta) * speed;
    camera.position += delta;
    camera.target   += delta;
}
```

The cross product is safe here because `forward` comes from
`target - position` and `pitch` is clamped (see 5b) so `|forward.y| < 0.997`,
keeping `|cross(forward, world-up)| > 0.07`.

### Change 5b — Right-mouse-drag look (codex F4 split + F6 yaw/pitch)

```cpp
// New members in demo3d.h:
float cameraYaw   = 0.0f;   // radians, 0 = +Z forward
float cameraPitch = 0.0f;   // radians, clamped to ~+/-85 degrees

// Mouse-look RELEASE/CLEANUP — runs UNCONDITIONALLY (codex F4):
// even if mouse enters ImGui mid-drag, the next release still re-shows cursor.
if (mouseDragging && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
    mouseDragging = false;
    EnableCursor();
}

// Mouse-look START — only when ImGui doesn't want the mouse:
if (!io.WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
    mouseDragging = true;
    DisableCursor();
}

// Mouse-look BODY — runs whenever dragging:
if (mouseDragging) {
    Vector2 md = GetMouseDelta();    // raylib's Vector2; safer than absolute coords with cursor lock
    cameraYaw   += -md.x * camera.rotationSpeed;
    cameraPitch += -md.y * camera.rotationSpeed;
    cameraPitch = glm::clamp(cameraPitch, -1.4835f, 1.4835f);   // ~+/-85 degrees

    // Reconstruct forward from scalars; no cross-product, no singularity.
    glm::vec3 forward(
        std::cos(cameraPitch) * std::sin(cameraYaw),
        std::sin(cameraPitch),
        std::cos(cameraPitch) * std::cos(cameraYaw)
    );
    camera.target = camera.position + forward;
}

// syncCameraYawPitchFromTarget() helper called by applyOBJViewPreset(),
// resetCamera(), and Demo3D ctor so the scalars match the initial forward:
void Demo3D::syncCameraYawPitchFromTarget() {
    glm::vec3 fwd = glm::normalize(camera.target - camera.position);
    cameraYaw   = std::atan2(fwd.x, fwd.z);
    cameraPitch = std::asin(glm::clamp(fwd.y, -1.0f, 1.0f));
}
```

### Change 5c — Mouse-wheel zoom + Ctrl-wheel FOV

```cpp
// Inside !io.WantCaptureKeyboard && !io.WantCaptureMouse:
float wheel = GetMouseWheelMove();
if (wheel != 0.0f && !showSDFDebug) {   // SDF debug already owns wheel for slice
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        camera.fovy = glm::clamp(camera.fovy - wheel * 2.0f, 20.0f, 110.0f);
    } else {
        glm::vec3 forward = glm::normalize(camera.target - camera.position);
        float zoomStep = wheel * camera.moveSpeed * 0.1f;
        camera.position += forward * zoomStep;
        camera.target   += forward * zoomStep;
    }
}
```

### Change 5d — R key resets to scene preset (codex F3 helper)

```cpp
// Inside !io.WantCaptureKeyboard:
if (IsKeyPressed(KEY_R)) {
    if (useOBJMesh && !currentOBJPath.empty()) {
        applyOBJViewPreset(currentOBJPath);
    } else {
        resetCamera();
        syncCameraYawPitchFromTarget();
    }
    std::cout << "[Demo3D] Camera reset to scene preset (R key)\n";
}
```

No file I/O. No voxelization. No EDT bake. Just a few float assignments
plus the yaw/pitch sync.

### (5e DELETED, codex F2)

Was: "set `historyNeedsSeed` when camera moves." Removed because:
- Cascades are scene-space; camera movement doesn't change them.
- Setting `historyNeedsSeed` from `processInput()` was a no-op anyway
  (`cascadeReady` is local-static in `render()`).
- "Fixing" it by forcing rebuilds would have been ~30 ms/frame of waste.

---

## Files Touched

| File | Function / area | Change |
|---|---|---|
| `src/demo3d.h` | private member section | Add `cameraYaw`, `cameraPitch` members; `applyOBJViewPreset` and `syncCameraYawPitchFromTarget` declarations |
| `src/demo3d.cpp` | new function `applyOBJViewPreset()` | Extracted from Step 4 4b inline preset (codex F3) |
| `src/demo3d.cpp` | new function `syncCameraYawPitchFromTarget()` | Initializes yaw/pitch from forward vector (codex F6) |
| `src/demo3d.cpp` | `loadOBJMesh()` | Replace inline preset block with `applyOBJViewPreset(objKind)` call |
| `src/demo3d.cpp` | `resetCamera()` | Add `syncCameraYawPitchFromTarget()` call at end |
| `src/demo3d.cpp` | `processInput()` | Add 5a/5b/5c/5d blocks with split capture (codex F4); rebind KEY_D -> KEY_F1 (codex F5) |
| `src/demo3d.cpp` | startup message line ~355 | "Press 'D'" -> "Press F1" |
| `src/demo3d.cpp` | Quick Start panel ~3711-3712 | Update R + F1 labels |
| `src/demo3d.cpp` | SDF debug button ~3767 | "(D)" -> "(F1)" |
| `src/demo3d.cpp` | SDF debug overlay ~1218 | Same label update |
| `src/demo3d.cpp` | Quick Start panel | Add camera control bullet block |

No new headers, no shaders, no build changes.

---

## Verification Checklist

### Build (codex F7)

- [ ] Clean Release rebuild via `cmake --build . --config Release --clean-first`:
      0 errors, **37 project warnings in `3d/src/`** (Step 4 baseline preserved)

### Sponza interaction
- [ ] Load Sponza OBJ -> camera at preset `(3.5, 0.5, 0)`
- [ ] WASD strafe (camera + target move together)
- [ ] Q/E ascend/descend
- [ ] Shift+W goes 4x faster
- [ ] Right-mouse-drag yaw/pitch; cursor hidden during drag
- [ ] Pitch clamped to ~+/-85 degrees (no flip-over)
- [ ] Mouse wheel zoom works
- [ ] Ctrl+wheel changes FOV (visible widening/narrowing)
- [ ] R snaps back to Sponza preset

### Cornell interaction (regression)
- [ ] Load Cornell OBJ -> camera at preset `(0, 0, 4)`
- [ ] All Step 5 inputs work identically
- [ ] R resets to Cornell preset (not Sponza)

### Analytic scene interaction (codex F7 decision)
- [ ] Click "Cornell Box" analytic AFTER navigating in Cornell OBJ:
      camera position **preserved** (NOT reset to default)
- [ ] `lightPosition` **reset** to `(0, 0.8, 0)` (codex 09 F1, unchanged)
- [ ] R key in analytic scene calls `resetCamera()` -> default `(0, 0, 4)`

### UI safety (codex F4)
- [ ] Hover ImGui panel: WASD STILL MOVES camera (only `WantCaptureKeyboard`
      blocks keyboard, not mouse hover)
- [ ] Type into an ImGui text input: WASD does NOT fire
- [ ] Drag an ImGui slider: camera does NOT yaw/pitch (mouse-look START gated)
- [ ] Start RMB drag, move mouse over ImGui panel, release: cursor RE-SHOWS
      (cleanup runs unconditionally)
- [ ] SDF debug active (F1): mouse-wheel goes to slice, camera does not zoom

### F1/F2/F6 specific
- [ ] **No cascade rebuild during continuous movement** (codex F2): FPS stays
      stable, no temporal flicker, no log spam from cascade dispatch
- [ ] **Yaw/pitch never produce NaN** (codex F6): pan camera straight up,
      then yaw — no flicker / black frame / log of bad math
- [ ] **Inside-Sponza visibility (diagnostic, not pass/fail)**: drive from
      outside preset to inside atrium, capture mode 0/1/4. Pass = geometry
      visible. Fail (acceptable) = black regions; logged as Step 6
      prerequisite (widen surfaceRadius per Step 2 fallback ladder).

### Headless verification (CLI)
- [ ] `--load-obj=sponza --exit-frames=120 --screenshot=...` still works
      (no input -> no movement -> identical to Step 4 captures)

---

## Risks (revised)

| Risk | Notes |
|---|---|
| Inside-Sponza camera still produces black mode 0 (codex F1) | Acceptable; rolls to Step 6 (widen surfaceRadius). Not a Step 5 wiring failure. |
| KEY_D collision with strafe | Resolved by F1 rebind; all 5 label sites updated in same patch. |
| Cursor stuck hidden if mouse enters ImGui mid-drag (codex F4) | Resolved by always-running cleanup path. |
| Mouse-look NaN at top/bottom (codex F6) | Resolved by yaw/pitch scalars + clamp. Cross product only used in WASD where pitch clamp guarantees safe basis. |
| Analytic scene camera unexpectedly resets (codex F7) | Decided: camera preserved; only light resets. Tradeoff: user navigating from OBJ Cornell to analytic Cornell now sees the same view of two representations -- desirable for comparison. |
| `applyOBJViewPreset` for unknown name silently does nothing | Returns early without setting camera. Existing callers always pass a known name. If extended in future, add a [WARN]. |
| WASD is now keyboard-blocked only by `WantCaptureKeyboard`, not mouse-hover | Intentional (codex F4). User can hover Quick Start panel and still walk through Sponza with the keyboard. Only typing in input boxes blocks. |

---

## What's Out of Scope for Step 5

- SDF camera-collision pushback (don't drive into walls).
- Saved camera bookmarks (per-scene "remember last view").
- Smooth camera animation / cinematic paths.
- Orbit mode toggle.
- ImGui camera panel (numerical position/target/FOV sliders).
- Scene cycling shortcut keys.
- Touch / gamepad input.
- Widening `surfaceRadius` to fix inside-Sponza visibility (Step 6
  candidate; codex F1 confirmed this stays separate).

---

## Implementation Order

1. **5-helper:** extract `applyOBJViewPreset()` + add
   `syncCameraYawPitchFromTarget()`. Update `loadOBJMesh` to call helper.
   Smoke-test via existing `--load-obj=sponza` headless run -- camera + light
   should be identical to Step 4 baseline (just routed through the helper now).
2. **Label patch (codex F5):** rebind KEY_D -> KEY_F1 in `processInput`,
   update all 5 label sites. Smoke-test: F1 toggles SDF debug, D does
   nothing (yet — strafe wired in step 5a next).
3. **5a:** WASD/QE/Shift translation. Test in Cornell first.
4. **5b:** RMB-drag look with yaw/pitch scalars. Test pitch clamp at extremes.
5. **5c:** wheel zoom + Ctrl-wheel FOV. Verify SDF-debug-mode wheel still
   wins.
6. **5d:** R key calls `applyOBJViewPreset` (OBJ) or `resetCamera` (analytic).
7. Manual inside-Sponza walk-through: capture mode 0/1/4; document outcome
   (codex F1 gate).
8. Update `sponza_sdf_step5_impl.md` with measured FPS during movement, the
   inside-Sponza capture result, and any keymap collision discoveries.

---

## Why Now (Sequencing Rationale)

Step 4 made Sponza visible from a fixed external camera. The user's natural
next question — and the one they asked — is "can I move?" Camera control
unblocks every visual investigation that follows: inspecting Sponza
interior columns, debugging GI bleed, walking through the atrium to find
the spots where the SDF band breaks down, etc.

Codex F1 reframes this honestly: Step 5 unblocks **investigation**, not
**solution**. The inside-atrium black-mode-0 problem may persist — but at
least the user can navigate to where it manifests and capture diagnostic
modes without recompiling. Step 6 (widen surfaceRadius) would address the
underlying cause if the manual capture in step 7 above shows the failure.
