# 12 Current Debug Workflow

This note is the practical "what should I look at first?" guide for the current codebase.

## Start with the render mode

Use final render modes in `raymarch.frag`:

- mode 0: final image
- mode 3: indirect x5, using the isotropic probe grid
- mode 4: direct only
- mode 5: integer step-count heatmap
- mode 6: GI only, respecting the directional-GI toggle
- mode 7: continuous ray-distance heatmap
- mode 8: probe-cell boundary visualization

The most useful comparisons are:

- mode 0 vs mode 4: how much of the image is direct lighting?
- mode 3 vs mode 6: is the issue in isotropic reduced GI or directional GI consumption?
- mode 5 vs mode 7: is a banding pattern just integer step-count quantization?
- mode 6 vs mode 8: does GI banding line up with probe-cell boundaries?

GI blur is enabled by default and affects modes 0, 3, and 6. Turn GI blur off when you need to inspect the raw probe signal rather than the postfiltered display.

## Use the radiance overlay for bake data

The radiance overlay is a separate mode namespace from the final render modes.

Use `radiance_debug.frag` overlay modes:

- overlay 3: raw atlas tiles
- overlay 4: hit-type heatmap
- overlay 5: one nearest direction bin across probes
- overlay 6: bilinear direction-bin view

These answer bake-side questions:

- are atlas tiles populated?
- do different directions store different wall colors?
- does bilinear direction lookup smooth bin-boundary changes?
- are many probes missing surfaces or sky exits?

## Read the probe stats carefully

The useful cascade stats are:

- `anyPct`: probes with nonzero radiance
- `surfPct`: probes with at least one direct surface hit in their interval
- `skyPct`: probes with at least one sky exit
- `meanLum`, `maxLum`, and variance

`surfPct` is especially useful after Phase 14. Low C0 or C1 `surfPct` means many probes are not hitting geometry in their own interval, so temporal jitter can turn into slow drift rather than stable smoothing.

The current defaults use `c0MinRange = 1.0` and `c1MinRange = 1.0` to keep C0 and C1 coverage high in the Cornell Box.

## Use the capture buttons by failure shape

Use `P` or Screenshot when:

- one final-frame artifact is enough to describe the problem
- you want a clean frame without ImGui

Use Burst Capture when:

- you need to compare final, indirect x5, and GI-only views
- you want probe stats JSON sent with the image set

Use Seq Capture when:

- the problem changes over frames
- you are judging jitter, EMA, flicker, ghosting, or slow convergence

Use `G` / RenderDoc when:

- screenshots cannot tell which GPU pass produced the problem
- you need per-dispatch timing
- you need to inspect named textures such as SDF, albedo, probe atlas, and probe grid

## Current output locations

- screenshots: `tools/frame_*.png`
- screenshot analysis: `tools/frame_*.md`
- burst images: `tools/frame_*_m0.png`, `_m3.png`, `_m6.png`
- sequence images: `tools/frame_*_f0.png`, `_f1.png`, ...
- probe stats: `tools/probe_stats_*.json`
- RenderDoc captures: `tools/captures/`
- RenderDoc extraction/analysis: `tools/analysis/`

## Common interpretation traps

Do not treat mode numbers as global. Final render modes and radiance overlay modes are different namespaces.

Do not treat temporal accumulation alone as a spatial fix. Without jitter, it mostly averages the same biased sample.

Do not assume GI blur proves the underlying probe signal is fixed. It is a screen-space postfilter on indirect light.

Do not use old Phase 5 D-scaling examples as current defaults. The current default `dirRes=8` with scaled D on produces `D8/D16/D16/D16`.

Do not use one screenshot to judge temporal quality. Use sequence capture when the suspected failure is flicker, drift, ghosting, or convergence.
