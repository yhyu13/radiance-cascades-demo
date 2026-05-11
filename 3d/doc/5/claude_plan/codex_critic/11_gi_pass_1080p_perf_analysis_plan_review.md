# Critic Review 11 - gi_pass_1080p_perf_analysis_plan.md

Reviewed: 2026-05-11T13:22:08+08:00

Target: `doc/5/claude_plan/gi_pass_1080p_perf_analysis_plan.md`

## Verdict

The plan is well-structured as a measurement-only step before any optimization work. The `--window-size=WxH` CLI flag addition is a minimal, correct approach. The three-config capture strategy (720p re-cap, 1080p forced, 1080p visual sanity-check) is reasonable. The existing RenderDoc infrastructure (`rdoc_extract.py`, `glPushDebugGroup` labels, `auto-rdoc` with forced cascade rebuild) is confirmed and matches the plan's claims. The stagger derivation from forced-cascade numbers is mathematically sound.

The plan has several issues: a wrong line reference for `rdocForceRebuildCount` (says `2721-area` but it's at line 4721), `SetWindowSize()` is not used anywhere in the current codebase (raylib's `InitWindow` sets the size at startup; resizing after init requires additional handling), the 1080p raymarch scaling estimate of `~2.25×` assumes linear pixel-count scaling but doesn't account for the march loop's variable step count which may not scale linearly with resolution, the `--window-size` flag needs to also update `Demo3D`'s viewport/cascade parameters (the viewport, aspect ratio, and `screenWidth`/`screenHeight` must be consistent), and Config C (1080p headless 600 frames) won't actually run at 1080p without the `--window-size` flag that hasn't been implemented yet — the plan describes C before implementing the infrastructure it depends on.

## Evidence Checked

- `doc/5/claude_plan/gi_pass_1080p_perf_analysis_plan.md`.
- Current `src/main3d.cpp`: `DEFAULT_WIDTH=1280, DEFAULT_HEIGHT=720` at lines 31-34, `InitWindow` at line 430, `GetScreenWidth/Height` at lines 318-319/370-372/490-491, no `SetWindowSize` usage, no `--window-size` flag. CLI parser at lines 167-232.
- Current `src/demo3d.cpp`: `rdocForceRebuildCount=2` at line 4721 (not 2721), `glPushDebugGroup` labels at 10 locations (GPU JFA SDF, GPU triangle voxelize, sdf_analytic, radiance_3d, reduction_3d, temporal_blend ×2, raymarch, gi_blur), cascade timing at lines 885/987.
- Current `tools/rdoc_extract.py`: exists on disk.
- Current `tools/analyze_renderdoc.py`: exists on disk.
- Current `tools/analysis/rdoc_frame_frame401_pipeline.md`: exists with per-pass µs table matching the plan's quoted numbers.
- `tools/captures/rdoc_frame_frame401.rdc`: exists from previous capture.

## What Looks Good

- The plan is purely a measurement step — no optimization code changes. This is the correct approach: measure first, optimize later.
- The `--window-size=WxH` CLI flag is minimal and preserves the default (1280×720). Existing captures stay reproducible.
- The three-config capture strategy covers the key scenarios: 720p re-cap for regression, 1080p forced for worst-case baseline, 1080p visual for sanity.
- The stagger derivation (`C0 + C1/2 + C2/4 + C3/8`) is mathematically correct for the described cadence pattern.
- The existing RenderDoc infrastructure is fully verified: `rdoc_extract.py` exists, `glPushDebugGroup` labels exist at 10 dispatch sites, `auto-rdoc` forces all cascades via `rdocForceRebuildCount=2`, and the frame 401 pipeline analysis exists with matching per-pass µs numbers.
- The plan correctly identifies that cascade work is volume-bound (not window-bound) while raymarch + GI blur are fragment-bound. This determines which passes scale with resolution.
- The 1 ms budget gap assessment is honest: ~29× over budget at 1080p staggered, requiring fundamental restructuring rather than just tuning.
- The out-of-scope section correctly defers all optimization work, in-app GPU timers, and default resolution change.

## Findings

### 1. Line reference `2721-area` for `rdocForceRebuildCount` is wrong

Severity: Low

The plan references `rdocForceRebuildCount` at `[demo3d.cpp:2721-area](../../../src/demo3d.cpp#L2721)`. The actual `rdocForceRebuildCount=2` assignment is at line 4721, not 2721. The `forceCascadeRebuild` / `renderFrameIndex=0` sustain logic is at lines 869-875. The line reference is off by ~2000 lines, likely referencing an older version of the source before Steps 8-11 additions shifted line numbers.

### 2. `SetWindowSize()` is not used in the current codebase — window size handling needs more work than the plan describes

Severity: Medium

The plan says "apply via `SetWindowSize()` (raylib) right after `InitWindow`". But `SetWindowSize` is not called anywhere in `main3d.cpp` currently. `InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, ...)` at line 430 sets the initial size, and `GetScreenWidth()/GetScreenHeight()` at lines 318-319 and 370-372 are used for resize detection.

There are several concerns with using `SetWindowSize()` after `InitWindow`:

1. **Resize ordering**: `InitWindow` creates the window at 1280×720. If `--window-size=1920,1080` is specified, `SetWindowSize` would resize after construction. But `Demo3D` is constructed at line 490 area, which reads `GetScreenWidth()/GetScreenHeight()`. The resize needs to happen BEFORE `Demo3D` construction so the initial viewport/cascade setup uses the correct dimensions.

2. **`Demo3D` viewport dependency**: `Demo3D::applyOBJViewPreset()` computes FOV-fit distance using `GetScreenWidth()/GetScreenHeight()` and `camera.fovy` (line 4993-4998). If the window is resized after `Demo3D` init but before these computations, the camera preset would use stale dimensions.

3. **Cascade aspect ratio**: The radiance cascade setup may depend on the screen aspect ratio for probe grid dimensions or blur kernel sizing. If the resize happens after cascade setup, the blur pass may use stale dimensions.

The plan should specify the exact insertion order: parse `--window-size` → `InitWindow` with the specified dimensions (not `DEFAULT_WIDTH/HEIGHT`) → `glewInit` → `Demo3D` construction. This avoids the resize-after-init ordering problem entirely by setting the correct size at `InitWindow` time rather than resizing afterward.

### 3. 1080p raymarch scaling estimate `~2.25×` may not account for non-linear march behavior

Severity: Medium

The plan estimates 1080p raymarch at `~5916.7 µs × 2.25 ≈ 13300 µs`, based on pixel count scaling (1920×1080 = 2,073,600 pixels vs 1280×720 = 921,600 pixels = 2.25×). But raymarching has variable step counts per pixel — shadowed or complex-surface pixels take more steps than empty-space pixels. At higher resolution, the same geometry fills more pixels with surface-adjacent steps (higher step density) relative to empty-space early-termination pixels. This means the scaling may be slightly higher than 2.25× (more pixels hitting surfaces = more total march steps).

The estimate is reasonable as a first approximation, but the plan should note this uncertainty and flag it as something the actual measurement will confirm or correct.

### 4. Config C (1080p headless) depends on the `--window-size` flag that hasn't been implemented yet

Severity: Low

The plan describes Config C as a headless `--exit-frames=600` capture at 1080p. But this requires `--window-size=1920,1080` to actually set the window to 1080p. The flag is proposed in Phase 1 but hasn't been implemented. Config C can't be run until Phase 1 is complete. The plan should note that Phase 1 must land before Phase 2 captures, or at least specify the build step as prerequisite.

This is obvious (you need the flag to use it) but the plan's phase ordering implies Phase 1 is trivial (~15 lines) and Phase 2 follows immediately. If the `SetWindowSize`/`InitWindow` integration has more complexity than expected (see finding 2), Phase 1 could take longer than planned.

### 5. The plan doesn't discuss Demo3D's viewport update on resize

Severity: Medium

When the window is resized (whether via `SetWindowSize` or `InitWindow` with different dimensions), `Demo3D` needs to update its viewport and aspect-dependent parameters. Currently `main3d.cpp` checks for resize at lines 370-372 and updates `screenWidth/screenHeight`. But `Demo3D` also needs to:

- Update the viewport (`glViewport(0, 0, width, height)` at line 492)
- Update the camera aspect ratio (currently implicit in `UpdateCamera3D` which reads `GetScreenWidth()/GetScreenHeight()`)
- Update the GI blur pass resolution (which depends on screen dimensions)
- Update the `applyOBJViewPreset` FOV-fit calculation (which uses `GetScreenWidth/Height`)

If `--window-size=1920,1080` is set at `InitWindow` time (before `Demo3D` construction), most of these will naturally pick up the correct dimensions. But the plan doesn't discuss this dependency chain, which could lead to incorrect aspect ratios or stale viewport dimensions if the resize happens at the wrong point in the initialization sequence.

### 6. The `4c A/B` log line already provides in-app cascade timing data

Severity: Low

The plan says "No GPU-timer instrumentation needed" and "The only existing `GL_TIME_ELAPSED` queries are around the mesh-SDF bake, not the cascade loop." But `demo3d.cpp:885` shows `cascadeTimeMs = (GetTime() - t0) * 1000.0;` which is a CPU-side wall-clock timing of the cascade dispatches. The `4c A/B` log at line 987 prints per-cascade mean luminance. This existing CPU timing is less precise than RenderDoc's GPU timing but could serve as a sanity cross-check against the RenderDoc numbers. The plan could mention this existing data as a consistency check alongside the RenderDoc extraction.

### 7. The `--window-size` flag parser location needs to handle commas

Severity: Low

The plan says `--window-size=WxH` with `W,H` comma-separated. The existing CLI parser pattern uses `arg.rfind("--flag=", 0) == 0` followed by `arg.substr(N)` and `sscanf` or `std::atoi`. Parsing `1920,1080` requires either `sscanf("%d,%d", &w, &h)` or splitting on comma. The plan doesn't show the parse code, which is fine for a plan document, but the comma format needs to be consistent with the existing CLI style. Current `--load-obj=NAME` uses string values; `--render-mode=N` uses single integers. The `--camera-pos=x,y,z` pattern (from Step 10 plan) uses comma-separated floats with `sscanf`. This precedent exists and can be reused, but the plan should reference it.

### 8. The plan says "6 dispatch sites" for glPushDebugGroup but there are 10

Severity: Low

The plan says "`glPushDebugGroup`-labeled passes already in `demo3d.cpp` at the 6 dispatch sites". But I found 10 `glPushDebugGroup` calls:
1. GPU JFA SDF (line 1694)
2. GPU triangle voxelize (line 1811)
3. sdf_analytic (line 1997)
4. radiance_3d (line 2147)
5. reduction_3d (line 2168)
6. temporal_blend ×2 (lines 2210, 2220)
7. raymarch (line 2434)
8. gi_blur (line 2530)

The plan says "6 dispatch sites" but there are 10 debug group labels. This doesn't affect the analysis — RenderDoc will group all labeled passes — but the count is inaccurate.

## Verification Gaps To Add

- Specify the exact initialization order for `--window-size`: parse flag → set dimensions for `InitWindow` → call `InitWindow(W, H, ...)` → `glewInit` → `Demo3D` construction. Do NOT use `SetWindowSize` after `InitWindow(DEFAULT...)`.
- Add a viewport update verification step: after launching with `--window-size=1920,1080`, confirm `GetScreenWidth()` returns 1920 and `GetScreenHeight()` returns 1080 before `Demo3D` construction.
- Cross-check RenderDoc per-pass µs against the existing CPU-side `cascadeTimeMs` from the `[4c A/B]` log lines for consistency.
- After the 1080p capture, compare actual raymarch µs against the 2.25× estimate and note whether the scaling is linear or non-linear.
- Confirm that the GI blur pass at 1080p uses the correct viewport dimensions (not stale 720p dimensions from a resize-after-init timing issue).