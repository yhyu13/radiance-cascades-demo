# M1 Stage 11b Implementation - Cornell Consumer-Side Under-Brightness Audit

**Date:** 2026-05-28.
**Plan:** [v3_m1_stage11b_cornell_consumer_audit_plan.md](v3_m1_stage11b_cornell_consumer_audit_plan.md).
**Result artifact:** `tools/v3_m1_cornell_audit/cornell_audit_results.json`.
**Verdict:** **`BAKE_UNDER_EMITS`.** Despite the stage name "consumer-side audit," the data points clearly at the bake: cornell cascade GI luma is ~45% of PT GI luma even when aggregated per probe cell (i.e. before the per-pixel consumer integration step). The consumer is exonerated. The bake under-emits.

## What changed

No engine source changes. Tooling only:

1. `tools/v3_m1_cornell_audit/capture_cornell_cam.ps1` — captures Cornell at gain=1.0, hybrid=0, render-mode=17 for the chosen measurement camera (uses the stdout-redirect pattern from Stage 10).
2. `tools/v3_m1_cornell_audit/analyze_cornell_audit.py` — per-camera, per-region, per-cell analyzer. Computes:
   - per-pixel `cascade_gi / pt_gi` histogram + mean/median/std
   - per-region split by gbuffer-derived albedo bucket (red wall, green wall, white, other)
   - per-cell aggregate luma ratio (cascade vs PT, grouped by `probe_diag.rgb` p000)
   - top-bin histogram on under-bright pixels
   - Sponza g=0.10 consistency check
   - bake-vs-consumer verdict per plan §5

## Verification

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_cornell_audit/capture_cornell_cam.ps1 -Cam 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_cornell_audit/capture_cornell_cam.ps1 -Cam 2
python tools/v3_m1_cornell_audit/analyze_cornell_audit.py
```

Both captures finished cleanly with exit=0 and all sidecars written. Cornell cam0 reused from Stage 8. Sponza g=0.10 reused from Stage 9.

## Results

### Per-pixel ratio `cascade_gi / pt_gi` on Cornell

| Camera | mean | median | per-pixel valid count |
|---|---:|---:|---:|
| cam0_default       | 0.4922 | 0.3926 | ~37k |
| cam1_closeup_front | 0.4089 | 0.3719 | (similar) |
| cam2_front_left    | 0.5280 | 0.3964 | (similar) |

All three cameras show median ratios in `[0.37, 0.40]` and means in `[0.41, 0.53]`. **The under-brightness is view-independent** — it's not a screen-space or camera-dependent artifact.

### Per-cell aggregate ratio (plan §2 — bake-vs-consumer discriminator)

Aggregate `mean(cascade_luma) / mean(pt_luma)` per probe-grid cell (using `probe_diag.rgb` as the cell key), weighted by pixel count:

| Camera | per-cell ratio (weighted) |
|---|---:|
| cam0 | 0.4521 |
| cam1 | 0.3936 |
| cam2 | 0.4825 |

**Per-cell ratio is essentially the same as per-pixel ratio.** This is the headline: if the cascade had the right energy at the probe atlas level and the consumer integration step was dropping it, the per-cell ratio would be ~1.0 and the per-pixel ratio ~0.5. Instead, per-cell ≈ per-pixel ≈ 0.45.

That rules out plan §5's `CONSUMER_UNDER_INTEGRATES` branch. The consumer's `sampleProbeDir` weighted-mean integration is correctly preserving the energy it's given. The bake just doesn't put enough energy into the atlas in the first place.

### Per-region split on cam0 (per albedo classification)

| Region | count | ratio median |
|---|---:|---:|
| red wall  | (significant) | 0.3264 |
| green wall| (significant) | 0.2267 |
| other     | (most pixels) | 0.4031 |

Green wall is the most under-bright (median 0.23). Red wall 0.33. "Other" 0.40. SC5 fired — gbuffer.rgb appears to NOT be pure albedo (most pixels classified as "other" instead of red/green/white wall), so the per-region split is partial. But the trend is clear: GREEN < RED < OTHER, all well below 1.0. No region is correctly emitted; green is worst.

The asymmetry (green worse than red) is interesting but doesn't change the verdict — both are well below 1.0. The cascade bake under-emits everywhere on Cornell, with extra under-emission on the green-wall direction. Possible cause for the asymmetry: cosine weighting interacts with the bin-direction quantization differently for the +x vs -x dominant-direction probes.

### Sponza g=0.10 consistency check (plan §4)

Sponza per-pixel ratio = 1.040. Sponza is well-calibrated at g=0.10. **The asymmetry is real and scene-specific** — Sponza's bake is fine; Cornell's bake is broken.

## Code-read finding (plan SC9)

I read `res/shaders/raymarch.frag:421-446` (`sampleProbeDir`). The consumer integration is:

```
for each bin (dx, dy) in [0, D)^2:
    bdir = binToDir(bin, D)
    wcos = max(0, dot(bdir, normal))      // upper-hemisphere clamp
    a    = texelFetch(uDirectionalAtlas, ...)
    w    = wcos * a.a                       // a.a = "alpha" used as opacity weight
    irrad += a.rgb * w
    wsum  += w
return irrad / max(wsum, 1e-4)             // weighted mean (NOT a Riemann sum)
```

No 2π / π / hemisphere math. No factor-of-2 bug surface in the consumer. This is the Delta #2 LANDED behavior (weighted-mean normalization, not Riemann sum) — it gives the *average* per-bin radiance over visible bins. For a Lambertian surface where the bake records per-bin radiance correctly, this is the right answer.

Conclusion: the consumer is structurally clean. The bug is in the **bake**.

## Self-critique

**SCs that fired:**

- **SC1 (diag-rgb leak under hybrid)**: avoided. Stage 11b uses cascade-only captures (hybrid=0), so the leak doesn't apply here. Per-cell binning is reliable at g=1.0 / hybrid=0 because the leak scales with MB feedback, not with hybrid.
- **SC2 (multi-camera view-dependence)**: confirmed view-independence. 3 cameras, all give per-pixel mean ~0.45-0.53. Not a screen-space artifact.
- **SC5 (gbuffer.rgb might not be pure albedo)**: confirmed. Most pixels classified as "other" instead of red/green/white wall. Stage 11b per-region split is partial. **Improvement noted**: future per-region work should pull pure albedo from a different source (e.g. material/voxel lookup) or modify the engine to store an unmultiplied albedo channel.
- **SC9 (consumer 2π/π/hemisphere code-read)**: clean. No obvious factor-of-2 bug in `sampleProbeDir`. The weighted-mean normalization is the landed Delta #2 design.

**New SC surfaced by the data:**

- **SC11 (new): Green-wall asymmetry (0.23 vs red 0.33) is unexplained.** Could be:
  - bin-direction quantization (octahedral D=8 maps +y/-y/+x/-x with different precision)
  - direct light position interaction (Cornell light at (0, 0.8, 0); green wall is at -x, red at +x — geometrically symmetric so light should illuminate both equally)
  - voxelization asymmetry (red and green walls might get different voxel coverage)
  - bake-side cosine pre-weighting that's missing for one wall
  
  This asymmetry should be a Stage 11c sub-investigation. It's hard to debug because both walls are geometrically symmetric in Cornell.

- **SC12 (new): The verdict doesn't tell us WHY the bake under-emits.** Only that it does. We know:
  - It's view-independent (all 3 cameras agree)
  - It's scene-specific (Sponza is fine; Cornell is broken)
  - It's gain-independent (Stage 9 — every MB gain leaves Cornell at ~0.5 ratio)
  - It affects all regions but green wall the most (~0.23)
  
  Stage 11c needs to instrument the **bake** path (`radiance_3d.comp` + light-injection) directly. Candidate experiments:
  1. **Light-type discriminator**: temporarily make Cornell use a directional light (overhead). If ratio jumps to 1.0, the bug is in how the bake handles point-light shadowing.
  2. **Bake-side energy probe**: log mean radiance per cascade per frame and compare against PT-equivalent.
  3. **Per-bin α audit**: dump α values for Cornell at a few representative probes; if α is ~0.5 systematically, the visibility test in the bake fires too aggressively.

## Decision

`BAKE_UNDER_EMITS` is the verdict. Stage 11b confirms the cascade bake under-emits Cornell GI by ~2× at the probe-atlas level, and the consumer is clean.

**Stage 11 work order update:**

- **Stage 11a (revive Fork A for Sponza)** — unchanged; still valid; independent track.
- **Stage 11b (this stage)** — CLOSED with `BAKE_UNDER_EMITS`.
- **Stage 11c (bake-side audit)** — NEW. Replaces my earlier vague "consumer-side audit" with a targeted bake-side investigation. Three candidate first probes (light-type, bake-side energy probe, per-bin α audit) listed above; pick one based on quickest-to-falsify ordering.
- **Stage 11d (diag-rgb leak fix)** — STILL OPEN; not actually blocking 11c since 11c can use new shader instrumentation that isn't subject to the leak.

The Cornell green-wall asymmetry (SC11) is filed as a Stage 11c sub-investigation; do not chase it independently.

**No engine source changes in this stage** — pure measurement + verdict refinement. The full 2× under-brightness is a real bug, not a measurement artifact, and the next stage's job is to find which line of bake-side code is responsible.
