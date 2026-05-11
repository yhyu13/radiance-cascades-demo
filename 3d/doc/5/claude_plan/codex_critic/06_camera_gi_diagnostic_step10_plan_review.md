# Critic Review 06 - camera_gi_diagnostic_step10_plan.md

Reviewed: 2026-05-10T16:17:28+08:00

Target: `doc/5/claude_plan/camera_gi_diagnostic_step10_plan.md`

## Verdict

The plan describes two orthogonal features that address real user complaints. The camera state UI/CLI feature (Feature A) is well-structured: the ImGui panel design, CLI flag naming, and setter/helper decomposition are all reasonable. The GI diagnostic modes (Feature B) have a serious redundancy problem: proposed modes 9 and 10 duplicate existing modes 4 and 6 with negligible alpha-compositing differences, and the plan doesn't acknowledge this overlap. There is also a significant line-reference error (alpha-validation code pointed at RenderDoc init instead of the actual validation), a missing `uSeparateGI` interaction discussion, and several smaller gaps in camera state management.

## Evidence Checked

- `doc/5/claude_plan/camera_gi_diagnostic_step10_plan.md`.
- Current `src/demo3d.cpp`: `renderSettingsPanel()` (line 3431), "Reset Camera" button (line 3448), render-mode radio buttons (lines 3508-3522), mouse-look forward math (lines 481-486), `syncCameraYawPitchFromTarget()` (line 4928), `resetCamera()` (line 4905), `resetCameraToScenePreset()` (line 4944), `applyOBJViewPreset()` (line 4959, alpha-validation at lines 5026-5050), `initRenderDoc()` (lines 4595-4619).
- Current `src/demo3d.h`: `setRenderMode()` (line 494).
- Current `src/main3d.cpp`: argv parser (lines 155-232), `--render-mode=N` (line 189), `--screenshot=` (line 186), `--load-obj=` (line 180), `--switch-to-scene=` (line 198), post-load CLI hook application (lines 258-270).
- Current `res/shaders/raymarch.frag`: `uRenderMode` modes 0-8 (lines 441-557 inside loop, lines 576-583 post-loop), `directColor` computation (line 535), `indirectColor` computation (line 544), alpha-composite line (line 557), `uSeparateGI` early return (lines 549-552), tone-mapping (lines 586-589).
- Current `res/shaders/radiance_3d.comp`: direct-light computation at line 262 (`color = albedo * (diff * uLightColor + vec3(0.05))`).

## What Looks Good

- The two features are genuinely orthogonal: camera state control doesn't touch shaders, and GI diagnostic modes don't touch camera logic. Landing them together is efficient.
- The camera state complaint is real and the proposed solution is well-scoped: read-only copyable display, editable InputFloat3, and three CLI flags that apply after auto-fit.
- The `rebuildCameraTargetFromYawPitch()` extraction from the inline mouse-look math (lines 481-486) is a reasonable refactoring that avoids formula duplication.
- The `syncCameraYawPitchFromTarget()` helper already exists at line 4928, so the target-edit path has its sync function ready.
- The `--camera-*` CLI flags follow the existing `--load-obj` / `--render-mode` naming convention.
- The three proposed setters (`setCameraPosition`, `setCameraTarget`, `setCameraFovy`) follow the existing setter pattern and include `std::cout` logging.
- The GI diagnostic analysis correctly identifies the `vec3(0.05)` ambient floor as a likely culprit: `albedo * (diff * uLightColor + vec3(0.05))` means for Sponza's ~0.5 albedo surfaces, the ambient floor contributes `0.5 * 0.05 = 0.025` per channel in linear space, which can dominate the GI bounce signal.
- The line references for `raymarch.frag:535` (`directColor`), `raymarch.frag:544` (`indirectColor = albedo * indirect`), `raymarch.frag:557` (alpha-composite), `radiance_3d.comp:262` (direct-light formula), `demo3d.cpp:481-486` (forward math), `demo3d.cpp:4928` (syncYawPitch), and `demo3d.cpp:3431` (renderSettingsPanel) are all correct.
- The plan correctly identifies that mode 11 (`albedo * vec3(0.05)` only) is genuinely new and useful — no existing mode isolates the ambient floor.
- The verification plan covers camera display, CLI overrides, alpha-validation, diagnostic mode captures, reset-helper interaction, and log preservation. This is thorough.
- The out-of-scope section correctly defers tuning sliders, ambient floor removal, camera bookmarks, and 3D picking.

## Findings

### 1. Alpha-validation line reference points to RenderDoc init, not validation code

Severity: High

The plan's A4 section and reuse table both reference alpha-validation logic at `demo3d.cpp:4604-4618`. That range is `initRenderDoc()` — setting `rdocCaptureDir`, calling `rdoc_load_api`, and configuring the RenderDoc capture template. It has nothing to do with camera-position validation.

The actual alpha-validation code is at `demo3d.cpp:5026-5050` inside `applyOBJViewPreset()`. It checks whether the computed camera position falls inside a marked surface voxel in `meshVoxelData` and logs a warning. An implementer following the plan would look at line 4604 and see RenderDoc initialization code, not the validation logic they need to reuse.

The correct reference should be `demo3d.cpp:5026-5050`.

### 2. Proposed modes 9 and 10 are nearly redundant with existing modes 4 and 6

Severity: High

Mode 4 ("Direct only") at `raymarch.frag:514-526` already computes `albedo * (diff4 * uLightColor + vec3(0.05))` — exactly the same formula as `directColor` in mode 0 (line 535). The plan's proposed mode 9 outputs `directColor` only, which is identical to mode 4's formula.

Mode 6 ("GI only") at `raymarch.frag:498-512` already computes `albedo * indirect6` — exactly the same formula as `indirectColor` in mode 0 (line 544). The plan's proposed mode 10 outputs `indirectColor` only, which is identical to mode 6's formula.

The only difference is that modes 4 and 6 use early `return` (single surface hit, no alpha compositing) while modes 9 and 10 would be inserted at the alpha-composite line 557 (preserving the march loop's transparency accumulation). For Sponza and other opaque scenes, this difference is negligible — the march loop hits one surface and `alpha = 1.0` at line 556.

The plan doesn't acknowledge this overlap. Adding modes that duplicate existing diagnostics adds UI clutter without meaningful new diagnostic capability. The plan should either:

- Acknowledge the overlap and justify why alpha-compositing variants are needed (e.g., for scenes with transparent surfaces), OR
- Replace modes 9 and 10 with genuinely new diagnostics. For example:
  - Mode 9: direct **without** ambient floor (`albedo * diff * uLightColor` only) — this would isolate what "real" direct lighting contributes and directly compare against mode 11 (ambient floor only). This is genuinely new and not redundant.
  - Mode 10: indirect with tone mapping (currently mode 6 outputs linear, un-tonemapped) — or keep as proposed if the alpha-compositing difference is justified.

### 3. `uSeparateGI` interaction with proposed modes 9/10/11 is not discussed

Severity: Medium

When `uSeparateGI != 0` (GI blur active), `raymarch.frag` returns early at lines 549-552 with `fragColor = vec4(directColor, 1.0); fragGI = vec4(indirectColor, 1.0);`. This early return happens **before** the proposed insertion point at line 557. So when GI blur is active, modes 9/10/11 would never execute — the shader would still output both direct and indirect channels regardless of `uRenderMode`.

The plan should specify that the `uSeparateGI` early-return needs to be modified to respect modes 9/10/11, or that the GI blur path should be disabled when these diagnostic modes are active. Without this, the diagnostic modes would produce incorrect output whenever `useGIBlur` is enabled (which it is by default in mode 0).

### 4. `InputFloat3` "commits on Enter" claim is partially incorrect

Severity: Medium

The plan says "Editable fields use `InputFloat3` (commits on Enter, not per-frame, so no per-frame camera jitter)." This is only true for keyboard input. ImGui's `InputFloat3` widget also supports drag interaction (click and drag on the step buttons or the label area), which triggers value changes per-frame during the drag. During a drag, `ImGui::InputFloat3` returns `true` on every frame where the value changes, and the camera position would be updated per-frame, causing smooth but continuous camera movement — not "jitter" exactly, but continuous position updates that the plan claims to avoid.

If per-frame camera updates during drag are acceptable, the plan should document this. If they're not, the plan should use `ImGuiInputTextFlags_EnterReturnsTrue` on `InputText` fields instead of `InputFloat3`, or add a "Apply" button pattern.

### 5. `rebuildCameraTargetFromYawPitch()` doesn't clamp pitch

Severity: Medium

The existing mouse-look handler clamps `cameraPitch` to ±85° (`glm::clamp(cameraPitch, -1.4835f, 1.4835f)` at line 478). The proposed `rebuildCameraTargetFromYawPitch()` helper computes the forward vector from `cameraYaw`/`cameraPitch` without clamping. If `syncCameraYawPitchFromTarget()` computes a pitch outside ±85° (e.g., when the user sets a target almost directly above the camera), the next call to `rebuildCameraTargetFromYawPitch()` would use that un-clamped pitch, producing a degenerate forward vector.

The plan should specify whether `rebuildCameraTargetFromYawPitch()` should clamp `cameraPitch` after computing it, or whether the ImGui/CLI paths should clamp separately. The mouse-look handler's clamp serves dual purposes (preventing gimbal-lock singularity and preventing degenerate view matrices); the same protection should apply to programmatic camera changes.

### 6. FOVY reset interaction not documented

Severity: Low

`applyOBJViewPreset()` hardcodes `fovy = 60.0f` (line 4992). The plan's proposed `setCameraFovy()` and the InputFloat "FOVY" allow the user to change this. But the "Reset Camera" button calls `resetCameraToScenePreset()` → `applyOBJViewPreset()` which always sets `fovy` back to 60.0f. The plan doesn't document that FOVY changes are lost on camera reset, or propose making FOVY persistent across resets.

If the user sets FOVY to 90°, clicks "Reset Camera", and FOVY silently resets to 60°, that's surprising behavior. The plan should either document this interaction or propose preserving the user-set FOVY across resets.

### 7. CLI flag application order for analytic scenes is underspecified

Severity: Medium

The plan's execution order diagram shows CLI camera flags applied after `loadOBJMesh()` (OBJ path). But for analytic scenes (`--switch-to-scene=N`), the camera preset happens inside `setScene()` which calls `resetCamera()` or `resetCameraToScenePreset()` at a different point in `main3d.cpp` (line 260-261). The plan says "apply AFTER `--load-obj` / `--switch-to-scene`" but the actual application point in `main3d.cpp` needs to be after both `loadOBJMesh()` (line 248-257) and `setScene()` (line 261), which are currently in sequence.

The current code applies `--switch-to-scene` after `--load-obj` at lines 259-262. The CLI camera flags would need a new application block after line 262 (or after `testResetHelper` at line 266). The plan should show the exact insertion point in `main3d.cpp`.

### 8. Mode 9 doesn't isolate direct lighting without ambient floor

Severity: Medium

The plan's diagnostic goal is to show whether the `vec3(0.05)` ambient floor dominates the GI bounce. Mode 9 is described as "directColor only (no indirect, includes 0.05*albedo ambient floor)". But `directColor = albedo * (diff * uLightColor + vec3(0.05))` includes both the real direct lighting (`diff * uLightColor`) and the ambient floor (`vec3(0.05)`) in one term. To compare against mode 11 (ambient floor only), the diagnostic needs a mode that shows **direct without ambient** — `albedo * diff * uLightColor` — so the user can see: mode 9 = mode 11 + (direct without ambient), and compare whether mode 11's contribution is larger than the "real" direct lighting.

Without a "direct without ambient" mode, the diagnostic comparison is:
- Mode 9 = direct + ambient
- Mode 10 = indirect (GI bounce)
- Mode 11 = ambient floor only

The user can compute mode 9 - mode 11 = direct without ambient, but they can't see it directly in a capture. Adding a mode that shows `albedo * diff * uLightColor` (direct, no ambient) would make the diagnostic self-explanatory without requiring manual subtraction of screenshots.

### 9. The `setRenderMode` setter has no range validation for new modes

Severity: Low

The existing setter `void setRenderMode(int m) { raymarchRenderMode = m; }` (demo3d.h:494) accepts any integer. Adding modes 9-11 extends the valid range from 0-8 to 0-11, but the setter still doesn't validate. Passing `--render-mode=99` would set `raymarchRenderMode=99`, and the shader would fall through all `if` branches to the default composite path (rendering like mode 0). This is benign but undocumented — the plan should either add range validation or document the fallback behavior.

### 10. Mode 10 renders black when cascades are disabled

Severity: Low

`indirectColor` is `vec3(0.0)` when `uUseCascade == 0` (line 540: the `if (uUseCascade != 0)` guard). Mode 10 ("indirectColor only") would render entirely black if cascades are disabled. The plan doesn't document this dependency. The ImGui tooltip for mode 10 should warn the user that it requires cascade GI to be enabled.

### 11. The alpha-validation reuse requires extraction into a standalone helper

Severity: Medium

The plan's A4 section says "Reuse the existing alpha-validation logic from `applyOBJViewPreset()`" and "Call it from `setCameraPosition` and the ImGui edit handler." But the current validation code (lines 5026-5050) is tightly embedded inside `applyOBJViewPreset()` — it uses local variables (`camPos`, `uvw`, `volumeOrigin`, `volumeSize`, `volumeResolution`, `meshVoxelData`) that are computed or available in that function's scope. To call it from `setCameraPosition`, the validation needs to be extracted into a standalone method like `validateCameraPosition(const glm::vec3& pos)` that takes a position parameter and accesses the necessary state from `this`.

The plan doesn't specify this extraction step. An implementer might try to call the validation inline from `applyOBJViewPreset()` context, which wouldn't work for arbitrary CLI/UI-provided positions.

### 12. "codex 09 F4" forward reference is ambiguous

Severity: Low

The plan references "codex 09 F4 clean-3D path" for `--screenshot=path.png`. In the existing source comments, "codex 09" refers to Step 9 verification hooks (e.g., line 165: `// codex 09 F1 verification`). No codex 09 review document exists in the `codex_critic` directory (reviews are numbered 01-06 for Steps 8-10). The reference appears to be to the plan's own codex numbering (Step 10 = codex 09 in some internal count), which conflicts with the source's use of "codex 09" for Step 9. This could confuse an implementer trying to look up the referenced finding.

## Verification Gaps To Add

- Before implementing modes 9/10/11, verify that the `uSeparateGI != 0` path doesn't bypass them. Either add mode checks to the `uSeparateGI` early return, or document that GI blur must be disabled for diagnostic modes to work correctly.
- Add a "direct without ambient" mode (e.g., mode 12: `albedo * diff * uLightColor`) so the diagnostic comparison is self-contained without manual screenshot subtraction.
- Test proposed modes 9 and 10 against existing modes 4 and 6 on an opaque scene (Sponza) to confirm they produce visually identical output, then decide whether the redundancy is justified.
- Verify that `InputFloat3` drag interaction produces acceptable camera behavior, or switch to an "Apply" button pattern.
- Test CLI camera flags with `--switch-to-scene=N` (not just `--load-obj`) to confirm the flags apply after the analytic scene preset.
- Verify that `rebuildCameraTargetFromYawPitch()` correctly handles pitch values computed by `syncCameraYawPitchFromTarget()` for near-vertical look directions (pitch > 85°).
- Verify that FOVY changes persist or reset as expected when the "Reset Camera" button is pressed.
- Extract the alpha-validation code from `applyOBJViewPreset()` into a standalone helper before wiring it into `setCameraPosition` and the ImGui edit handler.