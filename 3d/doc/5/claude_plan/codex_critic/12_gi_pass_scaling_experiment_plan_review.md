# Critic Review 12 - gi_pass_scaling_experiment_plan.md

Reviewed: 2026-05-11T14:30:44+08:00

Target: `doc/5/claude_plan/gi_pass_scaling_experiment_plan.md`

## Verdict

The plan is a well-designed empirical verification of the window-bound vs volume-bound classification from Step 12's perf analysis. The four-experiment matrix (window scaling, probe-res scaling, raymarch steps, blur radius) cleanly isolates each scaling axis. The shader bottleneck research (Phase 4) adds valuable depth beyond just timing numbers. The experimental approach is sound: log-log slope analysis to distinguish linear vs cubic vs constant scaling.

The plan has several issues: `volumeResolution` is NOT compile-time (it's a runtime member initialized from a constexpr default, but can be changed at runtime — the plan incorrectly claims it's compile-time only), no public setters exist for any of the 3 proposed CLI knobs (`setCascadeC0Res`, `setRaymarchSteps`, `setGIBlurRadius` are all absent from the header), changing `cascadeC0Res` triggers a full `destroyCascades + initCascades` cycle which is expensive and would add ~1-2 seconds of cascade re-allocation to each probe-res scaling data point (potentially introducing variance), the `giBlurRadius` ImGui range is 1-8 but the plan's Experiment 4 uses values 1-4 which are within range, and the plan references Step 12 and "codex 11" but no codex 11 review exists in the critic directory (only 01-10).

## Evidence Checked

- `doc/5/claude_plan/gi_pass_scaling_experiment_plan.md`.
- Current `src/demo3d.h`: `cascadeC0Res` at line 947 (member, no setter), `raymarchSteps` at line 1187 (member, no setter), `giBlurRadius` at line 1245 (member, no setter), `volumeResolution` at line 922 (member, initialized from `DEFAULT_VOLUME_RESOLUTION = 128` at line 52 — runtime member, NOT compile-time), `DEFAULT_VOLUME_RESOLUTION` at line 52 (constexpr default, but the member itself is runtime mutable).
- Current `src/demo3d.cpp`: `cascadeC0Res` ImGui combo at lines 3776-3791 (`kC0Options = {8,16,24,32,48,64}`), `cascadeC0Res` change triggers `destroyCascades() + initCascades() + cascadeReady=false` at lines 794-798, `raymarchSteps` initialized to 256 (line 225), passed as `uSteps` uniform (line 2333), `giBlurRadius` initialized to 8 (line 279), passed as `uBlurRadius` uniform (line 2518), ImGui slider range 1-8 (line 3635).
- `res/shaders/gi_blur.frag`: exists on disk.
- `res/shaders/temporal_blend.comp`: exists on disk.
- `res/shaders/radiance_3d.comp`, `reduction_3d.comp`, `sdf_analytic.comp`: all exist on disk.
- `tools/rdoc_extract.py`: exists on disk.

## What Looks Good

- The four-experiment matrix is well-designed: each experiment varies exactly one axis while fixing all others. This cleanly isolates each scaling factor and avoids confounding variables.
- The window scaling experiment (320×180 → 2560×1440, 5 data points) spans a wide range (0.03× to 2× of 1080p pixel count), providing good log-log slope discrimination.
- The probe-res experiment at 320×180 (minimum window) eliminates fragment-bound noise from cascade measurements. This is a clever choice — at 320×180, raymarch + blur cost is negligible, so cascade timing dominates and is easier to measure precisely.
- The shader bottleneck research (Phase 4) goes beyond just timing numbers. Identifying the inner loop, per-thread cost, and scaling driver for each pass is essential for directing optimization work.
- The honest expectation note (lines 236-241) correctly acknowledges that some passes won't fit cleanly into binary window-bound/volume-bound classification. GI blur has FBO copy overhead, reduction has a D² inner loop on top of probe³ dispatch.
- The 3.75% divergence threshold for classification verification (lines 126-128) is a reasonable tolerance for GPU timing noise.
- The total 20 captures at ~5 minutes runtime is practical.
- The `cascadeC0Res` options `{8,16,24,32,48,64}` match the existing ImGui dropdown exactly, so Experiment 2's data points are all valid values.

## Findings

### 1. `volumeResolution` is NOT compile-time — it's a runtime member

Severity: Medium

The plan says "Volume resolution scaling deferred: `volumeResolution=128` is compile-time only ([demo3d.cpp:168](../../../src/demo3d.cpp#L168))". But `volumeResolution` is a runtime member at `demo3d.h:922`, initialized from a constexpr default `DEFAULT_VOLUME_RESOLUTION = 128` at `demo3d.h:52`. The line the plan references (`demo3d.cpp:168`) is just the member initializer `volumeResolution(DEFAULT_VOLUME_RESOLUTION)` in the constructor — this initializes the runtime member from a constexpr, not a compile-time constraint.

`volumeResolution` is used extensively as a runtime variable (lines 600, 638, 1511, 1702, 1818, 1996, 2246, 2294, etc.). It could theoretically be changed at runtime if the texture reallocation infrastructure were added (same pattern as `cascadeC0Res`'s `destroyCascades + initCascades` approach).

This doesn't invalidate the plan's decision to defer volume-resolution scaling — the reallocation cost would indeed be significant. But the rationale is wrong: volumeResolution is NOT "compile-time only." The correct rationale is "runtime-changeable but would require full texture/SDF reallocation, which is a non-trivial infrastructure addition for this experiment."

### 2. No public setters exist for the 3 proposed CLI knobs

Severity: Medium

The plan says to "Apply via existing public setters on `Demo3D` (or add minimal setters where missing)." But NONE of the three setters exist:
- `setCascadeC0Res(int)` — NOT in `demo3d.h`. The ImGui combo directly writes `cascadeC0Res = kC0Options[curIdx]` at line 3791, which triggers the `destroyCascades + initCascades` invalidation check at lines 793-801.
- `setRaymarchSteps(int)` — NOT in `demo3d.h`. The member is directly editable via ImGui (no ImGui widget visible for it in the search results, but the uniform is at line 2333).
- `setGIBlurRadius(int)` — NOT in `demo3d.h`. The ImGui slider at line 3635 directly writes `&giBlurRadius`.

For all three, the plan needs to either:
- Add public setters that include the necessary invalidation logic (e.g., `setCascadeC0Res` needs `destroyCascades + initCascades + cascadeReady=false`),
- Or directly set the member and manually trigger the invalidation from `main3d.cpp` after the CLI parse.

The second approach is more fragile — it duplicates the invalidation pattern and risks missing steps. The first approach (proper setters) is cleaner and follows the Step 10/11 setter pattern (e.g., `setStripAmbientFloorBake`).

### 3. Changing `cascadeC0Res` triggers full cascade re-allocation (~1-2 s)

Severity: Low

At lines 794-798, changing `cascadeC0Res` triggers `destroyCascades(); initCascades(); cascadeReady=false;`. This reallocates all 4 cascades' atlas, grid, and history textures (plus the EMA state). For Experiment 2 (6 data points at 8/16/24/32/48/64), each data point would incur a ~1-2 second reallocation overhead before the RenderDoc capture fires at +8 seconds warmup.

This doesn't affect the GPU timing measurement (RenderDoc captures after warmup), but it means:
- The 8-second warmup must account for reallocation time. If the reallocation takes ~2 s and occurs during warmup, the cascades will have re-converged by the time RenderDoc captures.
- The experiment takes longer than the estimated ~5 minutes (6 probe-res changes × ~12-15 s warmup + ~2 s reallocation = ~100 s total for Experiment 2 alone).

The plan's "~12-15 s per capture" estimate should include this re-allocation cost.

### 4. `giBlurRadius` default is 8, but Experiment 4 tests values 1-4

Severity: Low

The plan's `--gi-blur-radius=N` default is 1 (from the flag table). But the actual member default is `giBlurRadius = 8` (demo3d.cpp line 279). The ImGui slider range is 1-8 (line 3635). The plan's Experiment 4 tests values 1, 2, 4, 8 — the last value (8) matches the current default, but the flag's stated default of 1 is inconsistent with the actual runtime default.

This creates confusion: if the user runs without `--gi-blur-radius`, they'd get 8 (the member default), not 1 (the plan's stated flag default). The plan should clarify: the CLI flag DEFAULT should match the member default (8), or the member default should be changed to 1. The flag table should say "Default: 8 (matches runtime)" or "Default: (existing default, 8)".

### 5. Experiment 2 uses 320×180 window — but `--window-size` flag hasn't been implemented yet

Severity: Low

Experiment 2 specifies a 320×180 window. This requires the `--window-size` flag from the Step 12 perf plan (codex 11), which hasn't been implemented yet. The same dependency issue as codex 11's Config C — the flag must exist before experiments can run. This is obvious but should be noted as a prerequisite.

### 6. Plan references "Step 12" and "codex 11" but no codex 11 review exists

Severity: Low

The plan says "Step 12 perf analysis classified each pass" and references the 1080p perf analysis from a prior step. But no codex 11 review exists in the `codex_critic` directory (reviews 01-10 only). The plan references "codex 11" implicitly (the perf analysis plan was reviewed as codex 11). This is a numbering gap — the critic reviews have covered plans/impls for Steps 8-11 plus the follow-ups, but the Step 12 perf plan was reviewed as codex 11 and this scaling experiment is the next step after that.

### 7. `raymarchSteps` default is 256, not documented in the flag table

Severity: Low

The plan's flag table says `--raymarch-steps=N` with default "(existing default)". The actual default is `raymarchSteps = 256` (demo3d.cpp line 225). The plan should specify "Default: 256" for clarity, matching the member initialization. Experiment 3 tests values 32/64/128/256/512 — 256 is the existing default, and 512 would be 2× beyond the current limit. The plan doesn't discuss whether `raymarchSteps` has any upper bound or whether setting it to 512 would cause issues (e.g., very slow captures at 512 steps × 1080p pixels).

### 8. `setCascadeC0Res` setter needs full invalidation — not just `cascadeReady = false`

Severity: Medium

If a `setCascadeC0Res` setter is added, it must replicate the full invalidation chain from the ImGui handler at lines 793-801:
1. `destroyCascades()` — frees all 4 cascades' GPU textures
2. `initCascades()` — reallocates with new probe-res dimensions
3. `cascadeReady = false` — forces cascade rebuild on next frame
4. Possibly also: `forceCascadeRebuild = true`, `renderFrameIndex = 0`, `historyNeedsSeed = true`

The plan's Phase 1 says "just need to extract the assignment + invalidation pattern into the public setters" but doesn't specify the exact invalidation chain. A setter that only does `cascadeC0Res = v; cascadeReady = false;` would leave stale GPU textures allocated at the old probe-res dimensions — the next cascade bake would write into textures sized for the old probe-res, producing corrupted output or GL errors.

The correct setter must:
```cpp
void setCascadeC0Res(int v) {
    if (cascadeC0Res == v) return;
    cascadeC0Res = v;
    destroyCascades();
    initCascades();
    cascadeReady        = false;
    forceCascadeRebuild = true;
    renderFrameIndex    = 0;
    historyNeedsSeed    = true;
    // Also invalidate SDF since probe grid dimensions affect JFA boundaries
    meshSDFReady        = false;
}
```

Note: `meshSDFReady = false` may be needed here because the cascade probe grid dimensions affect the SDF boundary setup. This is a geometry change (probe grid volume changes size), unlike the strip toggle which was a lighting-only change (codex 08 F1).

## Verification Gaps To Add

- Add explicit `setCascadeC0Res`, `setRaymarchSteps`, `setGIBlurRadius` public setters to `demo3d.h` with full invalidation chains. Do NOT just set the member directly from `main3d.cpp`.
- The `setCascadeC0Res` setter MUST call `destroyCascades() + initCascades()` plus the full 5-line geometry-invalidation pattern (not just the 4-line lighting pattern from codex 08 F1).
- Specify the actual defaults for all 3 flags: `cascadeC0Res = 32`, `raymarchSteps = 256`, `giBlurRadius = 8`. The flag table should say "Default: 32/256/8 (matches runtime)" not "Default: 32/(existing)/1".
- Add a warmup overhead estimate for probe-res changes: each `cascadeC0Res` change costs ~1-2 seconds for destroy+init. The total experiment runtime should include this overhead.
- Verify that `raymarchSteps = 512` doesn't cause unacceptable capture times at 1080p. Estimate: 512 steps × ~2M pixels × ~3 texture fetches/step = massive wall time. Consider whether 512 is practical for RenderDoc capture or should be capped at 256.
- After implementing the CLI flags, smoke-test `--cascade-c0-res=16` and confirm the log shows `16^3` probe grid AND the `destroyCascades + initCascades` cascade reallocation happens correctly.