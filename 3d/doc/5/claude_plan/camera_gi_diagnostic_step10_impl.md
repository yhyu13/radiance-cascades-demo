# Step 10 — Implementation Notes: Camera State UI/CLI + GI Diagnostic Modes

**Date:** 2026-05-10
**Status:** Implemented and verified. Build clean (0 errors); 5 Sponza
diagnostic captures land cleanly and produce a definitive answer to the
user's "GI looks like uniform ambient" complaint.

**Plan source-of-truth:** [camera_gi_diagnostic_step10_plan.md](camera_gi_diagnostic_step10_plan.md)
(revised after codex 06 review — all 12 findings folded in).

---

## Summary

| Change | File | Effect |
|---|---|---|
| 3 public setters + 2 private helpers + `setRenderMode` range warn | [src/demo3d.h](../../../src/demo3d.h) | New camera API surface; range-check guards future modes |
| `validateCameraPosition` extracted from `applyOBJViewPreset` | [src/demo3d.cpp:5026](../../../src/demo3d.cpp#L5026) | Standalone helper consumed by preset, CLI setter, ImGui edit |
| `rebuildCameraTargetFromYawPitch` (with ±85° pitch clamp) | [src/demo3d.cpp](../../../src/demo3d.cpp) (after `applyOBJViewPreset`) | Mirrors mouse-look forward math; preserves facing on position-only edits |
| 3 setters (`setCameraPosition/Target/Fovy`) | [src/demo3d.cpp](../../../src/demo3d.cpp) | Each logs override + calls validation/sync |
| ImGui "Camera state" CollapsingHeader | [src/demo3d.cpp:3496-area](../../../src/demo3d.cpp#L3496) | Read-only InputText (copyable) + InputFloat3 editable |
| Render-mode dropdown extended (modes 9, 10) with tooltips | [src/demo3d.cpp:3508-area](../../../src/demo3d.cpp#L3508) | RadioButton + IsItemHovered tooltip |
| 3 CLI flags (`--camera-pos`, `--camera-target`, `--camera-fovy`) + apply block at line 268+ | [src/main3d.cpp](../../../src/main3d.cpp) | Apply AFTER `--load-obj`/`--switch-to-scene`/`--test-reset-helper` (codex 06 F7) |
| Mode 9/10 branches at composite + `uSeparateGI` gate on `uRenderMode == 0` | [res/shaders/raymarch.frag:549-560](../../../res/shaders/raymarch.frag#L549) | New diagnostic outputs; gate prevents silent bypass under default GI blur |

No new shaders, no new GPU resources, no new state members.

---

## Diagnostic Outcome (the load-bearing finding)

The 5 Sponza-master captures (auto-fit camera, GPU SDF + GPU voxelize):

| Mode | Output | Visual |
|---|---|---|
| 0 (combined) | `directColor + indirectColor` | Brown silhouette body + bright orange rim along top ceiling edge |
| 4 (existing direct + ambient) | `albedo * (diff * uLightColor + vec3(0.05))` | Pure brown silhouette, NO orange rim |
| 6 (existing GI bounce) | `albedo * indirect` | Mostly black, bright orange rim along top edge only |
| **9 (NEW direct, no ambient)** | `albedo * diff * uLightColor` | **Completely BLACK** |
| **10 (NEW ambient floor only)** | `albedo * vec3(0.05)` | Pure brown silhouette (≡ Mode 4) |

**What this proves:**

- **Mode 9 = black** → `diff = max(dot(normal, lightDir), 0.0) * (1 - shadow)` is ~0 across every visible surface. Either the light is shadowed from these surfaces, or the surfaces face away from `uLightPos`. Direct lighting reaches **none** of the visible geometry from this camera angle.
- **Mode 4 ≈ Mode 10** → Mode 4's "Direct only" output is essentially 100% the hidden `vec3(0.05)` ambient floor. The "direct light" label in the existing UI was misleading for any scene where the camera-visible surfaces aren't facing the light.
- **Mode 0 ≈ Mode 6 (rim) + Mode 10 (body)** → 99% of what the user sees in the default render is the ambient floor; the only "real" GI is the orange ceiling-bounce rim.

**Diagnosis confirmed:** the `vec3(0.05)` ambient floor in `raymarch.frag:535` IS what the user perceives as "uniform ambient lighting". The cascade GI is doing real work (the orange rim is bounce light from the lit ceiling), but its contribution to camera-facing walls/floor is dominated by `albedo * 0.05` for surfaces whose direct contribution is also 0. Tuning options open for next session:

- Lower the `0.05` floor (e.g. `0.005` or remove)
- Boost indirect intensity multiplier
- Reposition default light so direct lighting reaches more of the scene
- All three (orthogonal)

The user explicitly chose "diagnose only" for Step 10 — these decisions belong to Step 11.

Captures live in:

- [tools/step10_sponza_mode0_combined.png](../../../tools/step10_sponza_mode0_combined.png)
- [tools/step10_sponza_mode4_direct_with_ambient.png](../../../tools/step10_sponza_mode4_direct_with_ambient.png)
- [tools/step10_sponza_mode6_indirect_only.png](../../../tools/step10_sponza_mode6_indirect_only.png)
- [tools/step10_sponza_mode9_direct_no_ambient.png](../../../tools/step10_sponza_mode9_direct_no_ambient.png)
- [tools/step10_sponza_mode10_ambient_only.png](../../../tools/step10_sponza_mode10_ambient_only.png)

---

## Feature A — Camera State UI + CLI

### Setters and helpers

`Demo3D::setCameraPosition(pos)` assigns `camera.position`, calls
`rebuildCameraTargetFromYawPitch()` to preserve the existing facing
direction (pos-only edit doesn't change yaw/pitch), then calls
`validateCameraPosition(pos, "CLI/setter")` and logs the override.

`Demo3D::setCameraTarget(t)` assigns `camera.target` and calls the
existing `syncCameraYawPitchFromTarget()` ([demo3d.cpp:4928](../../../src/demo3d.cpp#L4928))
so subsequent mouse-look starts from the new forward.

`Demo3D::setCameraFovy(f)` clamps to `[20, 110]` (matching the ImGui
edit clamp).

`Demo3D::rebuildCameraTargetFromYawPitch()` reuses the inline mouse-look
math at [demo3d.cpp:481-486](../../../src/demo3d.cpp#L481) but with
`cameraPitch` clamped to ±85° at the top **before** computing forward
(codex 06 F5). Without the clamp, a `syncCameraYawPitchFromTarget()`
result for a near-vertical user-typed target could yield a degenerate
forward.

`Demo3D::validateCameraPosition(pos, originLabel)` is the F11 extraction
of the alpha-sample validation that previously lived inline in
`applyOBJViewPreset()` (lines 5026-5050 pre-extract). Now standalone:
reads `volumeOrigin`/`volumeSize`/`volumeResolution`/`meshVoxelData`
from member state, logs inside/outside-volume status with the caller's
`originLabel` ("preset" / "CLI/setter" / "ImGui edit"). The original
caller in `applyOBJViewPreset()` now reduces to a single line:

```cpp
validateCameraPosition(camPos, "preset");
```

### ImGui Camera-state panel

Inserted as a `CollapsingHeader("Camera state")` in `renderSettingsPanel()`
between the Reset Camera/Screenshot block and the "Cascade GI" block.

Read-only display fields use `InputText` with `ReadOnly | AutoSelectAll`:
clicking selects all text, Ctrl+C copies to clipboard. Three fields:

- **Position** — `%.4f, %.4f, %.4f` of `camera.position`
- **Target** — `%.4f, %.4f, %.4f` of `camera.target`
- **Facing** — yaw/pitch in both rad and deg

Editable fields:

- **Set position** — `InputFloat3`, on commit reassigns + rebuilds target + validates
- **Set target** — `InputFloat3`, on commit reassigns + syncs yaw/pitch
- **FOVY** — `InputFloat` with step 1/5, on commit clamps to [20, 110]

`InputFloat3` also commits per-frame during drag interaction (codex 06
F4). Acceptable for this round — drag motion is intentional, not jitter.

### CLI flags

```
--camera-pos=x,y,z
--camera-target=x,y,z
--camera-fovy=DEG
```

Parsed with `std::sscanf("%f,%f,%f", ...)`. Stored in three local
`bool ... = false; glm::vec3 ...;` pairs in `main3d.cpp main()`. Apply
block lives at line ~270, AFTER:

```
loadOBJMesh()       // line 248-257; auto-fit fires
setScene()          // line 259-262; resetCamera fires
testResetHelper()   // line 263-266
toggleGpuSdfOff()   // line 267-270
-- NEW Step 10 CLI camera block --
setCameraPosition(cliCameraPos);
setCameraTarget(cliCameraTarget);
setCameraFovy(cliFovy);
```

Codex 06 F7 nailed the position correctly: CLI flags now win over EVERY
prior reset path. Verified runtime by `--test-reset-helper
--camera-pos=2.0,0.5,0.5 --camera-target=0,0.3,0` showing the override
fires AFTER reset restores the auto-fit position (log:
[tools/app_run_step10_reset_then_cli.log](../../../tools/app_run_step10_reset_then_cli.log)).

---

## Feature B — GI Diagnostic Render Modes

### Shader changes

Two surgical edits to `raymarch.frag`:

1. **Gate `uSeparateGI` early-return on `uRenderMode == 0`** (codex 06
   F3, [raymarch.frag:549-552](../../../res/shaders/raymarch.frag#L549)):

   ```glsl
   if (uSeparateGI != 0 && uRenderMode == 0) {
       fragColor = vec4(directColor,   1.0);
       fragGI    = vec4(indirectColor, 1.0);
       return;
   }
   ```

   Without this gate, the GI-blur pipeline (default-on) would silently
   return the mode-0 channels regardless of `uRenderMode`, making
   modes 9/10 invisible.

2. **Mode 9/10 branches replace the inline composite** at the alpha-blend
   line ([raymarch.frag:557](../../../res/shaders/raymarch.frag#L557)):

   ```glsl
   vec3 modeColor;
   if      (uRenderMode == 9)  modeColor = albedo * diff * uLightColor;
   else if (uRenderMode == 10) modeColor = albedo * vec3(0.05);
   else                        modeColor = directColor + indirectColor;
   accumulatedColor += modeColor * alpha * (1.0 - accumulatedAlpha);
   ```

   `albedo`, `diff`, and `uLightColor` are already in scope at this
   loop iteration (mode 0 path). Modes 1/2/3/4/6/7/8 still early-return
   above this line; mode 5 is post-loop. Modes 9/10 reach the composite
   with one surface hit and break, same control flow as mode 0.

### UI tooltips (codex 06 F2 + F10)

Two new RadioButton entries with `IsItemHovered` tooltips:

- **DirectNoAmb (9)** — "Direct lighting WITHOUT the hidden vec3(0.05)
  ambient floor. Formula: albedo * diff * uLightColor. Compare vs Mode
  4 (=Mode 9 + Mode 10). Dark in scenes with no direct light source."
- **AmbFloor (10)** — "Ambient floor only (albedo * vec3(0.05)).
  Diagnostic baseline -- if this dominates Mode 6 (GI bounce), the
  floor is washing out cascade GI. Independent of light direction."

### `setRenderMode` range guard (codex 06 F9)

Inline setter in `demo3d.h`:

```cpp
void setRenderMode(int m) {
    if (m < 0 || m > 11) {
        std::cerr << "[Demo3D] WARN: render mode " << m
                  << " out of range [0,11]; rendering as default\n";
    }
    raymarchRenderMode = m;
}
```

Upper bound 11 leaves one slot for the next diagnostic mode without
re-bumping the warning. No clamp — preserves existing shader
fallthrough behavior.

---

## Verification (executed)

| # | Test | Outcome | Log |
|---|---|---|---|
| 1 | Build (cmake --build build --config Release) | 0 errors, 2 new sscanf-deprecation nags only | (build console) |
| 2 | Smoke test: `--load-obj=cornell --camera-pos=1.5,0.5,3.0 --camera-target=0,0,0 --camera-fovy=70` | Override fires AFTER preset, screenshot from new viewpoint | [tools/app_run_step10_smoke.log](../../../tools/app_run_step10_smoke.log), [tools/step10_smoke_cornell.png](../../../tools/step10_smoke_cornell.png) |
| 3 | Sponza-master mode 0/4/6/9/10 captures | All 5 land; reveal diagnostic outcome above | `tools/app_run_step10_sponza_mode{0,4,6,9,10}.log` + 5 PNGs |
| 4 | Inside-volume CLI: `--camera-pos=0,0,0` on Sponza-master | "validation (inside volume): voxel=(64,64,64) alpha=0" — extracted helper works, originLabel "CLI/setter" appears | [tools/app_run_step10_alpha_inside.log](../../../tools/app_run_step10_alpha_inside.log) |
| 5 | uSeparateGI gate (default GI blur on) | Mode 9 produces black, mode 0 produces brown — different output → gate fires correctly | (implicit from #3) |
| 6 | Reset-helper interaction: `--test-reset-helper --camera-pos=2.0,0.5,0.5` | preset fires → reset fires → CLI override fires LAST and wins | [tools/app_run_step10_reset_then_cli.log](../../../tools/app_run_step10_reset_then_cli.log) |

---

## Codex 06 Findings — All 12 Folded In

| # | Sev | Resolution |
|---|---|---|
| F1  | High | Doc fix: alpha-validation line ref `:4604-4618` (RenderDoc init) → `:5026-5050` (real validation, now extracted) |
| F2  | High | Plan rewrite: dropped redundant modes 9/10 (= existing 4/6); used freed slots for new "direct without ambient" + "ambient floor only" |
| F3  | Med  | Code: `uSeparateGI` early-return gated on `uRenderMode == 0` |
| F4  | Med  | Doc: `InputFloat3` per-frame commits during drag acknowledged (acceptable smooth motion) |
| F5  | Med  | Code: `rebuildCameraTargetFromYawPitch` clamps pitch to ±85° before forward computation |
| F6  | Low  | Doc: "Reset Camera" silently restores fovy=60 documented as intentional this round |
| F7  | Med  | Code: CLI camera apply block lives at `main3d.cpp` line ~270, AFTER `--switch-to-scene` AND `--test-reset-helper` |
| F8  | Med  | Same as F2 (mode 10 = ambient-only, not duplicate of mode 6) |
| F9  | Low  | Code: `setRenderMode` range warning (no clamp; range [0, 11] for headroom) |
| F10 | Low  | Doc: ImGui mode 9/10 tooltips warn about cascade/light dependencies |
| F11 | Med  | Code: `validateCameraPosition(pos, originLabel)` extracted as standalone Demo3D method; reused by preset/setter/edit |
| F12 | Low  | Doc: cross-folder codex-09-F4 reference clarified |

---

## What's Open (next session candidates)

| Item | Notes |
|---|---|
| Lower / remove the `vec3(0.05)` ambient floor in `raymarch.frag:535` | Diagnostic above shows it dominates user-visible surfaces; first knob to try |
| Boost indirect intensity multiplier (existing `indirectBrightness`) | If lowering 0.05 makes scenes too dark, compensate with stronger GI |
| Reposition default Sponza light so direct contribution reaches more surfaces | Mode 9 going completely black means the auto-fit camera angle + default light position is worst-case |
| Camera bookmarks (per-scene saved viewpoints) | Step 10 deferred this; could land alongside ambient-floor tuning |
| 3D-pick camera target via mouse | Step 10 deferred this; nice-to-have |
| Real `.tga` texture loading + UV-driven voxel sampling | Independent track from Step 10 (was an open item from Step 9 followups) |

---

## Architecture Notes

**Validation extraction is the single most important change** for future
work. Previously, alpha-validation was tightly bound to the auto-fit
preset path — calling it from anywhere else would have required
duplicating ~20 lines per call site. The new helper takes `pos` +
`originLabel` and reads everything else from member state. Adding
validation to a new caller (e.g. mouse-look's terminal position when
WASD movement ends) is now a one-liner.

**Render-mode range guard preserves existing fallthrough.** The
`setRenderMode` warn-but-don't-clamp design means out-of-range values
still produce mode-0-style output (existing shader behavior). Code that
relies on this fallthrough (e.g. tests that pass invalid modes
intentionally) is unaffected — the warning is informational.

**The `uRenderMode == 0` gate on the GI-blur split path** is a small
but load-bearing change. It cleanly separates "production rendering
(uses GI blur)" from "diagnostic modes (composite directly)". Future
diagnostic modes can be added at the composite without further
plumbing — they automatically bypass the GI-blur pipeline.

**Mode 9/10 share `albedo` + `diff` with mode 0** (no new texture
fetches, no new uniforms). Cost is one extra `if/else` branch per
fragment per surface hit — negligible.

---

## Why This Step Was Worthwhile

The user's original complaint ("Sponza GI looks like uniform ambient
lit") was correct, but the cause was non-obvious — could have been:

1. Cascade probes failing to populate (would need C0 bake debug)
2. Direct lighting overwhelming GI (would need light-position tuning)
3. The hidden `vec3(0.05)` floor washing out everything (the actual
   answer, but non-obvious from inspection)

Without the diagnostic modes, "tune the GI" would have meant
trial-and-error with `indirectBrightness`, light position, cascade
counts, and shadow-ray flags — each adjustment requiring a full
visual evaluation. Mode 9 going completely black + Mode 10 ≡ Mode 4
gave an unambiguous one-frame answer: the 0.05 floor is dominant on
camera-visible surfaces, and the GI is genuinely contributing only
where direct light reaches the source surface (the orange ceiling rim
in mode 6).

This makes Step 11's tuning decisions data-driven rather than
guess-and-check.
