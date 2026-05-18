# Critic Review 13 - perf_tooling_step12_impl.md

Reviewed: 2026-05-11T18:00:50+08:00

Target: `doc/5/claude_plan/perf_tooling_step12_impl.md`

## Verdict

Step 12 is a genuine landing with all claimed CLI flags present in the current source and functional. The `--window-size` parse-before-init pattern correctly avoids the stale-dims trap identified in codex 11 F2+F5. The 3 scaling-experiment setters exist with proper invalidation chains. The `setCascadeC0Res` setter correctly applies the 4-line lighting-change pattern (no `meshSDFReady = false`) consistent with the codex 12 F8 partial-reject decision and the codex 08 F1+F7 lesson. The 22-capture pipeline and per-pass timing extraction confirm the implementation is functional end-to-end.

The document has one moderate factual error (FOV-fit math line reference off by ~95 lines, propagated from the 1080p perf analysis plan), one internal contradiction ("RenderDoc covers it, no per-pass instrumentation needed" at line 31 vs. "single-shot captures have ±2-5× variance" at line 230 — if RenderDoc is noisy, the deferred `cascadeTimeMs` stdout cross-check becomes more urgent, not less), one unflagged consistency gap (ImGui handler does 3-line invalidation for the same `cascadeC0Res` trigger while the CLI setter does 6 lines — same trigger, different invalidation depth), and one recommendation gap (the "what's next" section presents two paths neutrally but the document's own evidence argues for path 2 first, since Exp 3+4 produced unusable data and any optimization before/after comparison will hit the same noise floor).

## Evidence Checked

- `doc/5/claude_plan/perf_tooling_step12_impl.md`.
- Current `src/main3d.cpp`: `--window-size` pre-init parse at lines 143-159, `initializeApplication(int, int)` declaration at line 59 and implementation at lines 462-523 (InitWindow uses parameters, not hardcoded defaults), `--cascade-c0-res` at lines 247-257, `--raymarch-steps` at lines 258-267, `--gi-blur-radius` at lines 268-277. All 3 flags use `std::atoi` with explicit `std::cerr` error logging, matching the Step 10 `--camera-pos` precedent.
- Current `src/demo3d.h`: `setCascadeC0Res(int v)` at line 532 (out-of-line, comment "reallocates cascade textures"), `setRaymarchSteps(int v)` inline at lines 533-536, `setGIBlurRadius(int v)` inline at lines 537-543 (uses manual `if`-clamp, not `std::clamp`).
- Current `src/demo3d.cpp`: `setCascadeC0Res` implementation at line 5216 (6-line invalidation: `cascadeReady=false`, `forceCascadeRebuild=true`, `renderFrameIndex=0`, `historyNeedsSeed=true`, plus `destroyCascades()+initCascades()`), ImGui handler for `cascadeC0Res` at lines 792-801 (3-line invalidation: `destroyCascades()+initCascades()+cascadeReady=false` — no `forceCascadeRebuild`, `renderFrameIndex`, or `historyNeedsSeed`), FOV-fit math at lines 5080-5098 (inside `applyOBJViewPreset()`, NOT at line 4993 which is the closing brace of `launchSequenceAnalysis()`), `cascadeTimeMs` ImGui display at line 3654.
- `doc/5/claude_plan/gi_pass_1080p_perf_analysis_plan.md`: same incorrect FOV-fit line reference (`:4993`).
- `doc/5/claude_plan/gi_pass_scaling_experiment_plan.md`: setter snippets show `std::clamp(v, 1, 8)` for `setGIBlurRadius` — actual implementation uses `if (v < 1) v = 1; if (v > 8) v = 8;`. Functionally identical.
- `doc/5/claude_plan/perf/gi_pass_1080p_perf_analysis.md`: Config B anomaly (720p = 341.7 ms, 4× higher than 1080p = 82.8 ms) attributed to GPU power-state variance.
- `doc/5/claude_plan/perf/gi_pass_scaling_experiment.md`: Exp 2 cleanest signal (raymarch flat at 1.97 ms across all 6 probe-res values), Exp 3+4 defeated by variance.

## What Looks Good

- The parse-before-init pattern is correct and well-justified. The codex 11 F2+F5 catch identified that `SetWindowSize()` after `InitWindow()` would leave `Demo3D` constructed with stale 720p dims, producing wrong FOV-fit math + viewport size mismatch. The fix (parse dims before init, pass directly to `InitWindow`) avoids this silently. No `SetWindowSize` call exists anywhere in the codebase — the simpler approach is correct.
- The `setCascadeC0Res` setter correctly omits `meshSDFReady = false`, applying the codex 12 F8 partial-reject decision and the codex 08 F1+F7 lesson (probe-res changes the cascade probe atlases, not the SDF voxel grid). This is consistent with the 4-line lighting-change pattern established in Step 11's `setStripAmbientFloorBake` debate (codex 08 finding 1). The setter's log message `"cascade reallocated; SDF unchanged"` makes this explicit in runtime output.
- The `setRaymarchSteps` and `setGIBlurRadius` setters are correctly inline in the header since they're uniform-only — no GPU resource changes needed. The `std::cout` logging in each setter matches the Step 10/11 setter precedent and provides headless-mode visibility.
- The variance admission (±2-5× for single-shot RenderDoc captures, Exp 3+4 defeated by noise) is honest and empirically grounded. The specific note that Exp 1+2 succeeded because workload differences (64× pixel count or 267× probe count) were large enough to overcome the noise, while Exp 3+4 failed because their workload differences were too small, is a useful calibration for future measurement design.
- The Exp 2 cleanest-signal callout (raymarch flat at 1.97 ms across all probe-res values, proving raymarch is NOT cascade-bound) is the right empirical highlight — this single dataset point disproves a whole class of potential optimizations (reducing raymarch step count per-cascade) and focuses attention on window-bound optimizations.
- The codex findings tables (11 for codex 11, 8 for codex 12) are comprehensive and correctly track which findings resulted in code changes (F2+F5 → parse-before-init, F2 → setters added, F8 → partial reject on `meshSDFReady`).
- The 4-line vs 5-line invalidation-chain distinction (geometry-change vs lighting-change) is now documented as a "recognized codebase pattern," which helps future implementers avoid blindly copying the 5-line pattern for lighting-only changes (the bug codex 08 F1 caught in Step 11).

## Findings

### 1. FOV-fit math line reference is wrong

Severity: Medium

The document references "FOV-fit math at demo3d.cpp:4993" at line 42. Line 4993 is the closing brace `}` of `launchSequenceAnalysis()`. The actual FOV-fit math (using `GetScreenWidth/Height` + `tan(fovy)` for camera distance fitting) is at lines 5080-5098 inside `applyOBJViewPreset()`.

This error propagates from the 1080p perf analysis plan (`gi_pass_1080p_perf_analysis_plan.md`), which also references `:4993`. The concern is valid — that code does use `GetScreenWidth/Height` and would produce wrong results with stale dims — but the reference points to the wrong function entirely. An implementer reading the doc alongside source would look at line 4993 and find nothing relevant.

The correct reference should be `demo3d.cpp:5080-5098` or simply `demo3d.cpp:5083-5099` (the core viewport/FOV lines).

### 2. Internal contradiction: "no per-pass instrumentation needed" vs "±2-5× variance"

Severity: Medium

Line 31 claims "no per-pass timing instrumentation needed (RenderDoc covers it via existing `glPushDebugGroup` labels)." Line 230 admits "Single-shot RenderDoc captures have ±2-5× variance" and that Exp 3+4 were defeated by this noise. The Config B anomaly in the perf analysis report (720p = 341.7 ms, 4× higher than 1080p) shows variance can flip the direction of results, not just inflate magnitude.

If RenderDoc single-shot captures have ±2-5× variance that can make 720p appear slower than 1080p, then RenderDoc does NOT "cover it" for any measurement where the expected difference is smaller than the noise floor. The `cascadeTimeMs` stdout cross-check (deferred in codex 11 F6 and listed as an "open item" here) would provide a cheap CPU-side sanity check that's independent of RenderDoc's GPU timing variance. Deferring it because "RenderDoc covers it" is inconsistent with the document's own evidence that RenderDoc does NOT cover small-difference measurements.

The document should either:
- Acknowledge the contradiction and escalate `cascadeTimeMs` stdout logging from "open item" to "recommended before any before/after optimization measurement," or
- Add a caveat to line 31: "no per-pass instrumentation needed for large-difference measurements (Exp 1+2 class); small-difference measurements (Exp 3+4 class) need N-capture averaging or GPU clock locking."

### 3. ImGui vs CLI invalidation inconsistency not flagged

Severity: Medium

The ImGui handler for `cascadeC0Res` changes (lines 792-801) does 3 lines of invalidation:
```
destroyCascades(); initCascades(); cascadeReady = false;
```

The CLI setter `setCascadeC0Res` (line 5216) does 6 lines of invalidation:
```
destroyCascades(); initCascades(); cascadeReady = false;
forceCascadeRebuild = true; renderFrameIndex = 0; historyNeedsSeed = true;
```

Both trigger from the same event (cascadeC0Res change) but the CLI path is more thorough. The ImGui path lacks `forceCascadeRebuild`, `renderFrameIndex`, and `historyNeedsSeed` — meaning an interactive user changing probe-res via the dropdown gets a different invalidation sequence than a headless CLI user. The missing `forceCascadeRebuild` means the ImGui path relies on the stagger cadence to eventually dispatch all 4 cascades, while the CLI path forces all 4 on the next frame. The missing `historyNeedsSeed` means the ImGui path may blend with stale zeroed EMA history for a few frames.

The document correctly describes the CLI setter's 6-line chain but doesn't note the ImGui handler only has 3 lines. This is a consistency gap that should be either:
- Fixed by upgrading the ImGui handler to match the CLI setter's 6-line pattern (the codex 08 extras should apply to both paths), or
- Documented as intentional (the ImGui path gets eventual convergence through the stagger cadence; the CLI path needs immediate convergence because it's followed by a RenderDoc capture at +8 seconds).

### 4. "What's next" section should recommend path 2 first

Severity: Low

The document presents two paths neutrally: (1) implement Tier 1 optimizations, (2) add tooling improvements (stdout `cascadeTimeMs` + N-capture averaging) to defeat variance. But the document's own evidence argues strongly for path 2:

- Exp 3 (raymarch step count) and Exp 4 (GI blur radius) produced unusable data — "wild capture-to-capture variance that swamped the actual workload differences."
- The Config B anomaly shows variance can flip results by 4× in the wrong direction.
- Any Tier 1 optimization before/after comparison will measure differences smaller than Exp 3+4's workload changes (step-cap from 256→128 is a 2× reduction; the variance is ±2-5×).
- The document notes "for precise scaling slopes, need GPU clock locking or N-capture averaging" — but then doesn't recommend implementing either before attempting optimization measurements.

Implementing Tier 1 optimizations without N-capture averaging would produce untrustworthy before/after comparisons. The document should recommend path 2 first (defeat variance, then measure optimizations with confidence), or at minimum flag that path 1 without path 2's tooling will produce measurements with ±2-5× uncertainty bars.

### 5. "No new GPU resources" claim is imprecise

Severity: Low

Line 29 claims "no new GPU resources." This is correct for the CLI flags themselves (no new textures, buffers, or shaders were added). But `setCascadeC0Res` calls `destroyCascades() + initCascades()`, which deallocates and reallocates all 4 cascades' atlas, grid, and history textures at the new probe-res dimensions. This is not "zero GPU impact" — it's a runtime reallocation of existing GPU resources.

The distinction between "no new permanent GPU resources added to the codebase" vs "existing resources may be reallocated at runtime via the new setter" is worth making explicit. A reader might interpret "no new GPU resources" as "calling `--cascade-c0-res=16` has zero GPU memory impact," which is wrong — it changes the memory footprint of the cascade probe atlases (fewer probes = smaller atlases).

### 6. sscanf vs atoi inconsistency not discussed

Severity: Low

The `--window-size` flag uses `std::sscanf` for parsing (two integers from a comma-separated string). The `--cascade-c0-res`, `--raymarch-steps`, and `--gi-blur-radius` flags use `std::atoi` for parsing (single integers). Both are flagged by MSVC's deprecation warnings (`_CRT_SECURE_NO_WARNINGS` needed to silence). The document dismisses these as "not worth the noise" and notes they're consistent with Step 10's precedent.

But Step 10's `--camera-pos` uses `std::sscanf` (three floats from comma-separated), so the document claims the 3 new flags "match the Step 10 `--camera-pos` precedent." They don't — `--camera-pos` uses sscanf, the 3 new flags use atoi. The inconsistency isn't harmful (atoi is simpler for single-int parsing and sscanf is needed for multi-value parsing), but the document's claim of "matching" the precedent is slightly misleading. A note like "atoi for single-value flags (simpler than sscanf for one int), sscanf for multi-value flags (matching `--window-size` and `--camera-pos`)" would be more accurate.

### 7. `std::clamp` vs manual `if`-clamp discrepancy in `setGIBlurRadius`

Severity: Low

The scaling experiment plan (codex 12) shows `setGIBlurRadius` using `std::clamp(v, 1, 8)`. The step12 implementation note's code snippet shows manual `if (v < 1) v = 1; if (v > 8) v = 8;`. The actual source code matches the implementation note (manual `if`-clamp). Functionally identical, but the implementation note doesn't acknowledge the style change from the plan, and neither document discusses why `std::clamp` was avoided (likely to avoid `<algorithm>` header dependency in the inline header setter, but this isn't stated).

This is cosmetic — both approaches produce the same clamped value. But the implementation note should mention the deviation from the plan, or the plan should have used the manual `if`-clamp style from the start.

### 8. Config B anomaly severity understated

Severity: Low

The 1080p perf analysis report documents a Config B anomaly: 720p measured 341.7 ms (4× higher than the 1080p measurement of 82.8 ms). This is attributed to GPU power-state variance (P5 idle vs P0 turbo). The step12 document references this indirectly in the "±2-5× variance" section, but frames it as magnitude noise: "single-shot captures have ±2-5× variance."

The Config B anomaly is more severe than magnitude noise — it flips the direction of the result. A lower-resolution measurement appeared 4× slower than a higher-resolution measurement. This means variance can produce anti-correlated results, not just inflated ones. For the "what's next" discussion, this should be explicitly highlighted: "GPU power-state variance can make lower-resolution workloads appear slower than higher-resolution ones (Config B: 720p 4× slower than 1080p). Without variance control, before/after optimization comparisons may show the 'optimized' version as slower than the baseline."

## Verification Gaps To Add

- Fix the FOV-fit math line reference from `:4993` to `:5080-5098` (or `:5083-5099` for the core lines). Verify that `applyOBJViewPreset()` is the correct function name and that `GetScreenWidth/Height` at lines 5083-5084 are the calls that would produce stale dims if `SetWindowSize` had been used.
- Upgrade the ImGui handler at `demo3d.cpp:792-801` to match the CLI setter's 6-line invalidation pattern, or document the inconsistency as intentional with a rationale (e.g., "ImGui path relies on stagger cadence for convergence; CLI path needs immediate convergence for RenderDoc capture").
- Acknowledge the "RenderDoc covers it" contradiction: either add a caveat distinguishing large-difference vs small-difference measurements, or escalate `cascadeTimeMs` stdout logging from "open item" to "required before optimization measurements."
- Recommend path 2 (tooling/variance control) before path 1 (optimizations) in the "what's next" section, with explicit reasoning: any optimization before/after comparison with ±2-5× variance bars is untrustworthy.
- Clarify "no new GPU resources" to "no new permanent GPU resources in the codebase; `setCascadeC0Res` triggers runtime reallocation of existing cascade textures at different dimensions."
- Note the sscanf vs atoi inconsistency explicitly: `--window-size` and `--camera-pos` use sscanf (multi-value), the 3 new flags use atoi (single-value). Both trigger the same MSVC deprecation nags. Not a bug, but the "matching Step 10 precedent" claim is imprecise.
- Explicitly flag the Config B anomaly's direction-flipping severity (720p 4× slower than 1080p), not just magnitude noise, in the variance discussion.