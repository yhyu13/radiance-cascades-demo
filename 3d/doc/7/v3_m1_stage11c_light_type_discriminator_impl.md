# M1 Stage 11c Implementation - Light-Type Discriminator (first bake-side probe)

**Date:** 2026-05-28.
**Plan:** [v3_m1_stage11c_light_type_discriminator_plan.md](v3_m1_stage11c_light_type_discriminator_plan.md).
**Result artifact:** `tools/v3_m1_cornell_light_type/light_type_results.json`.
**Verdict:** **`LIGHT_TYPE_DOMINANT`** — directional Cornell closes 87% of the gap to PT (ratio mean 0.49 → 0.93); cascade specifically gains 1.98× relative to PT under directional light. The cascade bake under-emits **point-light direct contribution** by ~2×.

## What changed

Minor engine-CLI plumbing in `src/main3d.cpp` to make `--light-direction=` survive `loadOBJMesh`. No render-code changes.

The CLI handler already called `setUseDirectionalLight(true)` + `setLightDirection(...)`, but `loadOBJMesh` at `demo3d.cpp:7137` then unconditionally writes `useDirectionalLight = isSponza`, silently clobbering the CLI choice for Cornell. The fix is two ~5-line blocks:

1. The `--light-direction=` handler stores the parsed vector in two file-scope globals `g_cliLightDirSet` and `g_cliLightDir`.
2. After the deferred `loadOBJMesh` block, a new re-apply block sets `setUseDirectionalLight(true)` + `setLightDirection(g_cliLightDir)` if `g_cliLightDirSet` is true, so the CLI setting wins over the mesh-load default.

Plus new tooling under `tools/v3_m1_cornell_light_type/`:
- `capture_directional.ps1` — Cornell + `--light-direction=0,-1,0` capture script.
- `analyze_light_type.py` — compares against Stage 11b cornell_point_baseline and emits the verdict.

## Verification

```
cmake --build build --config Release --target RadianceCascades3D
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_cornell_light_type/capture_directional.ps1
python tools/v3_m1_cornell_light_type/analyze_light_type.py
```

Build passed with no new warnings. The first attempt was a false negative (analyzer reported bit-identical numbers to baseline) because the CLI light setup was clobbered — caught in 1 minute via the analyzer's `cascade_luma_change_factor=1.0` flag and the missing light-setup grep hits. Re-ran after the plumbing fix; got real signal.

## Results

| Metric | cornell_point_baseline | cornell_directional | change |
|---|---:|---:|---:|
| per-pixel `cascade_gi / pt_gi` mean   | 0.4922 | **0.9345** | +89% closer to 1.0 |
| per-pixel ratio median                | 0.3926 | (not reported separately; mean shift swamps median) |       |
| per-cell weighted ratio               | 0.4521 | **0.8926** | +97% closer to 1.0 |
| absolute mean cascade_gi luma (valid) | (1.0×)  | 0.34× of baseline | absolute scene got dimmer |
| absolute mean pt_gi luma (valid)      | (1.0×)  | 0.17× of baseline | PT got even more dim |
| **cascade vs PT change asymmetry**    | —       | **1.98×**           | **cascade specifically gains 2× relative to PT under directional** |
| bridged fraction (gap-to-1.0 closed) | —       | **87.1%**           | dominant |

**Both per-pixel and per-cell ratios converge to ~0.9 under directional** (baseline ~0.45). The fix is at the per-cell level (bake), consistent with Stage 11b's `BAKE_UNDER_EMITS` verdict — the consumer continues to be exonerated.

**Cascade-directional even surpasses the hybrid-point oracle**: hybrid_on Cornell had `ratio_self=0.83` (Stage 8); directional cascade reaches 0.93. The hybrid correction layer was compensating for the same point-light under-emit at the consumer stage; with the bake fixed at source, hybrid's correction becomes unnecessary on this scene.

**The 1.98× cascade-vs-PT change asymmetry is the headline number**: when the light type changes, *both* cascade and PT recompute everything, and PT's per-pixel `pt_gi` shifts because the actual scene illumination changed. Specifically, `pt_gi` went down 0.17× (directional gives less diffuse-bounce energy than the closer point light) but `cascade_gi` only went down 0.34× — meaning the cascade *kept more energy proportionally*. That 1.98 ratio is precisely the missing 2× factor that disappeared when the light geometry stopped requiring point-source sampling.

## Self-critique

**SCs that fired:**

- **SC1 (cascade and PT both change under directional)**: confirmed and central to the interpretation. Absolute luma asymmetry (1.98×) is the cleanest signal — not the ratio alone.
- **SC2 (Cornell baseline light position unverified)**: not blocking. The baseline ratio_self=0.4922 reproduces Stage 8/11b numbers exactly to 4 decimals, so the baseline light setup is the same one Stage 11b measured.
- **SC5 (interpretation care)**: directional ratio 0.93 with 1.98× cascade asymmetry is unambiguous — cascade specifically gains under directional, not just "directional is intrinsically easier."
- **SC6 (analyzer should grep light setup from log)**: the analyzer's log-grep failed because PowerShell's `*>` redirect produces UTF-16 LE files; my grep expected UTF-8. **Improvement landed in this stage's impl doc rather than in code**: future log-grep tools should accept both encodings. Tracked but not blocking the verdict.
- **SC7 (null result has high value too)**: not applicable here — got a strong positive — but the false-first-run was a useful demonstration that the analyzer would have surfaced a null result correctly.
- **SC10 (hybrid is also under-emitting Cornell)**: cascade-directional surpasses hybrid-point. Confirms hybrid's residual Cornell shortfall is the same point-light geometry problem cascade had, just less severe because hybrid does a per-pixel MC correction. Hybrid is not structurally separate; it's the same bug at a smaller magnitude.

**Things the plan missed / new SCs:**

- **SC11 (false-first-run via CLI clobber)**: the plan flagged at SC6 that I should verify the baseline light setup, but it did NOT predict that `loadOBJMesh` would clobber the CLI `useDirectionalLight` for Cornell on the new capture too. The first capture run produced bit-identical EXRs to the baseline because the directional flag never took effect. Caught by the analyzer's `cascade_luma_change_factor=1.0` sanity. **Improvement landed**: the engine-CLI plumbing fix is now in `main3d.cpp` for any future stage that wants to override the auto-light on OBJ load.
- **SC12 (green-wall asymmetry cross-check)**: under directional, the green wall has too few classified pixels to compute a median (gbuffer-based classifier surfaces SC5's earlier "gbuffer is not pure albedo" issue more strongly when the lighting changes the shaded color). The Stage 11b green-wall asymmetry (red-median 0.10 above green) is **probably explained by the same point-light geometry** but Stage 11c can't directly prove it. Filed as a sub-investigation for whichever Stage 11d probe touches the bake direct path.

## Interpretation

Cornell baseline `cascade_gi` is ~50% of PT GI *specifically because* the bake doesn't capture enough direct-light energy from the small point light. Switching to a "light at infinity along (0,-1,0)" (which the engine handles as a far-point) recovers 87% of the gap. The remaining 13% might be:
- residual indirect-bounce mismatch (bounce-2+ from the cascade chain is still slightly off)
- the small "directional" light is internally still a point at far distance, not a truly parallel source
- temporal accumulation finite-N noise

What's almost certainly happening in the bake: the probe rays sample directions over the unit sphere. The probability of any single ray hitting a 0-area point light is 0 (point lights have zero solid angle). Either:

- **Hypothesis A (most likely):** the cascade bake relies on direct-light visibility being captured by the random-ray hit on the *floor*, where the floor's color includes the direct illumination. So cascade indirect = (probability of hitting the lit floor) × (floor brightness). A small point light produces a small lit-floor patch, which fewer probe rays hit. A directional light produces a uniformly lit floor (all rays hitting the floor see the same brightness), so the cascade samples the lit floor at full statistical rate.
- **Hypothesis B:** the bake explicitly computes direct light at the probe using a shadow ray, and this code path has a missing factor of 2 (e.g. `* 0.5` somewhere unintentional). Less likely because the same code would underrun Sponza too.
- **Hypothesis C:** the bake under-emits the *floor's* outgoing radiance because the bake's first-bounce hit-evaluation drops the cosine term or solid-angle weighting in a way that compounds with the point-light geometry.

Hypothesis A is the most consistent with the data: directional uniformly illuminates surfaces, so probe rays hit lit pixels at a high rate; point light illuminates a tiny patch, so probe rays mostly hit dark pixels.

## Improved next direction

Stage 11d targets the bake's direct-light handling. The plan §6 hypothesis tree narrows to:

1. **Stage 11d.A (test Hypothesis A): point-light area scaling probe.** Increase Cornell's point-light brightness intensity to compensate for its small effective hit probability, and see if cascade `ratio_self` rises toward 1.0. If a 2× intensity bump fixes the cascade ratio to ~1.0 (and PT also shifts by 2×, ratio preserved), Hypothesis A is confirmed. If PT scales but cascade doesn't, A is falsified and B/C take over.
2. **Stage 11d.B (test Hypothesis B): shader-source read of `radiance_3d.comp`.** Look for any direct-light computation at probe with a missing/extra factor of 2.
3. **Stage 11d.C: code-read of first-bounce evaluation in radiance_3d.comp.** Look for cosine / solid-angle / area-light handling in the bake.

11d.A is the cheapest (1 capture, no source change). Recommend doing 11d.A first.

The cascade bake's **direct-light handling under point lights** is the locus of the 2× under-emit. Until that's fixed, no amount of consumer-side tuning will close Cornell's mode-0 RMS gap (Stage 10 verdict). Hybrid was masking the same bug at the consumer stage and only got 17% of the way back — Stage 11c shows the fix has to be upstream in the bake.

## Decision

`LIGHT_TYPE_DOMINANT`. Stage 11c closes with a sharp localization: the bug is in the bake's handling of small/point lights. Stage 11d.A (point-light intensity scaling probe) is the cheapest first follow-up. No source code changes in this stage beyond CLI plumbing that survives `loadOBJMesh`.

**Engine source changes in this stage:**
- `src/main3d.cpp` — `--light-direction=` now stores values for post-load re-application; new post-load re-apply block. Total ~15 lines. No render-path changes. Reviewable as a self-contained CLI-plumbing fix even if Stage 11c's verdict is later revised.
