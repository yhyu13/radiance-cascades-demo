# Plan: Camera State UI/CLI + GI Diagnostic Modes (revised after codex 06)

## Changelog (post codex `06_camera_gi_diagnostic_step10_plan_review.md`)

All 12 findings accepted. Plan revised before any code lands:

- **F1 (high) doc fix.** Alpha-validation line reference was wrong:
  `:4604-4618` is RenderDoc init, NOT the validation code. Real
  location is `demo3d.cpp:5026-5050` inside `applyOBJViewPreset()`.
- **F2+F8 (high+medium) plan rewrite.** Proposed modes 9 and 10
  duplicated existing modes 4 (direct only) and 6 (GI only). Plan
  now drops the duplicates and uses the freed slots for genuinely
  new diagnostics:
  - **Mode 9** = "Direct WITHOUT ambient floor" (`albedo * diff *
    uLightColor` — strips the hidden `vec3(0.05)`)
  - **Mode 10** = "Ambient floor only" (`albedo * vec3(0.05)`) —
    was originally numbered "mode 11" in the prior plan
  Comparing existing mode 4 vs mode 9 vs mode 10 directly answers
  the diagnostic question without screenshot subtraction:
  Mode 4 = Mode 9 + Mode 10. Mode 6 already exists for GI bounce
  visualization.
- **F3 (medium) plan revision.** `uSeparateGI != 0` early-return at
  `raymarch.frag:549-552` returns BEFORE the alpha-composite
  insertion point at line 557. Proposed modes would be silently
  bypassed when GI blur is on (default). Fix: gate the early-return
  on `uRenderMode == 0` so diagnostic modes always reach the
  composite. Single-line change in raymarch.frag.
- **F5 (medium) plan revision.** `rebuildCameraTargetFromYawPitch()`
  must clamp `cameraPitch` to ±85° (`±1.4835f`) before computing
  the forward vector. Without the clamp, a `syncCameraYawPitchFromTarget()`
  result for a near-vertical target (e.g. user types target right
  above camera) yields un-clamped pitch → degenerate forward.
  Mouse-look's existing clamp is at `demo3d.cpp:478`; the helper
  inherits it.
- **F7 (medium) plan revision.** CLI camera-flag application must
  come AFTER the `--switch-to-scene=N` block (which calls
  `setScene` → `resetCamera`) and AFTER `--test-reset-helper`. Plan
  now specifies exact insertion point: `main3d.cpp` line 266+ (after
  `testResetHelper` block).
- **F11 (medium) plan revision.** Alpha-validation code is currently
  embedded in `applyOBJViewPreset()` and uses local-scope variables.
  Plan now adds an extraction step: refactor to a standalone helper
  `void Demo3D::validateCameraPosition(const glm::vec3& pos, const
  char* originLabel)` reused by `applyOBJViewPreset`,
  `setCameraPosition`, and the ImGui edit handler. Listed in Files
  to Modify.
- **F4 (medium) doc fix.** `InputFloat3` ALSO commits during drag
  interaction (per-frame), not just on Enter. For typed pasting
  this is fine (no per-frame events between keystrokes); for
  drag-step-button interaction this means smooth per-frame camera
  motion. Behavior is acceptable (smooth motion, not jitter); plan
  text reworded.
- **F6 (low) doc fix.** "Reset Camera" button calls
  `applyOBJViewPreset()` which hardcodes `fovy = 60.0f`, so any
  user-set FOVY is reset on click. Documented as intentional this
  step (preserving FOVY across resets is a separate decision).
- **F9 (low) plan revision.** `setRenderMode` gets a simple range
  warning: `if (m < 0 || m > 11) std::cerr << "[Demo3D] WARN: render
  mode " << m << " out of range; rendering as default";`. No clamp
  — existing fallthrough behavior preserved.
- **F10 (low) doc fix.** Existing mode 6 (and the new no-ambient
  mode 9 in scenes with no direct lighting) renders dark/black
  when its source signal is disabled or absent. Documented in the
  ImGui mode-selector tooltips.
- **F12 (low) doc fix.** "codex 09 F4 clean-3D path" reference was
  ambiguous. Clarified to point at `doc/4/claude_plan/codex_critic/
  09_sponza_sdf_step4_impl_review.md` F4 (different folder than the
  Step 10 plan reviews under `doc/5/`). The Step 9 work used
  `--screenshot=path.png` BEFORE the ImGui draw to produce a clean
  3D-only frame.

## Context

Two orthogonal features land together as a quality-of-life pass.
The user has two specific complaints:

1. **Camera reproducibility.** No way to read the current camera
   pose (position/facing) from the UI for copy-paste, and no way to
   set it from CLI for headless captures or RenderDoc runs. Today
   the camera is always set by `applyOBJViewPreset()` (auto-fit
   from bounds, Step 7) or by interactive WASD/RMB-look. Capturing
   a specific viewpoint requires either restarting from the preset
   and counting steering inputs (not reproducible) or modifying
   source code.

2. **Sponza GI looks "uniformly ambient lit".** No visible color
   bounce from the brick walls onto the floor or columns. The
   Explore agent traced the GI pipeline end-to-end and confirmed
   the math is correct — `radiance_3d.comp:262` (`color = albedo *
   (diff * uLightColor + vec3(0.05))`) and `raymarch.frag:544`
   (`indirectColor = albedo * indirect`) both multiply by surface
   albedo. The likely culprit is the hardcoded `vec3(0.05)`
   ambient floor in the directColor computation
   (`raymarch.frag:535`): for Sponza-master's mid-tone (~0.5)
   surfaces, `0.05 * 0.5 = 0.025` per channel of "fake ambient"
   per surface, which can out-shine the GI bounce.

   User chose **diagnose only** (no tuning sliders this round) —
   add render modes that isolate direct/indirect/ambient
   components so the actual contribution breakdown becomes visible.
   Tuning decisions follow next session based on what the
   diagnostic shows.

---

## Approach

### Feature A — Camera state display + control

#### A1. ImGui camera panel (read + edit)

Add a new collapsible "Camera" section in `renderSettingsPanel()`
([demo3d.cpp:3431](src/demo3d.cpp#L3431)) just after the existing
"Reset Camera" button:

```cpp
if (ImGui::CollapsingHeader("Camera state")) {
    // Read-only copyable display via InputText + ReadOnly flag
    char buf[256];
    snprintf(buf, sizeof(buf), "%.4f, %.4f, %.4f",
             camera.position.x, camera.position.y, camera.position.z);
    ImGui::InputText("Position", buf, sizeof(buf),
                     ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);

    snprintf(buf, sizeof(buf), "%.4f, %.4f, %.4f",
             camera.target.x, camera.target.y, camera.target.z);
    ImGui::InputText("Target", buf, sizeof(buf),
                     ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);

    snprintf(buf, sizeof(buf), "yaw=%.4f pitch=%.4f (rad)  yaw=%.2f pitch=%.2f (deg)",
             cameraYaw, cameraPitch,
             glm::degrees(cameraYaw), glm::degrees(cameraPitch));
    ImGui::InputText("Facing", buf, sizeof(buf),
                     ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);

    ImGui::Separator();

    // Editable input boxes
    glm::vec3 posEdit = camera.position;
    if (ImGui::InputFloat3("Set position", &posEdit.x, "%.3f")) {
        camera.position = posEdit;
        // Keep facing direction: rebuild target from yaw/pitch + new pos
        rebuildCameraTargetFromYawPitch();
    }
    glm::vec3 tgtEdit = camera.target;
    if (ImGui::InputFloat3("Set target", &tgtEdit.x, "%.3f")) {
        camera.target = tgtEdit;
        syncCameraYawPitchFromTarget();
    }
    float fovyEdit = camera.fovy;
    if (ImGui::InputFloat("FOVY", &fovyEdit, 1.0f, 5.0f, "%.1f")) {
        camera.fovy = glm::clamp(fovyEdit, 20.0f, 110.0f);
    }
}
```

`InputText` with `ImGuiInputTextFlags_ReadOnly +
AutoSelectAll` is the cleanest "select-and-copy" widget — clicking
the field selects all, Ctrl+C copies. Editable fields use
`InputFloat3` (commits on Enter, not per-frame, so no per-frame
camera jitter).

A new tiny private helper `rebuildCameraTargetFromYawPitch()`
mirrors the inline math at [demo3d.cpp:481-486](src/demo3d.cpp#L481).
**Codex 06 F5: must clamp pitch first** so a `syncCameraYawPitchFromTarget`
result for a near-vertical target doesn't produce a degenerate
forward:

```cpp
void Demo3D::rebuildCameraTargetFromYawPitch() {
    cameraPitch = glm::clamp(cameraPitch, -1.4835f, 1.4835f);   // ±85° (matches mouse-look:478)
    glm::vec3 forward(
        std::cos(cameraPitch) * std::sin(cameraYaw),
        std::sin(cameraPitch),
        std::cos(cameraPitch) * std::cos(cameraYaw)
    );
    camera.target = camera.position + forward;
}
```

Avoids duplicating the formula in 3 places (mouse-look, position
edit, and CLI flag).

#### A2. CLI flags

In `main3d.cpp` argv parser (line ~155-230), add three flags
matching the existing parse style:

| Flag | Effect |
|---|---|
| `--camera-pos=x,y,z` | set `camera.position` after auto-fit |
| `--camera-target=x,y,z` | set `camera.target` after auto-fit + sync yaw/pitch |
| `--camera-fovy=DEG` | set `camera.fovy` after auto-fit |

All applied **after** `applyOBJViewPreset()` AND
`--switch-to-scene=N` (which calls `setScene → resetCamera`) AND
`--test-reset-helper` so user values aren't overridden by any of
those resets. **Codex 06 F7: exact insertion point** is
`main3d.cpp` line 266+ (after the `testResetHelper` block). Apply
order:

```
parse argv
-> demo->loadOBJMesh(path)              // line 248-257; auto-fit fires
-> if (switchToScene)  demo->setScene(N);  // line 259-262; resetCamera fires
-> if (testResetHelper) demo->testResetCameraHelper();  // line 264-267
-- NEW Step 10 CLI camera block goes HERE (line ~268+) --
-> if (cliCameraPosSet)    demo->setCameraPosition(cliCamPos);
-> if (cliCameraTargetSet) demo->setCameraTarget(cliCamTgt);
-> if (cliFovySet)         demo->setCameraFovy(cliFovy);
```

Two new public setters on Demo3D (mirroring the Step 7+ pattern):

```cpp
void Demo3D::setCameraPosition(const glm::vec3& p) {
    camera.position = p;
    rebuildCameraTargetFromYawPitch();   // preserve facing
    std::cout << "[Demo3D] CLI override: position=" << ...;
}
void Demo3D::setCameraTarget(const glm::vec3& t) {
    camera.target = t;
    syncCameraYawPitchFromTarget();
    std::cout << "[Demo3D] CLI override: target=" << ...;
}
void Demo3D::setCameraFovy(float f) {
    camera.fovy = glm::clamp(f, 20.0f, 110.0f);
    std::cout << "[Demo3D] CLI override: fovy=" << f << "\n";
}
```

CLI parse helper for "x,y,z" comma-separated floats — just `sscanf(arg.c_str(), "%f,%f,%f", &x, &y, &z)`.

#### A3. Auto-frame-capture / RenderDoc integration

These flags fire **once** at startup, before
`autoRdocDelaySeconds` warmup expires. So a typical scripted run:

```
RadianceCascades3D.exe --load-obj=sponza-master --gpu-voxelize --gpu-sdf
                       --camera-pos=2.0,0.5,0.5 --camera-target=0,0.3,0
                       --auto-rdoc                    # captures at +8s
                       --exit-frames=600
```

For `--screenshot=path.png` (codex 09 F4 clean-3D path), same
deal — camera flags applied at startup, screenshot fires on the
exit frame at the user-set viewpoint.

#### A4. Camera-position alpha-validation (codex 06 F1+F11 corrected)

The existing alpha-validation logic at
[demo3d.cpp:5026-5050](src/demo3d.cpp#L5026) (inside
`applyOBJViewPreset()`) warns if the camera position lies inside a
marked surface voxel. It currently uses local-scope variables so
can't be called directly from setters.

**Codex 06 F11: extract into a standalone helper first.** New
private method:

```cpp
void Demo3D::validateCameraPosition(const glm::vec3& pos, const char* originLabel);
```

Reads `volumeOrigin`, `volumeSize`, `volumeResolution`, and
`meshVoxelData` from member state; logs `inside`/`outside` voxel
status with `originLabel` ("preset", "CLI", "ImGui edit", etc.)
in the log line. `applyOBJViewPreset()`, `setCameraPosition()`,
and the ImGui edit handler all call it. The warning is a log
line, not a refusal — user can still march from inside.

---

### Feature B — GI diagnostic render modes (codex 06 F2+F8 revised)

`raymarch.frag` already dispatches on `uRenderMode` cases 0-8.
**Codex 06 F2: existing mode 4 already does "direct only" and mode 6
already does "indirect only"** — the original plan duplicated them.
Drop the redundant proposals and add only TWO genuinely new modes:

| Mode | Output | Diagnostic value |
|---|---|---|
| 9 (NEW) | `albedo * diff * uLightColor` — direct lighting WITHOUT the `vec3(0.05)` ambient floor | Isolate "real" direct contribution from the hidden ambient term. Mode 4 = Mode 9 + Mode 10. |
| 10 (NEW) | `albedo * vec3(0.05)` — the hidden ambient floor only | Unconditional ambient that's added to every voxel regardless of light direction. If this dominates Mode 6 (existing GI-only), the GI bounce is being washed out. |

**Existing modes used as-is:**
- **Mode 4** = `albedo * (diff * uLightColor + vec3(0.05))` — direct
  lighting + ambient floor (= Mode 9 + Mode 10)
- **Mode 6** = `albedo * indirect` — GI bounce only (cascade contribution)

The diagnostic comparison is now self-contained: capture mode 4 / 6
/ 9 / 10 of Sponza-master and visually compare:
- Is mode 6 (GI bounce) visible? → cascade GI is doing its job
- Is mode 10 (ambient floor) brighter/comparable to mode 6? → the
  0.05 floor is washing out GI; lower it or boost indirect intensity
  (next session)
- Is mode 9 (real direct) much brighter than both? → direct lighting
  dominates; we're seeing the lit surface and bounce is just polish

**Implementation.** New mode branches inside the alpha-composite
loop ([raymarch.frag:557](res/shaders/raymarch.frag#L557)):

```glsl
// Existing line 557:
//   accumulatedColor += (directColor + indirectColor) * alpha * (1.0 - accumulatedAlpha);
// Add per-mode override for the new diagnostic modes:
vec3 modeColor = directColor + indirectColor;                  // mode 0 default
if      (uRenderMode == 9)  modeColor = albedo * diff * uLightColor;   // direct WITHOUT ambient
else if (uRenderMode == 10) modeColor = albedo * vec3(0.05);            // ambient floor only
accumulatedColor += modeColor * alpha * (1.0 - accumulatedAlpha);
```

**Codex 06 F3: gate uSeparateGI early-return on uRenderMode == 0.**
The existing GI-blur path at [raymarch.frag:549-552](res/shaders/raymarch.frag#L549)
returns BEFORE the alpha-composite — diagnostic modes would be
silently bypassed when GI blur is on (default). Single-line fix:

```glsl
// Existing:
if (uSeparateGI != 0) {
    fragColor = vec4(directColor, 1.0);
    fragGI    = vec4(indirectColor, 1.0);
    return;
}
// Revised (gate on default mode only):
if (uSeparateGI != 0 && uRenderMode == 0) {
    fragColor = vec4(directColor, 1.0);
    fragGI    = vec4(indirectColor, 1.0);
    return;
}
```

ImGui: existing render-mode dropdown gets 2 new entries with
tooltips:
- Mode 9: "Direct (NO ambient floor) — strips vec3(0.05); compare
  vs mode 4"
- Mode 10: "Ambient floor only (albedo × 0.05) — diagnostic
  baseline"

CLI: existing `--render-mode=N` covers it. **Codex 06 F9:**
`setRenderMode` adds a range warning:

```cpp
void setRenderMode(int m) {
    if (m < 0 || m > 11) {
        std::cerr << "[Demo3D] WARN: render mode " << m
                  << " out of range; rendering as default\n";
    }
    raymarchRenderMode = m;
}
```

This is **diagnose-only** per user choice; no slider for tuning
the 0.05 ambient or for boosting indirect. Once we see the
breakdown captures we can decide the real fix next session.

---

## Files to modify

- [src/demo3d.h](src/demo3d.h) — public setters
  `setCameraPosition`, `setCameraTarget`, `setCameraFovy`; new
  private helpers `rebuildCameraTargetFromYawPitch()` (codex 06 F5
  with pitch clamp) and `validateCameraPosition()` (codex 06 F11
  extraction). Updated `setRenderMode` with range warning
  (codex 06 F9). No new state members.
- [src/demo3d.cpp](src/demo3d.cpp):
  - New "Camera state" collapsible block in `renderSettingsPanel()`
  - New `rebuildCameraTargetFromYawPitch()` definition (with
    pitch clamp)
  - **Extract** alpha-validation logic from
    [`applyOBJViewPreset()` lines 5026-5050](src/demo3d.cpp#L5026)
    into standalone `validateCameraPosition(pos, originLabel)`
    helper; have `applyOBJViewPreset()` call it (codex 06 F11)
  - Three new public-setter implementations; setters call
    `validateCameraPosition` before applying
  - Existing render-mode ImGui dropdown extended with 2 new mode
    names + tooltips (codex 06 F2 + F10)
- [src/main3d.cpp](src/main3d.cpp):
  - `--camera-pos=x,y,z`, `--camera-target=x,y,z`, `--camera-fovy=DEG`
    parser entries; apply AFTER `--load-obj` AND
    `--switch-to-scene` AND `--test-reset-helper` (codex 06 F7;
    explicit insertion at line 268+)
- [res/shaders/raymarch.frag](res/shaders/raymarch.frag):
  - Add `uRenderMode == 9/10` branches at the final-color composite
    ([line 557](res/shaders/raymarch.frag#L557))
  - Gate `uSeparateGI != 0` early-return on `uRenderMode == 0`
    so diagnostic modes always reach the composite (codex 06 F3,
    [lines 549-552](res/shaders/raymarch.frag#L549))

No GPU SDF / cascade shader changes; no new textures; no new
toggles.

---

## Reuse from existing code

- `syncCameraYawPitchFromTarget()` ([demo3d.cpp:4928](src/demo3d.cpp#L4928)) —
  used when target changes externally
- Mouse-look's forward-from-yaw-pitch math
  ([demo3d.cpp:481-486](src/demo3d.cpp#L481)) — extracted into
  `rebuildCameraTargetFromYawPitch()`
- `applyOBJViewPreset()`'s alpha-validation code
  ([demo3d.cpp:5026-5050](src/demo3d.cpp#L5026); codex 06 F1
  corrected line ref) — extracted into `validateCameraPosition()`
  and reused by `applyOBJViewPreset`, `setCameraPosition`, and
  the ImGui edit handler
- Existing `--render-mode=N` CLI flag
  ([main3d.cpp:189](src/main3d.cpp#L189)) — no new flag needed
- Existing `setRenderMode` setter
  ([demo3d.h:494](src/demo3d.h#L494))

---

## Verification

1. **Build clean.** 0 errors, ≤ 40 warnings (current baseline).

2. **Camera display.** Launch interactive, click in the new
   InputText fields — Ctrl+C copies. Move with WASD/RMB; values
   update in real time.

3. **Camera CLI sets values.** `--load-obj=sponza-master
   --gpu-voxelize --gpu-sdf --camera-pos=2.0,0.5,0.5
   --camera-target=0,0.3,0 --exit-frames=120
   --screenshot=tools/step10_camera_cli.png`. Log line shows the
   override; capture frames the requested viewpoint.

4. **Camera CLI alpha-validation.** Same command with
   `--camera-pos=0,0,0` (likely inside Sponza wall) — warning logs
   "OUTSIDE/INSIDE SDF volume" line; capture still produces (may
   look broken, expected).

5. **Diagnostic mode captures** of Sponza-master (codex 06 F2
   corrected — uses 2 new modes plus existing 4/6):
   - `tools/step10_sponza_mode0_combined.png` — baseline (existing
     mode 0 = direct + indirect)
   - `tools/step10_sponza_mode4_direct_with_ambient.png` — existing
     mode 4 (direct + 0.05 ambient floor; should equal mode 9 + mode 10)
   - `tools/step10_sponza_mode6_indirect_only.png` — existing mode 6
     (cascade GI bounce contribution)
   - `tools/step10_sponza_mode9_direct_no_ambient.png` — NEW mode 9
     (direct WITHOUT the 0.05 ambient floor)
   - `tools/step10_sponza_mode10_ambient_only.png` — NEW mode 10
     (just the 0.05 ambient floor × albedo)

   The diagnostic comparison is now self-contained:
   - Mode 6 vs Mode 10: if mode 10 is brighter or comparable to
     mode 6, the ambient floor is washing out GI bounce
   - Mode 9 vs Mode 10: if mode 10 dominates mode 9, the "direct
     lighting" signal is mostly fake ambient
   - Mode 4 ≈ Mode 9 + Mode 10 (sanity check)

6. **uSeparateGI verify** — load Sponza with GI blur enabled
   (default) and switch to mode 9/10. Confirm the mode override
   reaches the composite (without codex 06 F3 fix the early-return
   would silently fall through and modes 9/10 would render as
   mode 0).

6. **Reset-helper still works** with the new setters bypassed —
   `--test-reset-helper` after setting camera pos via CLI should
   STILL restore the auto-fit position (the reset path is unchanged).

7. **Logs preserved** to `tools/app_run_step10_*.log`.

---

## Out of scope (deferred to next session)

- GI tuning sliders (indirect-intensity, ambient-floor) — user
  chose diagnose-only this round
- Removing the `vec3(0.05)` ambient floor — needs the diagnostic
  captures first to decide
- Camera bookmarks per scene
- Camera-position-from-mouse-click (pick from 3D)
- ImGui camera-look-at gizmo
