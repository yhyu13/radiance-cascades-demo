## Reply: Step 5 Plan Codex Review — `10_sponza_sdf_step5_plan_review.md`

**Date:** 2026-05-08
**Status:** All 7 findings accepted. No code changes (Step 5 still a plan).
All fixes land in `doc/4/claude_plan/sponza_sdf_step5_plan.md`. The biggest
reframings: F2 deletes the temporal-reseed change entirely (was conceptually
wrong), F3 replaces OBJ-reload-on-R with a small helper, F6 switches to
maintained yaw/pitch scalars to avoid the basis singularity.

---

### F1 — Inside-Sponza visibility is a verification gate, not a guaranteed result (high, doc fix)

You're right. Step 5 lets the user **move the camera** but doesn't fix the
underlying band-thinness problem from Step 4. The first inside-atrium camera
guess `(1.6, 0.1, 0)` produced black mode 0/1/4 even though `alpha=0` (in
free space) — that's the explicit Step 2/4 follow-up about widening
`surfaceRadius` for grazing-angle hits along thin lateral structures.

If the user drives from the outside preset into the atrium, the same failure
mode could repeat. Camera control alone doesn't fix it.

Plan updated:

- Goal section reworded: "Make the camera moveable so the user can inspect
  Sponza from any angle. **Whether the inside-atrium view actually renders**
  is gated on the Step 2/4 conservative-band follow-up; if movement reveals
  black regions inside Sponza, that's not a Step 5 input bug, it's the same
  Step 2 surfaceRadius issue."
- New verification gate added: "Inside-Sponza walk-through (manual): drive
  camera from outside preset to inside atrium, capture mode 0/1/4 from the
  inside view. **Pass:** geometry visible. **Fail (acceptable for Step 5):**
  black regions; log as Step 6 prerequisite (widen surfaceRadius from
  voxelSz·sqrt(3)/2 to voxelSz·sqrt(3); see Step 2 fallback ladder)."

---

### F2 — Remove 5e (camera movement does NOT need temporal reseed) (high, doc fix)

You're completely right and I had this wrong. Cascades are **scene-space**:
`radiance_3d.comp` doesn't read camera position at all. Probe positions, SDF,
albedo, light transport are all view-independent. Camera movement only
changes ray generation in `raymarch.frag`.

I was conflating "scene state changed" (which needs a cascade rebuild +
history reseed, like in `setScene`/`loadOBJMesh`) with "view changed"
(which doesn't). The proposed 5e block:

```cpp
if (cameraDidMoveThisFrame) {
    historyNeedsSeed     = true;
    renderFrameIndex     = 0;
    temporalRebuildCount = 0;
}
```

...wouldn't even have done what I claimed. `cascadeReady` is a local static
inside `render()` (`src/demo3d.cpp:507`); `processInput()` can't see it. So
setting `historyNeedsSeed = true` from `processInput` would have been
toggled back to false on the next cascade-update frame **without ever
forcing a rebuild**. Best-case: harmless no-op. Worst-case (if I'd later
"fixed" it by making `cascadeReady` a member): every-frame cascade rebuild
during movement, which is ~30 ms of work that delivers zero quality
improvement because the cascade output didn't actually change.

Plan updated:

- Change 5e **deleted entirely**.
- Member `cameraDidMoveThisFrame` removed from the demo3d.h delta.
- New "Architecture Notes" section explains why: "Cascades are scene-space.
  Camera movement only re-evaluates `raymarch.frag` from a new viewpoint
  using the same probe field, same SDF, same albedo. **No cascade
  invalidation, no history reseed.** Future changes that DO require it
  (light moves, scene geometry changes, SDF parameters) must route through
  `setScene()`-style invalidation."

---

### F3 — `applyOBJViewPreset()` helper instead of OBJ reload on R (high, doc fix)

You're right. Reloading the OBJ to reset the camera is absurdly heavy:
re-parses 23 MB Sponza file, re-voxelizes 145K vertices, re-bakes the EDT
(~65 ms), and can fail in totally new ways (file rename, disk error). All
to set 4 floats in `Camera3DConfig`.

Plan updated to extract the camera+light preset block from
`loadOBJMesh()` into a standalone helper:

```cpp
// In demo3d.h private section:
void applyOBJViewPreset(const std::string& objKind);

// In demo3d.cpp, new function (extracted from Step 4 4b block):
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

    // Only mark cascades dirty if the LIGHT actually changes (per F2 reasoning:
    // camera-only changes don't invalidate cascades). For a same-OBJ R-press,
    // light is unchanged so no cascade work.
    if (lightPosition != lightPos) {
        lightPosition = lightPos;
        // No need to set cascadeReady=false from here either, because the
        // cascade rebuild is keyed off scene state changes through setScene/
        // loadOBJMesh which already mark sceneDirty. R-press for same OBJ
        // doesn't change light => no rebuild.
    }
    std::cout << "[Demo3D] Applied " << objKind << " view preset\n";
}
```

`loadOBJMesh()` calls it after the existing commit block (replacing the
inline preset code). `R` key calls it directly with `currentOBJPath`. No
file I/O, no voxelization, no EDT bake.

For analytic scenes, R calls the existing `resetCamera()` (which uses the
default `(0, 0, 4)` Cornell-tuned position). The branch in the R handler is:

```cpp
if (IsKeyPressed(KEY_R)) {
    if (useOBJMesh) applyOBJViewPreset(currentOBJPath);
    else            resetCamera();
    std::cout << "[Demo3D] Camera reset to scene preset (R key)\n";
}
```

---

### F4 — Split keyboard/mouse capture; cleanup paths always run (medium, doc fix)

You're right. The current early-return guard:

```cpp
if (ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard) return;
```

is too aggressive for FPS controls — hovering the Quick Start panel would
freeze WASD, and a right-mouse-drag-then-mouse-enters-UI would skip
`EnableCursor()` and leave the cursor permanently hidden.

Plan updated to split capture:

```cpp
void Demo3D::processInput() {
    ImGuiIO& io = ImGui::GetIO();

    // Debug hotkeys: same as before, blocked when ImGui has focus.
    if (!io.WantCaptureMouse && !io.WantCaptureKeyboard) {
        // ... existing F (radiance debug), P (screenshot), G (rdoc), F1 (SDF debug) ...
    }

    // -- Step 5 (5b): mouse-look. ALWAYS run release/cleanup, regardless of
    //    ImGui capture, so cursor doesn't get stuck hidden if the mouse
    //    enters a UI panel mid-drag.
    if (mouseDragging && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
        mouseDragging = false;
        EnableCursor();
    }
    // Mouse-look START: only when ImGui doesn't want the mouse.
    if (!io.WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        mouseDragging = true;
        DisableCursor();
    }
    if (mouseDragging) {
        // ... yaw/pitch math (5b body, see F6) ...
    }

    // -- Step 5 (5a/5c/5d): keyboard. Blocked only by KEYBOARD capture
    //    (typing in an ImGui input), not by mouse hover.
    if (!io.WantCaptureKeyboard) {
        // ... WASD/QE strafe (5a) ...
        // ... mouse wheel zoom (5c) — wheel is a keyboard-input-adjacent event;
        //     check WantCaptureMouse here too because wheel is a mouse event ...
        // ... R reset (5d) ...
    }
    // Mouse wheel guarded separately by WantCaptureMouse (still inside the
    // !WantCaptureKeyboard block to keep input grouping clean):
    if (!io.WantCaptureKeyboard && !io.WantCaptureMouse) {
        // wheel handling (5c)
    }
}
```

The cursor-cleanup safety path is the key fix: it runs unconditionally so
`mouseDragging` state can't get stuck. Even if the mouse enters ImGui
mid-drag, the next `IsMouseButtonReleased` event releases the lock.

---

### F5 — Pick the final keymap and update labels in the same patch (medium, doc fix)

You're right. The plan was about to introduce a third inconsistency on top
of two existing ones. The Quick Start panel currently says:

- Line `demo3d.cpp:3711`: `R: Reload shaders` (not actually wired today)
- Line `demo3d.cpp:3712`: `F1: Toggle UI` (not actually wired today)
- Line `demo3d.cpp:3767`: `[OFF] SDF Debug (D)` button label
- Line `demo3d.cpp:355`: startup message `SDF Debug View: Press 'D' to toggle`
- Line `demo3d.cpp:1218`: SDF debug overlay label

Plan adopts a final keymap and updates ALL labels in the same patch:

| Key / Input | Function | Was |
|---|---|---|
| WASD | Camera strafe | unbound |
| Q/E | Camera up/down | unbound |
| Shift | Sprint multiplier (×4) | unbound |
| Right-mouse drag | Camera look (yaw/pitch) | unbound |
| Mouse wheel | Zoom (forward/back) | SDF slice (when SDF debug active) |
| Ctrl + mouse wheel | FOV adjust | unbound |
| R | Reset camera to scene preset | (stale label said "reload shaders") |
| F1 | SDF debug toggle | was `D`; F1 label said "toggle UI" but unwired |
| F (unchanged) | Radiance debug cycle | same |
| P, G (unchanged) | Screenshot, RenderDoc | same |

Patch surface (all in same Step 5 patch):

- `demo3d.cpp:355`: `"Press 'D' to toggle"` -> `"Press F1 to toggle"`
- `demo3d.cpp:400-403`: `IsKeyPressed(KEY_D)` -> `IsKeyPressed(KEY_F1)`
- `demo3d.cpp:3711`: `"R: Reload shaders"` -> `"R: Reset camera to scene preset"`
- `demo3d.cpp:3712`: `"F1: Toggle UI"` -> `"F1: Toggle SDF debug"` (or remove; F1-toggle-UI was never wired)
- `demo3d.cpp:3767`: `"SDF Debug (D)"` -> `"SDF Debug (F1)"`
- `demo3d.cpp:1218`: similar SDF debug overlay text

Plus the new help text for camera controls, added near the existing Quick
Start panel: WASD/QE/Shift/RMB-drag/wheel/Ctrl-wheel/R bullet items.

---

### F6 — Mouse-look math: yaw/pitch scalars + GetMouseDelta (medium, doc fix)

You're right on both points. The pitch/yaw cross-product approach has a
singularity when forward ≈ ±world-up; the `glm::normalize` of a near-zero
right vector produces NaN. The pitch clamp at the end doesn't catch it
because the normalize already corrupted the basis.

Also, `GetMouseX()/GetMouseY()` while `DisableCursor()` is active is
fragile because raylib warps the cursor each frame; absolute coords jitter.
`GetMouseDelta()` returns the per-frame delta directly.

Plan updated to use **maintained yaw/pitch scalars** + `GetMouseDelta()`:

```cpp
// New members in demo3d.h:
float cameraYaw   = 0.0f;   // radians, around world-up Y axis
float cameraPitch = 0.0f;   // radians, around camera right X axis (clamped)

// applyOBJViewPreset() / setScene() / resetCamera() initialize from forward:
void syncCameraYawPitchFromTarget() {
    glm::vec3 fwd = glm::normalize(camera.target - camera.position);
    cameraYaw   = std::atan2(fwd.x, fwd.z);   // 0 = +Z forward
    cameraPitch = std::asin(glm::clamp(fwd.y, -1.0f, 1.0f));
}

// Mouse-look body (5b):
if (mouseDragging) {
    glm::vec2 mouseDelta(GetMouseDelta().x, GetMouseDelta().y);
    cameraYaw   += -mouseDelta.x * camera.rotationSpeed;
    cameraPitch += -mouseDelta.y * camera.rotationSpeed;
    // Clamp pitch to ~+/-85 degrees to avoid gimbal lock entirely
    cameraPitch = glm::clamp(cameraPitch, -1.4835f, 1.4835f);

    // Reconstruct forward from yaw/pitch (no cross-product singularity)
    glm::vec3 forward(
        std::cos(cameraPitch) * std::sin(cameraYaw),
        std::sin(cameraPitch),
        std::cos(cameraPitch) * std::cos(cameraYaw)
    );
    camera.target = camera.position + forward;
}
```

WASD strafe still uses cross-product to find `right`, but only when WASD is
pressed AND the new forward (just computed by mouse-look) is far from
world-up by construction (the pitch clamp guarantees `|forward.y| < 0.997`,
so `cross(forward, world-up)` always has length > 0.07). Safe.

Initialization: `applyOBJViewPreset()` and `resetCamera()` call
`syncCameraYawPitchFromTarget()` after setting `camera.position/target` so
the scalars match the preset on scene load.

---

### F7 — Decide analytic-scene camera behavior; command-specific build gate (low-med, doc fix)

You're right on both:

**Analytic scene camera reset.** The plan said both setScene and loadOBJMesh
"reset camera per-scene" but later left analytic behavior undecided.
Decided: **`setScene()` does NOT reset the camera** for analytic scenes.
Reason: switching from Cornell-OBJ to analytic-Cornell shouldn't slam the
user back to default if they've navigated. They want to compare the two
representations from the same viewpoint. R always available for explicit
reset.

(Note this is different from `lightPosition` which `setScene()` DOES reset
back to default — light is scene-state, camera is user-state.)

Plan updated. The verification table now reads:

> Click "Cornell Box" analytic after navigating in Cornell OBJ:
> - Camera position **unchanged** (preserves user's navigation)
> - `lightPosition` **reset** to (0, 0.8, 0) (codex 09 F1)

**Build warning gate.** The "37 in `3d/src/`" number is from
`cmake --build . --config Release --clean-first` per Step 4 reply F4.
Plan revised:

> Build verification: **clean Release rebuild via
> `cmake --build . --config Release --clean-first`** = 0 errors, 37 project
> warnings in `3d/src/`. Step 5 must not increase this. Incremental builds
> can show fewer (only modified files recompile) and Debug typically shows
> 38-40; both are valid baselines but the Release-clean number is the gate.

---

### Summary

| Finding                                               | Sev    | Action  | Result                                                   |
|-------------------------------------------------------|--------|---------|----------------------------------------------------------|
| F1 "Walk through Sponza" overclaim                    | High   | Doc fix | Inside-atrium visibility is a verification gate, not assumed; failure rolls to Step 6 |
| F2 `historyNeedsSeed` on camera movement is wrong     | High   | Doc fix | 5e deleted entirely; architecture note explains why     |
| F3 R reloads OBJ                                      | High   | Doc fix | New `applyOBJViewPreset()` helper; R calls helper, no I/O |
| F4 ImGui capture too coarse                           | Medium | Doc fix | Split keyboard/mouse; cleanup always runs               |
| F5 Stale F1/D/R labels                                | Medium | Doc fix | Final keymap chosen; 5 label sites updated in same patch |
| F6 Mouse-look math singularity                        | Medium | Doc fix | Yaw/pitch scalars + GetMouseDelta, no basis singularity |
| F7 Analytic camera undecided + warning gate stale     | Low-Med| Doc fix | setScene preserves camera; warning gate command-explicit |

All findings land in `doc/4/claude_plan/sponza_sdf_step5_plan.md`.
Implementation waits for the revised plan.
