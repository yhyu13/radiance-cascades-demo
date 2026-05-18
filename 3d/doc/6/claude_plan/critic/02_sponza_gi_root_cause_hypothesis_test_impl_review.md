# Critic 02 — Sponza GI Root-Cause Hypothesis Test Impl Review

**Date:** 2026-05-12T14:15+08:00
**Target:** [sponza_gi_root_cause_hypothesis_test_impl.md](../sponza_gi_root_cause_hypothesis_test_impl.md)
**Preceding critic:** [01_sponza_gi_root_cause_hypothesis_test_plan_review.md](01_sponza_gi_root_cause_hypothesis_test_plan_review.md)

---

## Summary

The impl doc delivers clear, quantitative verdicts on 7 hypotheses and correctly identifies H6 (probe-to-surface visibility) as the dominant Sponza light-leak fix. The experimental methodology (density sweep + blur A/B + code landings) follows the revised plan faithfully. The shader code is included inline for verification. These are real strengths.

However, 11 findings below identify gaps in measurement completeness, correctness of the H6 implementation, and precision of the hypothesis verdicts. Three are HIGH severity.

---

## Findings

### C1 — C0 meanLum non-monotonicity unexplained (MEDIUM)

The meanLum table shows C0 values:

| C0 res | C0 meanLum |
|---:|---:|
| 16 | 0.0470 |
| 32 | 0.0513 |
| **48** | **0.0503** |
| 64 | 0.0513 |

C0=48 **dips below** C0=32 and C0=64. The doc claims "C0 saturates by 32" but the dip at 48 undermines the monotonic-saturation narrative. Possible causes:

- (a) Bake-time stochastic noise (random seed differs per run)
- (b) The `--exit-frames=900` settling window not producing identical convergence across runs
- (c) An interaction between density and NaN/Inf early-frame contamination (critic 01 F6)
- (d) C0=48 produces slightly different SDF voxelization granularity that changes surface coverage

The doc should either explain this non-monotonicity or acknowledge it as measurement noise that weakens the saturation claim. A 2× rerun at C0=48 (same CLI, different `--auto-rdoc` timestamp) would confirm or refute whether the dip is reproducible.

### C2 — anyPct data incomplete (MEDIUM)

Only C0=16 and C0=64 anyPct are reported (Section "Per-cascade anyPct"). C0=32 and C0=48 are missing. The plan called for 4 density captures; all 4 should have `probe_stats_*.json` output (since `--auto-rdoc` was added per critic 01 F4). Without C0=32 anyPct, the reader cannot verify whether C0 occupancy actually saturates or is simply unreported.

Additionally, C0=64 shows `C0 anyPct=0%` — if C0 is "saturated with surface energy" (meanLum 0.051), why is anyPct 0%? This likely means `anyPct` measures "probes with nonzero irradiance in ALL direction bins" while `meanLum` averages across all probes regardless. The discrepancy should be explicitly explained to prevent confusion about what "saturated" means.

### C3 — H6 hardcoded constants are scene-scale-dependent (HIGH)

`probeVisible()` uses fixed world-space constants:

```glsl
float t   = 0.05;              // start offset from surface
if (d < 0.002) return false;   // SDF hit threshold
t < dist - 0.005;              // end cutoff (avoid false-occlude at probe)
max(d * 0.9, 0.005);           // minimum step size
```

For `volumeSize = (4,4,4)` with SDF voxel resolution 64³, voxel size = 4/64 ≈ 0.0625. The constants (0.05, 0.002, 0.005) are all sub-voxel — reasonable for this scene.

But for a larger scene (`volumeSize = (20,20,20)`, voxel size ≈ 0.31), 0.05 is still sub-voxel but 0.002 is **far** sub-voxel — the SDF can't resolve surfaces thinner than ~0.31, so `d < 0.002` would never trigger and visibility would always return `true`. Conversely, for a tiny scene (`volumeSize = (1,1,1)`), 0.05 start offset is **5% of the entire volume**, potentially skipping thin occluders near the surface.

**Fix needed**: Make these constants derive from `uAtlasGridSize / vec3(uAtlasVolumeSize)` (i.e., voxel size) or a `uSDFVoxelSize` uniform. The recommendation section should include "make probeVisible constants scene-adaptive."

### C4 — H6 binary visibility discards partially-visible probes → over-darkening risk (HIGH)

`probeVisible()` traces **one ray** from surface to probe center and returns a binary yes/no. This zeroes the **entire probe contribution** if any geometry blocks the direct path, even if the probe has unoccluded directions through a narrow slit or gap.

`sampleProbeDir()` computes cosine-weighted irradiance over ALL direction bins. A probe behind a column that has a narrow visible slit would still contribute valid light from those unoccluded directions. The binary gate zeroes the entire directional budget, producing over-darkening at partial-occluder boundaries (e.g., column edges, arch openings).

The doc's "Visible impact: dramatic" section correctly notes "Some new pixelation visible (probe-quantization artifacts that were previously hidden by the leak smoothing)" but doesn't acknowledge that some of the new darkening may be **incorrect** — not shadow but over-aggressive occlusion masking.

**Recommended acknowledgment**: The doc should note this tradeoff explicitly: better shadow definition vs potential over-darkening at partial-occluder boundaries. The recommendation section should suggest per-direction-bin visibility (check each bin's ray individually, not just the center-to-surface path) as a quality refinement, noting it would further increase cost.

### C5 — H6 only masks render-time C0 interpolation, not bake-time data (MEDIUM)

The doc doesn't clearly distinguish between two distinct leak sources:

1. **Bake-time**: Each probe computes irradiance from geometry visible from its position. This is correct per-probe (a probe inside the corridor sees corridor geometry, a probe outside sees exterior geometry).

2. **Render-time**: `sampleDirectionalGI()` trilinearly interpolates across 8 C0 probes. If thin geometry falls between probes on opposite sides of a wall, the interpolation mixes irradiance from both sides → leak through the wall.

H6 fixes **only** the render-time interpolation by gating each C0 trilinear corner with `probeVisible`. This means upper cascade probes (C1/C2/C3) still bake without visibility checks. The inheritance path C3→C2→C1→C0 could carry irradiance data from probes that see geometry on the wrong side of walls — data that lands in C0 probes and is then zeroed by H6 at render time.

The doc should clarify that H6 **masks the symptom at render time** without fixing bake-time probe irradiance accuracy. For scenes where C0 probes inherit bad data from upper cascades (and those probes happen to be visible from the surface), the leak would still manifest. A more complete fix would add bake-time visibility checks in `radiance_3d.comp`'s cascade inheritance pass, but that's architecturally more invasive.

### C6 — No measured H6 perf numbers (MEDIUM)

The doc estimates "roughly double" the raymarch cost based on worst-case `8 corners × 16 SDF samples = 128 fetches per surface pixel`, but provides no actual timing data. Even a single frame-time comparison (before H6 vs after H6, same resolution and settings) would validate or refute the estimate.

The "128 fetches worst case" also doesn't account for early exits. In Sponza, most surfaces are near walls (thin geometry). A visibility ray toward a nearby probe that's on the same side of the wall will typically terminate in ~2-4 SDF steps (the ray stays in empty space). A ray toward a probe on the opposite side will terminate in ~1-2 steps (the SDF hits the wall immediately). Average case might be ~3-5 steps, not 16 — yielding ~24-40 fetches, not 128. An average-case estimate would be more informative for the recommendation section's perf optimization discussion.

### C7 — Capture count mismatch with plan (LOW)

The doc says "7 total captures" but the revised plan specified 6 (4 density + 2 blur A/B). The extra captures are H7 and H6 verification frames. The doc should explicitly note the delta: "6 planned captures + 1 H7 verification + 1 H6 verification = 8 total" (or clarify what counts as a "capture" — RenderDoc `.rdc` frames vs. manual screenshots). The current "7 total" is ambiguous and doesn't align with the plan's count.

### C8 — H1 "PARTIAL" verdict conflates two distinct phenomena (MEDIUM)

"PARTIAL — C0 saturates by 32; upper cascades benefit" conflates:

- **H1a**: C0 probe density saturation (C0 meanLum doesn't improve beyond 32³ → raising C0 density further is wasteful)
- **H1b**: Upper-cascade occupancy scaling (C3 goes from 0% → 96.88% anyPct → upper cascades genuinely benefit from higher base density)

These are different mechanisms: (a) is about surface-sampling resolution at the finest cascade, (b) is about geometric coverage of the coarsest cascade. The current "PARTIAL" verdict is ambiguous about which part is confirmed and which is rejected.

**Recommended split**:

| Sub-hypothesis | Verdict |
|---|---|
| H1a (C0 density) | REJECTED beyond 32 — marginal improvement 0.051→0.051 |
| H1b (upper cascade occupancy) | CONFIRMED — C3 anyPct 0%→96.88% |

This matters for recommendations — "raise default probe res" is ambiguous about whether it means C0 res or upper-cascade base density. The actual lever is `--cascade-c0-res` which controls ALL cascade grid sizes via the 8:1 hierarchical factor, so raising it does help upper cascades even if C0 itself saturates. But the doc should articulate this mechanism explicitly.

### C9 — H7 boundary-zero may over-darken edge surfaces in other scenes (LOW)

Returning `vec3(0)` for out-of-grid probes is correct in principle (no data should contribute). But for scenes where geometry fills the volume edges, surfaces near the boundary will have ~4 of 8 trilinear corners zeroed, cutting GI brightness by ~50%. The doc says "low impact at cam.md viewpoint" (Sponza's geometry is mostly interior) but doesn't discuss whether this is the right default behavior generally.

A nearest-valid-probe fallback (the old clamp behavior) would at least provide some irradiance near edges, avoiding a hard brightness cliff. The tradeoff is: old behavior duplicated edge-probe radiance (wrong), new behavior produces edge darkening (also wrong, but more physically justifiable). The doc should mention this tradeoff and note that for scenes where edge behavior matters (e.g., top-down views of Sponza ceiling), the H7 zeroing should be tested.

### C10 — No explicit A/B control statement for H6 capture (LOW)

The H6 capture (`sponza_h6_visibility_fix.png`) should be directly comparable to Phase 1's C0=64 blur=1 capture. The doc doesn't explicitly confirm identical CLI settings: same `--cascade-c0-res=64`, same `--gi-blur-radius=1`, same camera, same `--exit-frames=900`, same ambient floors. Adding a "H6 was captured at identical settings to Phase 1 C0=64, with only the raymarch.frag probeVisible+H7 changes differing" statement would make the comparison rigorous.

### C11 — probeVisible start offset may self-occlude at coarse SDF resolution (MEDIUM)

The sphere trace starts at `t = 0.05` from the surface position. If the SDF voxel resolution is coarse (e.g., 32³ for a 4×4×4 volume → voxel size ≈ 0.125), a surface point might be up to 0.0625 inside the SDF boundary (half a voxel). Starting the trace at only 0.05 could still be **inside** the SDF, causing `sampleSDF(surfacePos + dir * 0.05)` to return a near-zero or negative value → false self-occlusion (probeVisible returns false for probes that should be visible).

This interacts with C3 (hardcoded constants). The fix is the same: make `t` scale with voxel size (e.g., `t = max(voxelSize * 0.5, 0.05)` or derive from SDF resolution). The doc should note this interaction explicitly.

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| C3 | HIGH | probeVisible hardcoded constants are scene-scale-dependent |
| C4 | HIGH | Binary visibility discards partially-visible probes → over-darkening risk |
| C1 | MEDIUM | C0 meanLum non-monotonicity at C0=48 unexplained |
| C2 | MEDIUM | anyPct data incomplete (only 2 of 4 densities) |
| C5 | MEDIUM | H6 masks render-time leak; bake-time inheritance may still carry bad data |
| C6 | MEDIUM | No measured perf numbers for H6 cost |
| C8 | MEDIUM | H1 "PARTIAL" conflates C0 saturation vs upper-cascade scaling |
| C11 | MEDIUM | probeVisible t=0.05 start may self-occlude at coarse SDF resolution |
| C7 | LOW | Capture count "7" vs plan "6" — ambiguous |
| C9 | LOW | H7 zero at boundary may over-darken edge surfaces in other scenes |
| C10 | LOW | No explicit A/B control statement for H6 |

---

## Top 3 action items for reply

1. **C3 + C11 combined fix**: Make `probeVisible` start offset, SDF hit threshold, and minimum step size derive from a `uSDFVoxelSize` uniform (or `uAtlasGridSize / uAtlasVolumeSize`). Document these as scene-adaptive parameters.
2. **C4 acknowledgment**: Add a paragraph to the H6 section noting the over-darkening risk from binary visibility at partial-occluder boundaries, and add per-direction-bin visibility as a future quality refinement in the recommendations.
3. **C8 split verdict**: Replace "H1 PARTIAL" with explicit sub-verdicts H1a (C0 density: REJECTED beyond 32) and H1b (upper cascade occupancy: CONFIRMED), and explain that `--cascade-c0-res` is the lever that controls both despite C0 itself saturating.