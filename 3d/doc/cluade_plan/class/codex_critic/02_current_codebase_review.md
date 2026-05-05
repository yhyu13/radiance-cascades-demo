# Current Codebase Review of Claude Class Notes

Review timestamp: 2026-05-05T19:01:51+08:00

Target: `doc/cluade_plan/class/`

Verdict: the class notes are useful as a phase-by-phase teaching set, but they are not consistently current with the live Phase 9-14 code. The largest problem is that many pages still describe the older Phase 5 default model: D=4, C0-only final atlas sampling, co-located-first layout, and separate temporal blend dispatches.

## Findings

### 1. High - Current defaults and key numbers are stale

Affected docs:

- `README.md:51`, `README.md:64-68`
- `00_jargon_index.md:145-194`
- `08_phase5e_direction_scaling.md:43-46`, `08_phase5e_direction_scaling.md:89-101`
- `10_c0_resolution_and_configuration.md:56`, `10_c0_resolution_and_configuration.md:111-126`
- `14_phase9_temporal.md:10`
- `15_phase10_staggered.md:15`
- `18_phase14_range_scaling.md:69`

The notes repeatedly use D=4 and a 128x128x32 C0 atlas as the current baseline. The live runtime default is different:

- `src/demo3d.cpp:126` initializes `dirRes(8)`.
- `src/demo3d.cpp:129` initializes `useScaledDirRes(true)`.
- `src/demo3d.cpp:2069-2074` computes `cascD = min(16, dirRes << i)` and atlas XY as `probeRes * cascD`.
- `src/demo3d.cpp:151` initializes `cascadeC0Res(32)`.
- `src/demo3d.h:51` sets `DEFAULT_VOLUME_RESOLUTION = 128`, while `README.md:65` says the SDF grid is 64^3.

Current default cascade D values are therefore:

```text
C0: 32^3 probes, D=8,  atlas 256x256x32, about 16 MB RGBA16F
C1: 16^3 probes, D=16, atlas 256x256x16, about 8 MB RGBA16F
C2:  8^3 probes, D=16, atlas 128x128x8,  about 1 MB RGBA16F
C3:  4^3 probes, D=16, atlas  64x64x4,  about 0.125 MB RGBA16F
```

That is about 25.125 MB of live directional atlas data before history textures, not the roughly 7 MB D4/D8/D16/D16 estimate in `10_c0_resolution_and_configuration.md:111`.

Also, `10_c0_resolution_and_configuration.md:56` says the C0 UI combo offers `{8, 16, 32, 64}`. The live UI offers `{8, 16, 24, 32, 48, 64}` at `src/demo3d.cpp:2868-2870`.

Suggested correction: update the README key numbers first, then make all phase pages explicitly separate "historical phase value" from "current default value".

### 2. High - Directional final render is not C0-only anymore

Affected docs:

- `README.md:51`
- `01_scene_and_pipeline.md:62-63`, `01_scene_and_pipeline.md:83`
- `02_probes_and_cascades.md:129`, `02_probes_and_cascades.md:147-149`
- `12_phase5g_directional_gi.md:121`
- `00_jargon_index.md:199`

The docs say the directional renderer reads the C0 atlas. The current code binds the selected cascade for both the isotropic grid and directional atlas:

- `src/demo3d.cpp:1644` clamps `selectedCascadeForRender` into `selC`.
- `src/demo3d.cpp:1681-1691` binds `cascades[selC].probeAtlasTexture` or history to `uDirectionalAtlas` and uploads `cascadeDirRes[selC]`.
- `src/demo3d.cpp:3217` exposes the selected cascade in the UI.

This means the class notes give the wrong debugging model when the user selects C1/C2/C3 for render inspection. The docs should say "selected cascade atlas" unless they are intentionally describing old Phase 5g behavior.

### 3. High - Phase 5d page says the opposite of the current layout path

Affected docs:

- `07_phase5d_probe_layout.md:10`
- `07_phase5d_probe_layout.md:190`
- `07_phase5d_probe_layout.md:227`
- `07_phase5d_probe_layout.md:236`
- Also related: `06_phase5c_directional_merge.md:50-64`

The page says co-located is the default and spatial trilinear is not implemented. The live defaults and shader path are now:

- `src/demo3d.cpp:128` initializes `useColocatedCascades(false)`.
- `src/demo3d.cpp:131` initializes `useSpatialTrilinear(true)`.
- `src/demo3d.cpp:1367` uploads `uUseSpatialTrilinear`.
- `res/shaders/radiance_3d.comp:165` defines `sampleUpperDirTrilinear`.
- `res/shaders/radiance_3d.comp:317-355` uses spatial trilinear in the non-co-located directional upper-cascade lookup.

Suggested correction: retitle the current-state portion around non-co-located cascades and move the "single nearest upper probe" explanation into a historical or disabled-toggle subsection.

### 4. Medium - Temporal accumulation docs describe the fallback as the main path

Affected docs:

- `01_scene_and_pipeline.md:38`
- `14_phase9_temporal.md:29-40`
- `15_phase10_staggered.md:48`
- `17_phase12_capture.md:87`

The docs say `temporal_blend.comp` blends live textures into history after the bake. In the current code, the normal path fuses atlas EMA into `radiance_3d.comp` and then swaps live/history handles:

- `res/shaders/radiance_3d.comp:69-73` documents fused in-bake EMA.
- `res/shaders/radiance_3d.comp:381-405` writes `mix(hist, rad, uTemporalAlpha)` during bake.
- `src/demo3d.cpp:1380-1387` enables `doFusedEMA`.
- `src/demo3d.cpp:1459-1467` swaps atlas/grid live and history handles after fused write.
- `src/demo3d.cpp:1470-1504` keeps the old `temporal_blend.comp` dispatch only as a non-fused fallback.

The current `14_phase9_temporal.md` also has a convergence math issue: stale history weight decays as `(1 - alpha)^n`, so with `alpha = 0.05` the old value is about `0.95^14 = 0.49` after 14 updates. The written `0.05^(1/14)` expression is not the EMA decay.

Suggested correction: document fused EMA as the default path and keep `temporal_blend.comp` in a "fallback/debug compatibility" subsection.

### 5. Medium - Staggered update wording overstates per-frame baking

Affected docs:

- `01_scene_and_pipeline.md:48-49`
- `15_phase10_staggered.md:11-19`, `15_phase10_staggered.md:34-37`
- `00_jargon_index.md:359-365`

The notes say C0 bakes every frame by default. That is only true once a cascade rebuild is actually requested. With temporal probe jitter enabled, the code changes jitter positions only after the hold counter reaches `jitterHoldFrames`:

- `src/demo3d.cpp:143-145` default `jitterPatternSize=8`, `jitterHoldFrames=2`.
- `src/demo3d.cpp:602-624` sets `cascadeReady=false` only when the jitter position advances.
- `src/demo3d.cpp:631-642` bakes cascades only when `cascadeReady` is false.
- `src/demo3d.cpp:1310-1318` applies per-cascade staggering inside that rebuild.

So the practical default is "C0 updates on each jitter position advance, currently every 2 frames", while upper cascades are further gated by the stagger interval. The docs should be precise about "eligible update frame" versus "actual bake frame".

### 6. Medium - GI blur is active for more than final mode 0

Affected docs:

- `16_phase9c_gi_blur.md:88-95`
- `01_scene_and_pipeline.md:40-41`

The GI blur page says all non-zero render modes bypass the blur. The current CPU code activates separated direct/indirect output and the blur pass for render modes 0, 3, and 6:

- `src/demo3d.cpp:878` runs `giBlurPass()` when mode is 0, 3, or 6.
- `src/demo3d.cpp:1699-1702` sets `uSeparateGI` for those same modes.
- `res/shaders/raymarch.frag:453-464` mode 3 can output GI into `fragGI`.
- `res/shaders/raymarch.frag:501-504` mode 6 samples directional GI.
- `res/shaders/raymarch.frag:541-543` final mode samples directional GI.

Suggested correction: document the exact mode set: 0 final, 3 indirect, and 6 diffuse albedo + GI preview are post-filtered when `useGIBlur` is enabled.

### 7. Medium - Phase 14 range interval table is mathematically wrong for C1

Affected docs:

- `18_phase14_range_scaling.md:29-41`
- `18_phase14_range_scaling.md:85-95`

The doc says the default C1 interval becomes `[1.000, 1.000]` and that interval boundaries shift accordingly. The shader does not move `tMin`; it only clamps/extends `tMax` with `uCnMinRange`:

- `src/demo3d.cpp:1341-1344` uploads `c0MinRange` for C0 and `c1MinRange` for C1.
- `res/shaders/radiance_3d.comp:290-295` sets C0 `tMin=0.02`; for higher cascades it keeps `tMin = f * d` and sets `tMax = max(f * 4.0 * d, uCnMinRange)`.

With `d = 0.125`, current defaults give:

```text
C0: [0.020, 1.000]
C1: [0.125, 1.000]
C2: [0.500, 2.000]
C3: [2.000, 8.000]
```

Suggested correction: describe Phase 14 as an overlap extension, not as a clean shifted handoff boundary.

### 8. Low - Capture sequence wording no longer matches the default jitter cycle

Affected docs:

- `17_phase12_capture.md:13`
- `17_phase12_capture.md:144`
- `00_jargon_index.md:421`

The docs say the sequence captures 8 frames, one per jitter position. The live defaults are `seqFrameCount=8`, `jitterPatternSize=8`, and `jitterHoldFrames=2`:

- `src/demo3d.h:555` defaults `seqFrameCount = 8`.
- `src/demo3d.cpp:143-144` defaults `jitterPatternSize=8`, `jitterHoldFrames=2`.
- `src/demo3d.cpp:831-865` captures consecutive frames.

With hold=2, a full 8-position cycle is 16 frames. An 8-frame capture is half of that cycle unless the user changes hold or sequence length.

### 9. Low - Some phase pages need stronger historical/current labels

Affected docs:

- `05_phase5b_atlas.md:10-25`
- `05_phase5b_atlas.md:132-138`
- `06_phase5c_directional_merge.md:50-64`
- `09_phase5f_bilinear.md:161`

These pages are acceptable if they are explicitly read as implementation-history chapters. They become misleading when read as current architecture because later code now samples the directional atlas in the final renderer, scales D per cascade, and uses spatial trilinear upper lookup.

Suggested correction: add a short "Current code note" block near the top of each historical phase page that points readers to the current-state pages and defaults.

## Recommended Fix Order

1. Fix `README.md` key numbers and renderer summary. This is the first page readers see, and it currently anchors them to stale defaults.
2. Update `00_jargon_index.md` so glossary entries match current defaults and render modes.
3. Rewrite `07_phase5d_probe_layout.md` around current non-co-located + spatial trilinear behavior.
4. Update `12_phase5g_directional_gi.md` from C0-only to selected-cascade atlas sampling.
5. Update `14_phase9_temporal.md`, `15_phase10_staggered.md`, and `16_phase9c_gi_blur.md` to match the fused EMA, jitter hold, stagger, and mode-specific blur behavior.
6. Correct `18_phase14_range_scaling.md` interval math.

## Side Note: Code Comments/UI Text Also Lag Runtime Defaults

The runtime value is `dirRes=8`, but several code comments and UI labels still say D4/D8/D16/D16:

- `src/demo3d.cpp:129`
- `src/demo3d.cpp:541`
- `src/demo3d.cpp:3001`
- `src/demo3d.cpp:3016`
- `src/demo3d.cpp:3557`

That is outside the Claude-doc review target, but it likely contributed to the documentation drift.
