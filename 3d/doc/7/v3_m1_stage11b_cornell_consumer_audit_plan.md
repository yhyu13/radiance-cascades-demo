# M1 Stage 11b Plan - Cornell Consumer-Side Under-Brightness Audit

**Date:** 2026-05-28.
**Predecessor:** [v3_m1_stage10_mode0_visual_ab_impl.md](v3_m1_stage10_mode0_visual_ab_impl.md) (SC8 + Stage 9 SC10).
**Goal:** characterize the shape of Cornell cascade-GI under-brightness and decide whether the root cause is in the **bake** (probe atlas energy), the **consumer** (`sampleDirectionalGI` integration), or the **scene setup** (Cornell-specific light / material / SDF interaction). No source code changes in this stage — only measurement + hypothesis narrowing.

## Why this stage exists

Stage 10 quantified the Cornell problem precisely:

| Variant         | mode-0 RMS | `ratio_self` (GI-only) |
|-----------------|---:|---:|
| cornell_g010    | 0.395 | ~0.24 |
| cornell_g050    | 0.368 | ~0.29 |
| cornell_g100    | 0.299 | 0.49 |
| cornell_hybrid  | 0.270 | ~0.83 |

Key features of Cornell under-brightness that Stage 11b needs to explain:
1. **Gain-independent**: every MB gain leaves cascade_gi under-bright. So feedback gain is not the lever.
2. **Hybrid IS substantially better** (RMS 0.27 vs cascade-g=1.0 RMS 0.30). So the cascade is not at a fundamental floor — there is room.
3. **Cornell hybrid RMS itself is large** (27× Sponza hybrid 0.010). So Cornell has a *bake-or-consumer* problem hybrid only partially compensates for.
4. **Sponza at gain=0.10 is also ~11% over-bright** (Stage 10 SC11 mean rel err 0.110). So a related residual may exist on both scenes — not strictly Cornell-only.

The investigation needs to distinguish "bake under-emits energy" from "consumer under-integrates energy". The first is fixed by changing the radiance bake; the second by changing the consumer integration.

## Plan

### 1. Pre-audit — characterize the under-brightness shape

Reuse existing Cornell captures:
- `cornell_g100` (g=1.0, hybrid=0) — baseline cascade
- `cornell_hybrid` (g=1.0, hybrid=1) — hybrid oracle

Compute per-pixel maps (saved as EXR for visualization):
- `ratio_gi_per_pixel = cascade_gi / pt_gi` (clip 0..5)
- `delta_gi_per_pixel = cascade_gi - pt_gi`
- `hybrid_gi_per_pixel = (hybrid_full - hybrid_direct) / pt_gi` (where hybrid_full is hybrid run's pt_full sidecar — wait, hybrid is the cascade run with `--use-hybrid=1` so cascade_gi IS the hybrid GI; no separate sidecar needed)

Histogram of `ratio_gi_per_pixel` over the valid mask. Headline metrics:
- Median ratio (more robust than mean for skewed distribution)
- Bimodality test: is the distribution one-peak around 0.5, or two-peak (some pixels at 0.5, some at 1.0)?
- Standard deviation
- Per-region split: bin by `gbuffer.rgb` (surface albedo) so we can see if red wall, green wall, white wall behave differently

If the ratio is uniformly 0.5 ± noise across all pixels → suggests a single multiplicative bug (constant factor of 2 missing).
If the ratio varies per surface → suggests basis/direction-dependent bug (cosine weighting, hemisphere clipping).
If the ratio varies per spatial region → suggests probe-position/SDF bake bug.

### 2. Bake-side vs consumer-side discriminator

The cascade chain writes per-probe directional radiance into the atlas (bake side). The consumer reads the atlas, applies cosine weighting and trilinear interpolation (consumer side). 

Discriminator: use the existing **mode 12** (raw probe radiance, isotropic average per probe — `texture(uRadiance, uvw).rgb`) vs mode 0 (full `sampleDirectionalGI` path). If mode-12 luma per-probe matches PT_GI per-pixel-collapsed-to-probe-cell, the BAKE is producing the right energy and the CONSUMER is dropping it. If mode-12 luma is already half PT, the BAKE is the culprit.

Mode 12 is `texture(uRadiance, uvw).rgb` — the isotropic 3D-blurred probe lookup. We can compare its luma against PT_GI averaged per probe cell.

Capture variant:
- `cornell_mode12` — same as `cornell_g100` but `--render-mode=12` PNG (or just capture mode-17 sidecars; mode 12 changes the SCREEN display, not the EXR sidecars).

Wait — mode 17 EXR sidecars are independent of `--render-mode` for the cascade_gi/pt_full/pt_direct content. The EXR dumps `cascade_gi` from the bake regardless of which mode is displayed. So we don't need a separate mode-12 capture. We compute the isotropic-equivalent from cascade_gi by averaging over probe cells.

Better discriminator (mode-17 EXRs only):
- For each probe cell (mode-17 `probe_diag` p000 maps each pixel to a C0 cell), compute mean `cascade_gi.luma` per cell and mean `pt_gi.luma` per cell. If they match, the consumer is over-spreading per probe. If they don't, the bake is wrong.

Actually simpler — directly compute `mean(cascade_gi.luma) / mean(pt_gi.luma)` per probe cell. If the per-cell ratio is consistently 0.5, the bake (per probe) is under-emitting. If the per-cell ratio is 1.0 but per-pixel ratio is 0.5, the consumer is spreading right but the integration coefficient is wrong (e.g. divide-by-2 in cosine normalization).

### 3. Direction-by-direction check (consumer side)

For each Cornell over-bright/under-bright pixel, mode-17's `probe_bin.exr` records the top bin index (D=8 octahedral). Histogram top-bin indices over all valid Cornell pixels. If under-bright pixels skew toward certain bin indices (e.g. all top-half hemisphere), suggests a hemisphere or basis bug.

Reuse Stage 7 contribution detail analyzer (already shader-side via mode-17 contrib sidecar).

### 4. Sponza-Cornell consistency check

Compute the same per-cell ratio for Sponza at the best gain (g=0.10). If Sponza per-cell ratios are clustered around 1.10 (matching the screen mean rel error 0.110) while Cornell per-cell ratios are around 0.50, the asymmetry is real and scene-specific.

If both scenes show a systematic offset (Sponza 1.1× over, Cornell 0.5× under), they might share a common consumer-side miscalibration that integrates differently with the per-scene direct-light geometry.

### 5. Output

`tools/v3_m1_cornell_audit/cornell_audit_results.json` records:
- Per-pixel ratio histogram (Cornell)
- Per-region ratio split (Cornell, by gbuffer albedo bucket)
- Per-cell ratio statistics (both scenes)
- Top-bin histogram on Cornell over-bright/under-bright pixels
- Headline verdict: `BAKE_UNDER_EMITS` / `CONSUMER_UNDER_INTEGRATES` / `MIXED` / `INCONCLUSIVE`

Decision rule:
- Per-cell ratio close to 1.0 (within 10%) AND per-pixel ratio ~0.5 → `CONSUMER_UNDER_INTEGRATES` (consumer drops energy on the per-pixel integration step)
- Per-cell ratio ~0.5 → `BAKE_UNDER_EMITS` (bake doesn't accumulate enough)
- Per-cell ratio shape is bimodal or strongly region-correlated → `MIXED` (multiple causes)

## Self-critique and improvements

### SC1 — `cascade_gi` is downsampled to PT-res before comparison; per-cell averaging may smear cross-cell signal

Per-cell aggregation depends on `probe_diag.rgb` being a clean cell index. Stage 8/9 found `probe_diag.rgb` is NOT geometric — it scales monotonically with MB gain (the Stage 11 candidate diag-rgb leak). At `g=1.0` the leak should be largest, making per-cell binning unreliable. **Improvement:** also compute per-pixel ratio without per-cell binning (just histogram everything), and report both views. If the per-cell view contradicts the per-pixel view, flag the diag-rgb leak as the confounder.

### SC2 — Cornell at g=0.10 has very few valid pixels (Stage 8 Cornell mb_off valid=36936)

A 2× under-brightness on Cornell could be explained by Cornell's directional light + small scene geometry interacting with the cascade differently than Sponza. **Improvement:** verify with two Cornell cameras (the existing camera file has 3). If under-brightness shape changes with camera, it's view-dependent (consumer-side likely). If it's identical across cameras, it's view-independent (bake or scene-setup likely).

Add cornell_g100_cam1 and cornell_g100_cam2 captures (~6 min).

### SC3 — Hybrid uses the per-pixel correction layer; comparing hybrid_gi against PT_gi is comparing apples to oranges

`hybrid_full` is cascade + per-pixel correction. The "hybrid_gi" I compute as `hybrid_full - pt_direct` is therefore cascade+correction with PT direct subtracted. That's not what the cascade alone produces. **Improvement:** report hybrid metrics only as a reference for "what the production-quality answer looks like" — don't use it to discriminate bake vs consumer. The bake-vs-consumer discriminator must use cascade-only signals.

### SC4 — Cornell baseline ratio_self ≈ 0.49 is the GI-only ratio; the mode-0 RMS = 0.30 is the composite

The two numbers are not directly comparable (one is a ratio of luma means, the other is RMS of pixel differences). **Improvement:** report both per scene per variant. The verdict is on per-pixel ratio shape, not on RMS.

### SC5 — Surface-color bucketing assumes the gbuffer.rgb is the albedo

`gbuffer.rgb` is the surface response stored at the time the GBuffer is written, which could include shading factors (it shouldn't, but worth verifying). **Improvement:** spot-check that gbuffer.rgb at a Cornell red-wall pixel is indeed ~(0.7, 0.1, 0.1) without any direct-light multiplication. If gbuffer is pre-multiplied with anything, the bucketing is wrong.

### SC6 — Cornell has only 3 cameras with measurement preset; SC2's per-camera audit increases capture cost

3 cameras × 1 variant = 3 captures (~9 min). Acceptable. The 3 existing cameras are at different viewing angles; if all three give ~0.5 ratio, view-independence is strong evidence for bake/scene-setup. **Improvement:** capture all 3 cameras at gain=1.0, hybrid=0, render-mode=17.

### SC7 — `cornell_mode12` capture is unnecessary — EXR sidecars are mode-independent for the cascade content

EXR sidecars dump cascade_gi / pt_full / pt_direct at bake completion, not at display time. So mode-17 EXR dumps are sufficient. **Improvement:** skip the mode-12 capture. The "isotropic-equivalent" of cascade can be computed from cascade_gi by per-probe-cell mean. (Already in §2's plan.)

### SC8 — The diag-rgb leak from Stage 8/9 will affect per-cell binning under hybrid_on too

If hybrid invokes the temporal EMA pipeline differently, the diag-rgb leak might be even worse. **Improvement:** treat hybrid per-cell measurements as advisory; rely on per-pixel ratios for the hybrid reference.

### SC9 — A 2× under-brightness could be an energy-conservation off-by-2 (e.g. integrating sphere instead of hemisphere, or 2π instead of π)

If the consumer integrates `L · cos(θ) · dω` over the full sphere instead of the hemisphere, that's a 2× error on a Lambertian surface where the lower hemisphere is zero. Similarly, normalizing by 2π instead of π or vice versa. **Improvement:** check the consumer shader (`sampleDirectionalGI` in raymarch.frag) for any obvious 2π/π/half-sphere math when reading results. This is a code-read task, out of capture scope, but should be done alongside the data analysis.

### SC10 — The audit is data-only; if it lands `MIXED` we'll need a Stage 11b.2 

Don't promise a single-stage closure. **Improvement:** the impl doc explicitly enumerates the next-stage decision tree based on each possible verdict.

## Acceptance

- `tools/v3_m1_cornell_audit/cornell_audit_results.json` records per-pixel + per-cell ratios for Cornell (g=1.0, 3 cameras) and Sponza (g=0.10, existing cam0 capture).
- Per-region ratio split by gbuffer albedo bucket (white wall, red wall, green wall, etc.).
- Bake-vs-consumer verdict.
- Code-read finding for `sampleDirectionalGI` 2π/π/hemisphere math (SC9), even if data is inconclusive.
- Stage 11b.2 / 11b.3 / Stage 11c decision tree based on verdict.

## Out of scope

- Code changes (next stage).
- Stage 11a Fork A infrastructure revival (independent track).
- Diag-rgb leak fix (separate Stage 11c candidate, blocks deeper per-cell shader-side attribution but not this stage's per-pixel analysis).
- Other scenes (sponza_master, cornell_orig, cornell_orig_alcove).
