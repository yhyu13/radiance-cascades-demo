# Critic Review 08 - gi_bake_strip_heatmap_step11_impl.md

Reviewed: 2026-05-10T18:08:11+08:00

Target: `doc/5/claude_plan/gi_bake_strip_heatmap_step11_impl.md`

## Verdict

Step 11 is a genuine implementation with all claimed code changes present in the current source. The `uStripAmbientFloor` uniform and branch exist in `radiance_3d.comp`, the hoisted `indirect` and heatmap modes 11/12/13 exist in `raymarch.frag`, the setter and ImGui guard exist in `demo3d.h`/`.cpp`, and all 10 verification screenshots and log files are present on disk. The diagnostic outcome analysis (mixed A/B: right wall/floor nearly black with strip, left pillars retain dim structure) is well-argued and directly answers the user's question.

The implementation has one significant bug: the `setStripAmbientFloorBake` setter includes `meshSDFReady = false` which forces an unnecessary SDF rebake (~3-7 ms GPU JFA or CPU EDT) on every toggle change. The strip toggle only affects cascade probe radiance (a lighting change), not geometry, so `meshSDFReady = false` is unnecessary. The doc conflates this with the Step 8 dynamic-sphere pattern which legitimately needs `meshSDFReady = false` because the sphere modifies geometry. Several line references are stale (pointing to pre-Step-11 source rather than current), and the mode 12 divisor saturation is correctly identified as a known issue.

## Evidence Checked

- `doc/5/claude_plan/gi_bake_strip_heatmap_step11_impl.md`.
- Current `res/shaders/radiance_3d.comp`: `uStripAmbientFloor` uniform at line 42 (after `uLightColor`), branch at lines 267-269 (not 262 — the original formula's location).
- Current `res/shaders/raymarch.frag`: hoisted `indirect` at line 541, `indirectColor` at line 549 (inside `if (uUseCascade != 0)` block at lines 545-550), heatmap modes at lines 552-573, uSeparateGI gate at line 577 (not 549 — shifted by Step 11 changes), mode 11 divisor `/0.1` at line 560, mode 12 divisor `/0.05` at line 561, mode 13 at lines 562-566.
- Current `src/demo3d.h`: `stripAmbientFloorBake` member at line 793, `setStripAmbientFloorBake` setter at lines 512-522 (includes `meshSDFReady = false` at line 515), `setRenderMode` upper bound 14 at lines 494-502.
- Current `src/demo3d.cpp`: `uStripAmbientFloor` binding at lines 2081-2082, `BeginDisabled(!useCascadeGI)` at line 3563, `stripAmbientFloorBake` checkbox at lines 3564-3567, heatmap RadioButtons at lines 3600-3606 with cascade-dependency tooltips.
- Current `src/main3d.cpp`: `--strip-ambient-floor-bake` flag at line 217.
- All 10 verification screenshots confirmed in `tools/` directory.
- All 10 log files confirmed in `tools/` directory.
- Step 8 dynamic-sphere invalidation pattern at `demo3d.cpp:623-627` (5 lines including `meshSDFReady = false`).

## What Looks Good

- All claimed code changes exist in the current source and match the document's description.
- All 10 verification screenshots and log files exist on disk.
- The diagnostic outcome analysis is thorough and well-argued: the mixed A/B result (right wall/floor nearly black = Outcome A; left pillars dim but visible = Outcome B) directly answers the user's question and provides a calibrated decision framework for Step 12.
- The mode 0 + strip capture correctly documents the mixed state: `directColor` still has the local 0.05 floor at `raymarch.frag:535`, while `indirectColor` no longer has 0.05 baked in from source surfaces. The doc correctly concludes "removing JUST the bake floor isn't enough" — both the bake and local floors need to be addressed.
- The `uStripAmbientFloor` uniform declaration at line 42 (after `uLightColor` at line 39) follows the lighting-uniforms semantic grouping (codex 07 F9).
- The `BeginDisabled(!useCascadeGI)` guard correctly disables the checkbox when cascade GI is off (codex 07 F6), and the tooltips warn about cascade dependency.
- The hoisted `indirect` variable at line 541 is correctly initialized to `vec3(0.0)` when cascades are disabled, making heatmaps degenerate to all-green (not broken).
- The heatmap insertion at lines 552-573 is structurally correct: AFTER the main-path computation (lines 535-550) and BEFORE the `uSeparateGI` gate (line 577), consuming `directColor`/`indirectColor`/`indirect` from the main path (codex 07 F3).
- The mode 12 divisor saturation is honestly reported as a known issue with a clear recommended fix (retune `/0.05` → `/0.5` or `/1.0`). The doc doesn't reship — it documents the issue and defers the one-line shader edit to Step 12. This is appropriate for diagnostic-only.
- The mode 13 (GI fraction) analysis correctly concludes that "GI is the DOMINANT light source for >50% of visible pixels" — this directly answers the "is GI being washed out?" question with evidence.
- The codex 07 findings table (11 items) is comprehensive and correctly claims resolutions for all items.
- The out-of-scope items are well-defined: bilateral GI blur interaction, toggle reset on scene change, and the Step 12 decision framework.

## Findings

### 1. `meshSDFReady = false` in setter forces unnecessary SDF rebake

Severity: High

The `setStripAmbientFloorBake` setter at `demo3d.h:512-522` includes 5 invalidation lines, including `meshSDFReady = false` at line 515. The strip toggle only affects cascade probe radiance at `radiance_3d.comp:267-269` — it does NOT change geometry, the voxel grid, or the SDF texture. Setting `meshSDFReady = false` forces `sdfGenerationPass()` to re-run (checked at `demo3d.cpp:681`: `if (!sdfReady || (useOBJMesh && !meshSDFReady))`), which dispatches the GPU JFA SDF shader or CPU EDT computation. This is ~3-7 ms of wasted GPU/CPU work per toggle change that produces the exact same `sdfTexture` as before.

The Step 8 dynamic-sphere pattern at `demo3d.cpp:623-627` legitimately needs `meshSDFReady = false` because the sphere modifies the voxel grid (geometry change). The strip toggle is a lighting-only change — the SDF should not be invalidated.

The doc calls this the "canonical 5-line invalidation pattern" matching the dynamic-sphere branch, but conflates two different invalidation scopes: geometry changes (need `meshSDFReady = false`) and lighting changes (don't). The setter should only include the 4 cascade-specific lines:

```cpp
cascadeReady        = false;
forceCascadeRebuild = true;
renderFrameIndex    = 0;
historyNeedsSeed    = true;
```

Suggested fix: remove `meshSDFReady = false` from `setStripAmbientFloorBake()`. If there's a defensive concern about stale SDF, add a comment explaining why it's intentionally excluded for lighting-only toggles.

### 2. Line reference "branch at line 262" is stale

Severity: Low

The doc's summary table says "`uStripAmbientFloor` uniform + branch at line 262" for `radiance_3d.comp`. The original bake formula was at line 262 before Step 11 changes. After adding the comment and conditional, the branch is now at lines 267-269. The uniform declaration is at line 42.

The doc references the original location (line 262 = the formula that was modified), not the current state. An implementer reading the doc alongside the current source would find the shadow computation at line 262, not the strip branch.

### 3. Raymarch.frag insertion-point line references are stale

Severity: Low

The doc says heatmap branches are inserted "AFTER the main-path `directColor` / `indirectColor` / hoisted `indirect` are computed (line ~544), BEFORE the Step 10 `uSeparateGI` gate at line 549."

After Step 11's changes (hoisting `indirect`, adding heatmap modes), the line numbers shifted:
- `indirectColor = albedo * indirect;` is now at line 549 (not 544)
- The `uSeparateGI` gate is now at line 577 (not 549)

The references describe the pre-Step-11 state, not the current source. This is reasonable for an implementation note that describes the change process, but could confuse someone reading the doc alongside the current source.

### 4. Doc claims "canonical 5-line invalidation pattern" for a lighting-only toggle

Severity: Medium

The doc says the setter "runs the canonical 5-line invalidation pattern (matches the Step 8 dynamic-sphere ENABLE branch at `demo3d.cpp:623-627`)." But the dynamic-sphere pattern includes `meshSDFReady = false` because the sphere modifies geometry. The strip toggle is lighting-only and shouldn't need SDF invalidation.

The doc's framing implies all invalidation patterns must include all 5 lines, which is incorrect. Different invalidation scopes require different subsets:
- Geometry changes (dynamic sphere, scene switch): 5 lines (meshSDFReady + cascadeReady + forceCascadeRebuild + renderFrameIndex + historyNeedsSeed)
- Lighting changes (strip toggle, light position): 4 lines (cascadeReady + forceCascadeRebuild + renderFrameIndex + historyNeedsSeed)

The doc should distinguish between these two patterns rather than calling the 5-line version "canonical."

### 5. Mode 12 divisor `/0.05` is acknowledged but left in source as-is

Severity: Low

The doc correctly identifies mode 12's saturation and recommends retuning from `/0.05` to `/0.5` or `/1.0`. But the current shader at `raymarch.frag:561` still has `/0.05`. The doc says "documented as known-issue rather than reshipped." This is acceptable for diagnostic-only, but anyone running the current build will see saturated red in mode 12. The doc should note that the retune is a one-line shader edit that can be applied immediately without rebuilding the entire project, or defer it explicitly to a specific future step.

### 6. Verification test 7 (ImGui checkbox gating) was not runtime-tested

Severity: Low

The doc acknowledges that the `BeginDisabled(!useCascadeGI)` guard was "not runtime-tested" because "code path is clear-cut." While the code path is indeed simple, this leaves a verification gap: the guard could be inverted (disabling when cascades ARE on), the tooltip condition could be wrong, or the `EndDisabled()` call could be misplaced. A quick interactive test would confirm the behavior. This is acceptable for diagnostic-only but should be flagged for a future verification pass.

### 7. The setter adds `meshSDFReady = false` which was NOT in the original plan

Severity: Medium

The plan's Step 2 setter code (review 07) showed 4 invalidation lines:

```cpp
forceCascadeRebuild = true;
cascadeReady = false;
historyNeedsSeed = true;
renderFrameIndex = 0;
```

The implementation added a 5th line (`meshSDFReady = false`) that wasn't in the plan. This is the unnecessary line identified in finding 1. The doc presents this as "matching the canonical 5-line pattern" without acknowledging that the plan specified only 4 lines and the 5th was added post-plan without justification for why a lighting-only toggle needs SDF invalidation.

### 8. No verification of bilateral GI blur interaction with strip toggle

Severity: Low

The known open items table correctly lists "bilateral GI blur interaction with strip: Not tested." The GI blur operates on `indirectColor`, which changes when the strip is toggled (because the cascade probes store different radiance). The blur should reflect the stripped bake naturally, but this hasn't been confirmed with a capture. This is an honest gap that the doc flags for Step 12.

## Verification Gaps To Add

- Remove `meshSDFReady = false` from `setStripAmbientFloorBake()` and verify that toggling the strip still produces correct cascade rebake without the unnecessary SDF rebake. Measure the toggle time with and without the `meshSDFReady` line to confirm the ~3-7 ms savings.
- Run a quick interactive test of the `BeginDisabled(!useCascadeGI)` guard: disable cascade GI, verify the strip checkbox is grayed out, enable cascade GI, verify it's interactive.
- Capture mode 6 + strip with `useGIBlur=true` to verify that bilateral GI blur correctly reflects the stripped cascade radiance (not blending stale un-stripped history).
- Apply the mode 12 divisor retune (`/0.05` → `/0.5` or `/1.0`) and recapture to confirm mode 12 shows useful spatial variation instead of saturation.
- Add a comment in `setStripAmbientFloorBake()` explaining why `meshSDFReady` is intentionally excluded (or included, if the current behavior is kept) for lighting-only invalidation, to prevent future implementers from blindly copying the 5-line pattern.