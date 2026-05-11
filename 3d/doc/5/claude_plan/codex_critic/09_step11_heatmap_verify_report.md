# Step 11 Heatmap Verification — GI Bounce Physical Correctness

**Date:** 2026-05-11
**Viewpoint:** `pos=1.0710,-0.0723,-0.3393  target=0.1212,-0.0812,-0.6520` (from `cam.md`)
**Build:** Release, codex 08 F1+F5 fixes applied (4-line lighting invalidation, mode 12 divisor retuned to `/0.5`)

---

## Executive Summary

The heatmap modes DO show spatial variation that matches geometric intuition — ceiling beams produce warm orange bounce, brick walls produce reddish-brown bleed onto nearby surfaces, and shadowed areas show low GI. However, **the GI is not physically correct** for three independent reasons:

1. **The `vec3(0.05)` ambient floor amplifies through cascade bounce**, making the GI appear much stronger than the real direct-lit contribution. Mode 6 + strip proves this: most camera-facing surfaces go near-black without the baked floor.
2. **Probe atlas NaN/Inf contamination**: cascade C0 shows `meanLum=NaN`, `maxLum=inf`; C2/C3 show negative mean luminance (-376, -320) with `maxLum=1024`. These are physically impossible values that indicate the radiance bake or temporal accumulation is producing corrupt output in some texels.
3. **Severely under-occupied cascades**: C0 `anyPct ≈ 3.5%`, C1 `≈ 0.02–5%` (depends on whether strip is on), C2 `≈ 0.4%`, C3 `≈ 0% surface / 100% sky`. Most probes are not finding surface hits, which means the cascade radiance signal is extremely sparse.

---

## Captures Taken (all at cam.md viewpoint)

| File | Mode | Purpose |
|---|---|---|
| `step11_verify_heatmap11_vis.png` | 11 | Visible-GI heatmap (`length(albedo * indirect) / 0.1`) |
| `step11_verify_heatmap12_raw.png` | 12 | Raw-GI heatmap (`length(indirect) / 0.5` — retuned) |
| `step11_verify_heatmap13_frac.png` | 13 | GI-fraction heatmap (`indirectColor / totalColor`) |
| `step11_verify_mode6_gi.png` | 6 | GI-only (with 0.05 baked in) |
| `step11_verify_mode6_strip.png` | 6 + strip | GI-only with ambient floor stripped from bake |
| `step11_verify_heatmap13_strip.png` | 13 + strip | GI-fraction with stripped bake |
| `step11_verify_mode9_direct.png` | 9 | Direct without ambient floor |
| `step11_verify_mode0_baseline.png` | 0 | Final composite baseline |
| `step11_verify_heatmap13_longrun.png` | 13 (600 frames) | Settled cascade long-run |

RenderDoc frame captured: `tools/captures/rdoc_frame_frame401.rdc` (at viewpoint, mode 0).

---

## AI Triage Analysis (from burst captures)

Two independent burst analyses were performed (at frames ~8s and the long-run at 600 frames). Key findings:

### Where GI IS doing real work (color bleed visible):

- **Ceiling/upper-right corner**: warm reddish wash from orange/copper ceiling beams bouncing onto adjacent surfaces. This is genuine **color bleed** — the brick/ceiling albedo is preserved through the cascade bounce.
- **Far doorway**: pinkish glow leaking out from behind the doorway arch. This is genuine indirect bounce from lit surfaces beyond the arch.
- **Left wall floor area**: cool/neutral grey contribution lifting shadowed floor under the railing. This is real bounce from the ceiling light hitting the floor.

### Where GI is NOT doing real work:

- **Right foreground blob** (large brown shape at x≈1000-1280): reads black even in mode 6, suggesting occlusion or under-sampled probes.
- **Left wall near camera**: almost no indirect contribution — stays dark. Geometrically correct for this viewpoint (no direct light hits this wall from the camera angle).

### Banding artifacts:

- **Ceiling beams**: hard stair-stepped blocks ~30-50px wide, matching probe grid footprint (baseRes=32, volumeSize=4 → ~8 voxel ticks). This is **probe-spatial banding** (Type A from mode 8 analysis), not directional binning.
- **Far doorway**: sharp rectangular dark/light boundary at the cascade boundary (C0→C1 handoff, consistent with C0 `anyPct≈3.5%` while C1 is near-empty).
- **No rotational pinwheel pattern**: `dirRes=8` is not the dominant artifact source.

---

## Probe Statistics (Quantitative Evidence)

### Vanilla (no strip) — early frames (~frame 8)

| Cascade | anyPct | surfPct | skyPct | meanLum | maxLum | variance |
|---------|--------|---------|--------|---------|--------|----------|
| C0 | 2.2% | 0 | 0 | **NaN** | **inf** | **NaN** |
| C1 | 77.6% | 0 | 0 | NaN | 213.3 | NaN |
| C2 | 0% | 0 | 0 | -375.3 | 0 | -0.1 |
| C3 | 0% | 0 | 51.6% | -320.5 | 6.2e-6 | 17051.5 |

### Vanilla (no strip) — settled frames (~frame 200+)

| Cascade | anyPct | surfPct | skyPct | meanLum | maxLum | variance |
|---------|--------|---------|--------|---------|--------|----------|
| C0 | 4.3% | 0 | 0 | **NaN** | **inf** | **NaN** |
| C1 | 0.024% | 0 | 0 | -1.37 | 54.2 | 4104 |
| C2 | 0.39% | 0 | 0 | -376.1 | 1024 | 24221 |
| C3 | 0% | 0 | 100% | -320.5 | 6.2e-6 | 17051.5 |

### With strip — settled frames

| Cascade | anyPct | surfPct | skyPct | meanLum | maxLum | variance |
|---------|--------|---------|--------|---------|--------|----------|
| C0 | 1.5% | 0 | 0 | NaN | inf | NaN |
| C1 | 0% | 0 | 0 | 0 | 0 | 0 |
| C2 | 0.2% | 0 | 0 | -373 | 0.002 | 820 |
| C3 | 0% | 0 | 100% | -320.5 | 6.2e-6 | 17051.5 |

### Cascade meanLum convergence (long-run, mode 13, 600 frames)

After initial NaN frames, the cascade 4c reduction stabilizes:
```
C0=0.01714  C1=0.01610  C2=0.01405  C3=0.00831
```

These are **extremely low** luminance values (~0.01-0.02 per channel). For Sponza's mid-tone (~0.5) albedo surfaces, `albedo × 0.02 ≈ 0.01` per channel of real GI bounce — barely above the `0.05 × 0.5 = 0.025` ambient floor contribution.

---

## Physical Correctness Assessment

### ✅ What IS physically correct:

1. **Color bleed direction**: Ceiling beams (orange/copper albedo) produce warm reddish bounce on adjacent surfaces. Brick wall produces reddish-brown bleed. This matches the Lambertian bounce formula `L_out = albedo_dest × integral(L_in × cos θ)`.
2. **Spatial coherence**: GI is stronger near lit surfaces (ceiling, doorway) and weaker in deep shadows (left wall, foreground occlusion). This matches geometric ray-tracing intuition.
3. **Mode 6 + strip vs. mode 6 without strip**: The difference proves that the 0.05 floor amplifies through cascade bounce. Camera-facing surfaces go near-black with strip, confirming the Step 11 Outcome A diagnosis.

### ❌ What is NOT physically correct:

1. **NaN/Inf contamination in C0**: `meanLum=NaN`, `maxLum=inf` in cascade 0 (the finest-resolution cascade, which should contain the most surface hits). This means the radiance bake shader or the temporal EMA accumulation is producing corrupt texels that propagate NaN through the reduction pass. **This is a bug that needs fixing before any GI tuning.**

2. **Negative mean luminance in C2/C3**: `meanLum = -376` and `-320` are physically impossible (luminance cannot be negative). The `reduction_3d.comp` shader averages atlas bin RGB values, so negative meanLum means the atlas contains texels with very large negative RGB values. Possible sources:
   - Uninitialized `probeAtlasTexture` memory (if temporal accumulation reads stale history before first bake seeds it)
   - The `historyNeedsSeed` path not fully clearing NaN from the history texture before the EMA blend
   - The fused EMA `mix(hist, vec4(rad, hit.a), alpha)` propagating NaN from `hist` when `alpha < 1.0`

3. **maxLum = 1024 in C2**: A single probe having luminance 1024 (over 1000× brighter than the mean) suggests a numerical explosion — possibly from NaN propagation through `imageAtomicMin` or from a shader arithmetic error in the bake.

4. **Severely under-occupied cascades**: C0 `anyPct ≈ 3.5%` means only ~3.5% of near-field probes have luminance above 1e-4. For Sponza (which fills most of the 128³ volume), this is far too low. The cascade bake should be hitting surfaces for ~50-80% of probes at C0 resolution. Possible causes:
   - `probeJitterScale = 0.06` is too small — probes at the regular grid positions miss thin geometry (ceilings, walls)
   - The `blendFraction` or `tMin/tMax` range computation may be too restrictive, causing probes to terminate before reaching surfaces
   - `c0MinRange = 1` (1 cm minimum range) may be too large for the 128³ volume where voxels are ~3cm apart

5. **No color bleed magnitude**: Even where color bleed IS present (ceiling→wall), the magnitude is too small. The cascade converged mean luminance of ~0.01-0.02 means `indirectColor ≈ 0.01 × albedo` — roughly 2% of surface brightness comes from real GI bounce. In a correct Lambertian renderer, the bounce from a brightly-lit ceiling onto a shadowed floor should contribute ~10-30% of the total illumination, not 2%.

---

## Recommended Actions (Priority Order)

### P0 — Fix NaN/Inf contamination in probe atlas (blocking)

The NaN/Inf values in C0 `probeGridTexture` propagate through the reduction pass and corrupt all downstream statistics. Before any GI tuning or heatmap calibration can be meaningful, the probe atlas must produce clean float values.

**Investigation steps:**
1. Add a `glFinish()` + `glGetTexImage` readback of `probeAtlasTexture` BEFORE the reduction dispatch to confirm whether NaN originates in the bake or the reduction.
2. Add NaN/Inf clamping in `radiance_3d.comp` at the `imageStore(oAtlas, ...)` output: `vec4 rad_clamped = clamp(rad, vec4(0.0), vec4(100.0)); imageStore(oAtlas, atlasTxl, mix(hist, rad_clamped, uTemporalAlpha));`
3. Add NaN/Inf clamping in `reduction_3d.comp` at the `imageStore(oRadiance, ...)` output: `vec4 avg_clamped = vec4(clamp(avg, vec3(0.0), vec3(100.0)), 0.0);`
4. Seed `probeAtlasHistory` and `probeGridHistory` with zeros (not uninitialized memory) before the first temporal accumulation frame.

### P1 — Increase probe occupation rate

With C0 at ~3.5% any-hit, the cascade is effectively empty. Most probes miss all geometry.

**Concrete change:** Increase `probeJitterScale` from `0.06 → 0.25` and raise `temporalAlpha` from `0.05 → 0.12`. This expands effective probe coverage by ~4× while keeping temporal convergence reasonable. (This matches the AI triage recommendation.)

### P2 — Reduce `c0MinRange` to allow closer surface hits

`c0MinRange = 1` means C0 probes ignore surfaces within 1cm of the probe position. In a 128³ volume with 4-unit volumeSize, voxels are ~3cm apart. A 1cm minimum range causes probes in thin geometry (ceilings, walls) to miss the surface entirely.

**Concrete change:** Reduce `c0MinRange` from `1 → 0` (or at least `0.5`). This allows probes right next to surfaces to register hits.

### P3 — After P0-P2 are fixed, re-verify heatmap physical correctness

Once the NaN bug is fixed and probe occupation increases, the heatmaps should show:
- Mode 11 (visible GI): meaningful spatial variation with yellow/red near lit surfaces, green in deep shadows
- Mode 12 (raw GI): similar spatial pattern but without albedo modulation
- Mode 13 (GI fraction): GI > 50% in shadowed areas near colored walls, direct > 50% in directly-lit areas
- Mode 6 + strip: visible colored bleed (not near-black) where geometry supports real bounce paths

---

## Mode 12 Divisor Verification

The codex 08 F5 retune changed mode 12 from `/0.05` to `/0.5`. The capture `step11_verify_heatmap12_raw.png` uses the retuned divisor. The probe stats show `meanLum ≈ 0.01-0.02` per channel, which means `length(indirect) ≈ 0.03-0.06` (for 3 channels). With `/0.5`, the heatmap value `v ≈ 0.06-0.12`, mapping to mostly green (0.0-0.5 range) with some yellow near hotspots. This is appropriate for showing spatial variation in the low-magnitude regime.

However, if P1-P2 increase the probe occupation and the actual GI signal becomes stronger (e.g., `length(indirect) ≈ 0.3-0.5`), the `/0.5` divisor would need further retuning to avoid saturation. The divisor should be revisited after the P0-P2 fixes.

---

## Mode 13 + Strip Comparison

The `step11_verify_heatmap13_strip.png` shows the GI-fraction heatmap with the 0.05 floor stripped from the bake. Expected difference from `step11_verify_heatmap13_frac.png`:
- Areas where GI was mostly ambient amplification (right wall, floor) should show LOWER GI fraction with strip (because `indirectColor` drops while `directColor` stays the same)
- Areas where GI has real direct-lit bounce (left pillars) should show SIMILAR fraction
- The overall shift should be toward green (more direct-dominated) in previously red/orange areas

This comparison directly answers "how much of the GI fraction is real vs. amplified?"

---

## Summary

**The heatmap pipeline is architecturally correct and produces spatial patterns that match geometric intuition.** But the GI signal itself is not physically correct because:

1. NaN/Inf corruption in the probe atlas (P0 blocker)
2. Severely under-occupied cascades (P1)
3. The 0.05 ambient floor amplifying through bounce (confirmed by Step 11, deferred to Step 12)
4. Low real bounce magnitude (~2% of surface brightness vs. expected ~10-30%)

Fix P0 (NaN/Inf) first — it corrupts all downstream analysis. Then P1 (probe jitter + temporal alpha) and P2 (c0MinRange) to increase cascade occupation. After those fixes, re-verify the heatmaps and calibrate the GI fraction to confirm physical correctness.