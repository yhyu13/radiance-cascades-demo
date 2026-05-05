# 11 Phase 6-14: What Changed After Directional Cascades

Phase 5 made the renderer directional. The later work did not replace that core model. It made the renderer easier to diagnose, smoother over time, and less dependent on one brittle frame.

## The short version

After Phase 5, the branch added four practical systems:

1. Capture and analysis tooling, so artifacts can be inspected from saved images and GPU captures.
2. Better diagnostic render modes, so the code can separate SDF issues, step-count artifacts, probe-cell artifacts, and directional-bin artifacts.
3. Temporal accumulation with jitter, so repeated probe bakes can cover sub-cell positions without increasing the probe count.
4. Quality defaults, including GI blur and C0/C1 minimum ray reach, so the current startup path is closer to the stable configuration.

## Phase 6: capture becomes part of the workflow

Phase 6a added clean screenshots and optional Claude vision analysis:

- `P` queues a clean screenshot after the 3D pass and before ImGui.
- `tools/analyze_screenshot.py` can analyze a single image.
- Later phases extended the same script for burst and sequence captures.

Phase 6b added RenderDoc capture:

- `G` queues a RenderDoc GPU frame capture on Windows when RenderDoc is installed.
- `--auto-rdoc` queues one after warm-up.
- The code preloads `renderdoc.dll` before OpenGL context creation and wraps major passes with debug groups.
- `tools/rdoc_extract.py` runs inside `qrenderdoc.exe --py`.
- `tools/analyze_renderdoc.py` runs in regular Python and writes the pipeline report.

The key mental shift is that a wrong final image is no longer the only evidence. The branch can now inspect probe atlases, grids, SDF slices, GPU timings, and final-frame thumbnails.

## Phase 7-8: stronger diagnostics

Phase 7 added the analytic SDF toggle in the final raymarcher. That lets the branch compare:

- texture-backed SDF sampling
- direct analytic primitive SDF evaluation

If an artifact survives analytic SDF mode, it is probably not caused by voxel-grid quantization.

Phase 7 also added final render modes 7 and 8:

- mode 7 shows continuous ray travel distance
- mode 8 shows probe-cell boundary coordinates

Phase 8 made directional resolution live through the UI. Changing `dirRes` now rebuilds cascade atlases because atlas dimensions depend on `D`.

## Phase 9: temporal accumulation and jitter

The branch found that some artifacts were spatial undersampling artifacts: fixed probe centers produced a fixed biased result.

Phase 9 answers that by moving probe sample positions slightly and blending the results over time:

1. `radiance_3d.comp` offsets probe world positions by `uProbeJitter`.
2. Jitter positions come from Halton(2,3,5).
3. Each fresh bake is blended into atlas/grid history.
4. The display path reads history when temporal accumulation is active.

The important caveat:

- temporal accumulation without probe jitter mostly converges to the same biased image
- temporal accumulation with probe jitter can soften probe-grid banding because it samples multiple nearby positions

Phase 9b added history clamping. This is a TAA-style guard: clamp stale history into a neighborhood range before blending so old jitter samples cannot bleed arbitrary color forward.

Phase 9c/9d added GI blur. The final raymarch shader can output direct light, GBuffer, and indirect light separately. `gi_blur.frag` blurs only the indirect term, using depth, normal, and luminance weights, then composites direct + blurred indirect.

## Phase 10: make temporal cheaper

Updating every cascade every frame is expensive once temporal jitter is active.

Phase 10 added staggered updates:

- C0 updates every frame
- C1 can update every 2nd frame
- C2 can update every 4th frame
- C3 can update every 8th frame

The exact rule is:

```text
cascade i updates when renderFrameIndex % min(1 << i, staggerMaxInterval) == 0
```

Phase 10 also added fused atlas EMA. When active, the bake shader blends atlas history while writing the fresh atlas, then C++ swaps texture handles. That removes the separate atlas temporal-blend dispatch in the common path.

Implementation names matter here: the C++ gate is `doFusedEMA`, the shader gate is `uTemporalActive`, and the history input is `uAtlasHistory`. `temporal_blend.comp` still exists and is still loaded because it is the non-fused fallback path, not because fused EMA is absent.

## Phase 12 and 14a: capture sequences, not just one frame

The current capture tools are:

- single screenshot: one current-frame image
- burst capture: modes 0, 3, and 6 across three frames
- sequence capture: N consecutive frames of the current render mode

Burst capture is useful for separating:

- final image
- magnified indirect
- GI-only

Sequence capture is useful for temporal problems. It can show whether jitter/EMA settings cause flicker, drift, or slow convergence over multiple frames.

CLI modes:

- `--auto-analyze` runs a burst capture, analyzes it, then exits
- `--auto-sequence` runs a sequence capture, analyzes it, then exits
- `--auto-rdoc` captures a RenderDoc frame after warm-up

## Phase 13-14: current quality defaults

Later quality work changed the startup behavior. The current constructor defaults include:

- `dirRes = 8`
- D scaling on, so scaled D is `D8/D16/D16/D16`
- non-co-located cascade layout on
- directional merge, directional bilinear, spatial trilinear, direct shadow rays, and directional GI on
- temporal accumulation on with `temporalAlpha = 0.05`
- probe jitter on with `jitterPatternSize = 8`, `jitterHoldFrames = 2`, and `probeJitterScale = 0.06`
- history clamp on
- stagger max interval `8`
- GI blur on with radius `8`
- C0 and C1 minimum ray reach both set to `1.0` world unit
- soft shadow display/bake off
- environment fill off

The C0 and C1 minimum ranges are important. The old interval formula made C0 and C1 too short-ranged for many probes to hit Cornell-box surfaces consistently. Raising the minimum reach improved `surfPct` coverage and reduced temporal drift from open-air probes.

## What did not change

The core renderer is still the Phase 5 renderer:

- probes still bake per-direction atlas data
- lower cascades still merge from upper cascades
- reduction still produces an isotropic grid
- the final renderer still chooses isotropic or directional GI consumption

The newer phases make that system easier to evaluate and less visually brittle. They are not a different GI algorithm.
