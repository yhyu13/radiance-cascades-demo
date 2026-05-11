# Critic Review 07 - gi_bake_strip_heatmap_step11_plan.md

Reviewed: 2026-05-10T17:30:10+08:00

Target: `doc/5/claude_plan/gi_bake_strip_heatmap_step11_plan.md`

## Verdict

The plan addresses a real diagnostic gap that Step 10 couldn't resolve: the `vec3(0.05)` ambient floor is baked into cascade probe radiance at `radiance_3d.comp:262`, so mode 6 ("GI only") still contains ambient-floor energy from source surfaces even after Step 10 added mode 9 ("direct without ambient") and mode 10 ("ambient floor only"). The `uStripAmbientFloor` toggle that forces cascade rebake is the correct architectural approach — it modifies the bake shader, not just the display shader, so mode 6 genuinely shows "pure direct-lit bounce" when the strip is on. The three heatmap modes (11/12/13) provide spatial visualization matching the existing mode 5/7 convention.

The plan has several issues: a wrong line reference for the `forceCascadeRebuild` pattern (points to RenderDoc capture instead of a canonical example), shifted line references for the heatmap palette code (mode 5 is at 588-594 not 576-582, mode 7 at 489-495 not 486-495), a structural inconsistency in describing heatmap modes as "early-returns matching mode 4/6/7 pattern" when they actually consume the main-path `directColor`/`indirectColor` computation (unlike modes 4/6 which compute their own), no discussion of what mode 0 or mode 6 would look like with the strip toggle on (particularly whether mode 6 might be nearly black), no `uUseCascade` dependency guard (the toggle is meaningless when cascades are disabled), and no justification for the `/0.5` heatmap normalization divisor.

## Evidence Checked

- `doc/5/claude_plan/gi_bake_strip_heatmap_step11_plan.md`.
- Current `res/shaders/radiance_3d.comp`: `uStripAmbientFloor` not yet added, `vec3(0.05)` at line 262 confirmed, uniform block at lines 17-74.
- Current `res/shaders/raymarch.frag`: modes 0-10 confirmed (mode 9 = `albedo * diff * uLightColor`, mode 10 = `albedo * vec3(0.05)`), `uSeparateGI` gate at line 552 with `uRenderMode == 0` guard, mode 5 heatmap at lines 588-594, mode 7 heatmap at lines 489-495, `directColor` at line 535, `indirectColor` at line 544, `indirect` local inside `if (uUseCascade != 0)` at lines 540-544.
- Current `src/demo3d.cpp`: render-mode radio buttons (lines 3558-3576), `uLightColor` binding at line 2079, RenderDoc `forceCascadeRebuild` at line 4687, mode 9/10 tooltip at line 3575.
- Current `src/demo3d.h`: `setRenderMode` with range check `[0,11]` at lines 494-503, no `stripAmbientFloorBake` member yet.
- Current `src/main3d.cpp`: CLI flags at lines 155-232.

## What Looks Good

- The diagnostic motivation is correct: Step 10's mode 6 still contains 0.05 ambient-floor energy baked into source-surface radiance at `radiance_3d.comp:262`. Mode 9 (direct without ambient) and mode 10 (ambient floor only) only strip the floor from the DISPLAY shader (`raymarch.frag`), not from the BAKE shader. The plan correctly identifies that the bake needs modification.
- The `uStripAmbientFloor` toggle architecture is sound: it adds a uniform to `radiance_3d.comp` and forces a full cascade rebake (`forceCascadeRebuild + cascadeReady = false + historyNeedsSeed + renderFrameIndex = 0`). When the strip is on, probes store `albedo * diff * uLightColor` instead of `albedo * (diff * uLightColor + vec3(0.05))`, so mode 6 genuinely shows bounce from real direct lighting only.
- The rebake invalidation pattern (`forceCascadeRebuild = true; cascadeReady = false; historyNeedsSeed = true; renderFrameIndex = 0;`) is consistent with the existing invalidation patterns in the codebase (used by dynamic sphere, RenderDoc capture, etc.).
- The setter includes a no-op guard (`if (stripAmbientFloorBake == v) return;`) to avoid redundant rebakes when the toggle state doesn't change.
- Mode numbering 11/12/13 is consistent with the current codebase (modes 0-10 are implemented, leaving 11 as the next free slot). The `setRenderMode` upper bound needs bumping from 11 to 14, which the plan correctly identifies.
- The heatmap palette (green→yellow→red two-segment mix) is identical to modes 5 and 7, maintaining visual consistency.
- The `uSeparateGI` gate at line 552 already has `uRenderMode == 0` guard (Step 10 codex 06 F3 fix), so modes 11/12/13 would bypass the GI-blur path correctly.
- The hoisting of `vec3 indirect = vec3(0.0)` from inside `if (uUseCascade != 0)` to outer scope is correct: it allows the heatmap modes to access `indirect` even when `uUseCascade == 0` (where `indirect` stays `vec3(0.0)` and heatmaps show green = "no GI").
- The CLI flag `--strip-ambient-floor-bake` follows the existing toggle naming pattern.
- The capture list is comprehensive: 9 captures covering the key comparisons (mode 6 with/without strip, mode 9/10/11/12/13).
- The out-of-scope section correctly defers per-mode auto-strip, indirect brightness boost, removing the local `raymarch.frag` 0.05 floor, and heatmap normalization sliders.

## Findings

### 1. Line reference for `forceCascadeRebuild` pattern points to RenderDoc, not canonical example

Severity: Low

The plan's reuse section references `demo3d.cpp:4687` as the "established invalidation pattern (used by Step 8 dynamic-sphere path)." Line 4687 is inside the RenderDoc `TriggerCapture()` preparation block (`rdoc->TriggerCapture()` context), not the Step 8 dynamic-sphere path. The invalidation pattern IS established and used in multiple places, but this specific line reference points to the wrong context. An implementer would see RenderDoc-specific code and might not recognize it as a general pattern.

The actual Step 8 dynamic-sphere invalidation is at line 618/625/638 (inside the sphere overlay logic), which is a better canonical example. Or the plan could reference the setter's own invalidation pattern without pointing to a specific line.

### 2. Heatmap palette line references are shifted from Step 10 changes

Severity: Low

The plan references `raymarch.frag:576-582` for mode 5 and `raymarch.frag:486-495` for mode 7. After Step 10's shader modifications (adding the `uSeparateGI` gate and mode 9/10 branches), the line numbers shifted:

- Mode 5 heatmap is now at lines 588-594 (not 576-582, shifted +12 lines)
- Mode 7 heatmap is now at lines 489-495 (not 486-495, shifted +3 lines)

The palette code is the same, just at different line numbers. The plan's references are stale from the pre-Step-10 state.

### 3. "Early-returns matching mode 4/6/7 pattern" is structurally misleading

Severity: Medium

The plan says "Insert as **early-returns** (matching the mode 4/6/7 pattern)." But modes 4 and 6 compute their own lighting terms BEFORE the main path computes `directColor`/`indirectColor`:

- Mode 4 at line 515: computes `albedo * (diff4 * uLightColor + vec3(0.05))` using its own `diff4` and `lightDir4`, then `return`.
- Mode 6 at line 501: computes `albedo * indirect6` using its own probe sampling, then `return`.

These modes don't need `directColor` or `indirectColor` from the main path. But the heatmap modes (11/12/13) DO need `directColor`, `indirectColor`, and the hoisted `indirect` from the main computation at lines 535-544. So the heatmap modes must be inserted AFTER those computations, which makes them structurally different from modes 4/6 — they consume the main-path results and then early-return with a diagnostic output.

The plan should clarify that the heatmap modes are "early-returns after consuming the main-path lighting computation" rather than "matching the mode 4/6 pattern" which implies self-contained computation. An implementer following the "matching mode 4/6 pattern" guidance might try to insert the heatmap modes alongside modes 4/6 (before `directColor`/`indirectColor` are computed), which would fail because `indirect` wouldn't be available yet.

### 4. No discussion of what mode 6 looks like with strip toggle ON

Severity: Medium

The plan's key diagnostic comparison is `mode6_indirect_strip` vs. `mode6_indirect_only`. But the plan doesn't discuss what mode 6 might look like when the ambient floor is stripped from the bake:

- **Scenario A**: Mode 6 with strip is nearly black → proves the GI bounce signal is entirely driven by the 0.05 ambient floor in source-surface radiance. The cascade math works, but the bounce energy comes from the ambient floor amplification, not from real direct lighting. Fix: remove the 0.05 floor from both the bake and the display.

- **Scenario B**: Mode 6 with strip shows visible colored bounce (brick→floor bleed, column→floor bleed) → proves the GI math produces real direct-lit bounce, but the 0.05 floor adds unwanted energy that washes out the bounce. Fix: remove just the 0.05 floor from the display (or reduce it), keep the bake formula.

The plan should present both scenarios and explain what each outcome implies for the next step. Without this, the verification captures lack a decision framework.

### 5. No discussion of what mode 0 (final composite) looks like with strip toggle ON

Severity: Low

When `stripAmbientFloorBake` is on, mode 0 renders `directColor + indirectColor` where `directColor = albedo * (diff * uLightColor + vec3(0.05))` (still has the local 0.05 floor from `raymarch.frag:535`) and `indirectColor = albedo * indirect_strip` (no 0.05 baked in from source surfaces). The plan doesn't explicitly describe this mixed state: the 0.05 floor still contributes to the direct term but no longer amplifies through bounce. The user needs to know what mode 0 looks like with the strip on to understand whether removing just the bake-floor is sufficient or whether both the local and bake floors need removal.

### 6. No `uUseCascade` dependency guard

Severity: Medium

The `uStripAmbientFloor` toggle only affects the cascade bake shader (`radiance_3d.comp`). When `uUseCascade == 0` (cascades disabled), no cascade bake happens, so the toggle has no effect. The ImGui checkbox should be disabled or grayed out when cascades are off, and the heatmap modes 11/12 should warn (via tooltip or visual indicator) that they require cascade GI to be enabled. The plan doesn't discuss this.

Similarly, the heatmap modes' hoisted `indirect` would be `vec3(0.0)` when `uUseCascade == 0`, so mode 11 would show `length(albedo * vec3(0.0)) / 0.5 = 0.0` (all green) and mode 12 would show `length(vec3(0.0)) / 0.5 = 0.0` (all green). These are technically correct (no GI = no contribution) but would produce useless diagnostic output without any indication to the user that cascades need to be enabled.

### 7. `/0.5` heatmap normalization divisor is not justified

Severity: Low

The plan normalizes the heatmap value by `/0.5` for modes 11 and 12, meaning the heatmap turns yellow at `length(albedo * indirect) = 0.25` and red at `>= 0.5`. The plan acknowledges "hardcoded `/0.5` is fine for diagnostic-only this round" but doesn't reference any existing captures (e.g., mode 3 "indirect * 5" or mode 6 "GI only") to justify whether `0.5` is a reasonable normalization target for Sponza's typical indirect magnitudes.

If typical `length(indirectColor)` values in Sponza are in the 0.01-0.05 range (common for indirect lighting without the ambient floor), the entire scene would appear green with no spatial variation — the same "uniformly lit" problem the diagnostic is trying to solve, but in heatmap form. A divisor of 0.1 or 0.2 would produce more visible spatial variation in the heatmap. The plan should reference mode 3 or mode 6 captures to estimate appropriate normalization, or at least note that the divisor may need adjustment based on the actual output.

### 8. Mode 13 (GI fraction) division-by-zero guard is present but fragile

Severity: Low

The plan's mode 13 code has `(total > 0.001) ? length(indirectColor) / total : 0.0`. The `0.001` threshold prevents division by near-zero. But `total = length(directColor + indirectColor)` could be as small as `0.05 * albedo` when shadowed (diff=0): for Sponza's 0.5 albedo, that's `length(vec3(0.025)) ≈ 0.043`, which is above 0.001. So the threshold is fine for typical Sponza values. But for very dark surfaces (albedo ≈ 0.1), `total ≈ 0.005` would still be above 0.001. The threshold only fails for near-zero albedo, which is unlikely in practice. This is acceptable for diagnostic-only.

### 9. The `uStripAmbientFloor` uniform declaration location is unspecified

Severity: Low

The plan shows the GLSL uniform declaration (`uniform int uStripAmbientFloor;`) and the branch code but doesn't specify where the uniform declaration should go in `radiance_3d.comp`'s uniform block (lines 17-74). The plan says "Add a uniform and branch at the bake site" which covers the functional change, but an implementer needs to know where to insert the uniform declaration. A natural location would be after `uSoftShadowK` (line 61) or after `uUseEnvFill` (line 42), matching the semantic grouping of lighting-related uniforms.

### 10. The `renderFrameIndex = 0` reset triggers all cascades in one frame

Severity: Low

The setter sets `renderFrameIndex = 0`, which resets the cascade stagger pattern. For a 4-cascade setup with stagger, this means all 4 cascade compute dispatches run on the next frame instead of just 1. This is a brief performance spike (4x the cascade workload for one frame) that the plan doesn't mention. It's harmless for a one-time toggle change, but worth noting for diagnostic capture timing.

### 11. Toggle persistence across scene changes is not discussed

Severity: Low

The `stripAmbientFloorBake` toggle is a global setting on `Demo3D`. If the user enables the strip and then loads a new scene (via `--switch-to-scene` or ImGui scene picker), the toggle persists and the new scene's cascades would be baked with the strip active. The plan doesn't discuss whether this is intended or whether the toggle should reset on scene change. For diagnostic use, persisting the toggle is probably fine — but it could surprise a user who forgot they enabled the strip.

## Verification Gaps To Add

- Capture mode 0 (final composite) with `--strip-ambient-floor-bake` to document what the mixed state looks like: direct still has 0.05, indirect no longer has 0.05 baked in.
- Capture mode 6 with strip and compare against mode 6 without strip. Document both possible outcomes (nearly black = bounce entirely from ambient floor; visible colored bleed = bounce from real direct light). Explain what each outcome implies for the next fix.
- Reference existing mode 3 ("indirect * 5") captures to estimate the typical `length(indirectColor)` range and validate the `/0.5` heatmap normalization divisor.
- Gray out the "Strip 0.05 ambient floor from GI bake" checkbox when `useCascade == false`.
- Add tooltips to heatmap mode 11/12 RadioButtons noting that they require cascade GI to be enabled.
- Verify that `uStripAmbientFloor` uniform location is found (not -1) after adding it to `radiance_3d.comp` and binding it in `demo3d.cpp`. (This mirrors codex 04 F6's finding about uniform validation in the GPU voxelizer.)
- Test the `renderFrameIndex = 0` reset impact: measure the cascade dispatch time on the rebake frame vs. a normal stagger frame to confirm the performance spike is acceptable.