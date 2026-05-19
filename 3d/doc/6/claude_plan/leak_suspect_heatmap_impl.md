# Leak-Suspect Heatmap — Render Mode 14

**Date:** 2026-05-18
**Trigger:** User asked: "now toggle anti leak seems no effect, then how do see potential leaks? Just by eyes? Can we do leak detection by some pseudo ground truth judgement?"
**Status:** Shipped. New render mode 14 visualizes per-pixel atlas leak. Cornell A/B (OFF vs Phase 3 v3) shows visible reduction in red intensity confirming Phase 3's effect at the atlas level.

---

## What it shows

For each pixel, the heatmap visualizes the **leak potential** = the radiance that Phase 2's render-side α-gate hides from display:

```
leak_potential = sum_over_bins(atlas[bin].rgb * wcos * (1 - atlas[bin].α))
```

- `atlas[bin].rgb`: the radiance the bake stored in this bin
- `wcos = max(0, dot(bdir, normal))`: hemisphere weight (forward-facing bins only)
- `(1 - atlas[bin].α)`: how "occluded" the bin is per Phase 2 (α=0 means "opaque", α=1 means "transparent / visible")

If a bin is marked occluded (α≈0) but ALSO carries significant `rgb`, that's atlas leak. Phase 2's display formula `w = wcos * a.a` gates it out → invisible in final render. The heatmap shows what's hiding.

### Color scale

- **Green** = no leak (atlas has no radiance in occluded bins facing this pixel's normal)
- **Yellow** = some leak
- **Red** = strong leak (atlas stored bright radiance in directions marked occluded for this surface)

**Sqrt scale** (added 2026-05-18 rev): shader applies `sqrt(leak_potential / divisor)` so that small reductions near saturation produce visible color shifts. Without sqrt, divisor-tuned-to-saturate gives "always red" with no toggle responsiveness; sqrt-with-divisor-0.5 gives a useful range where Phase 3 ON/OFF visibly shifts color across ~42k pixels in Cornell.

**Divisor slider**: exposed in GUI when mode 14 is selected (logarithmic range 0.001 — 1.0). Default 0.5. Lower = more sensitive to subtle leak. Higher = needs stronger leak to turn red. CLI/uniform: `uLeakHeatmapDivisor`.

---

## How to use

CLI: `--render-mode=14`
GUI: Render Mode combo → "14 LeakSuspect (atlas radiance in α=0 bins)"

A/B workflow:
1. Set mode 14 in GUI.
2. Toggle WeightedSample OFF — observe the red areas.
3. Toggle WeightedSample ON — see the red reduce in the areas Phase 3 affects.

Equivalent CLI:
```
build/RadianceCascades3D.exe --render-mode=14 --screenshot=off.png --exit-frames=300
build/RadianceCascades3D.exe --use-weighted-sample=1 --render-mode=14 --screenshot=on.png --exit-frames=300
```

---

## Measured results

### Default Cornell box

**With linear scale, divisor 0.5 (rev 1):**

| Mode | Red pixels |
|---|---:|
| OFF | 163,390 |
| Phase 3 v3 ON | 152,541 |
| Delta | −6.6% |

**With sqrt scale, divisor 0.5 (rev 2, current default):**

| Mode | Red pixels | Yellow pixels |
|---|---:|---:|
| OFF | 299,018 | 68,861 |
| Phase 3 v3 ON | 294,872 | 72,915 |
| **Per-pixel diff** | mean 0.92 | max **178**/255 |
| **Pixels with >20 total diff** | — | **42,288** |
| **Pixels with >50 total diff** | — | **19,806** |

The sqrt-scaled rev surfaces a much bigger per-pixel response (42k pixels visibly shift, max 178/255 = 70% per-pixel change) even though the binary "red pixel count" change is smaller. The sqrt mapping spreads the contrast across the perceptually-relevant range; the linear scale lost most of Phase 3's effect to saturation.

### cornell-orig-alcove (alcove view)

| Mode | Red pixels |
|---|---:|
| OFF | 440,785 |
| Phase 3 v3 ON | 414,646 |
| **Delta** | **−26,139 (−6.0%)** |

Visually: alcove interior (left side of frame) is solid red in both, but ON shows slight reduction. The right side (lit area) is mostly green in both — confirming the heatmap correctly identifies the geometrically-occluded region as the leak source.

---

## Why this is a pseudo-ground-truth signal

The heatmap is **not** a comparison against a ground-truth render. It's an INTROSPECTION of the atlas: "what radiance does the atlas carry in bins that Phase 2 marks as occluded?" Two facts are independently true:

1. **A bin marked occluded should not carry radiance.** If it does, that's leak by definition (something went wrong upstream in the bake — likely upper-cascade contribution flowing past a wall).
2. **Phase 2's render-side α-gate hides leak from display by multiplying by α.** This means leak's visible impact is zero on output, but the leak still exists in the atlas.

Mode 14 surfaces fact #1. The signal is "this many pixels have a probe whose atlas carries leak content in this pixel's hemisphere." Phase 3 reduces the magnitude of that leak at the atlas level; the heatmap shows the reduction.

**What it doesn't tell you:**
- Whether the reduction is enough (no absolute scale; "less red" is the only signal)
- Whether Phase 3's reduction is "correct" (some atlas content marked occluded is GENUINELY radiance from open-volume far-field that shouldn't be there in occluded bins; some is legitimate cascade-merge interpolation that the α-gate over-rejects)
- Whether other algorithms (path-traced reference, etc.) would be brighter or darker

For absolute calibration, the [bake-leak-test JSON metric](../../tools/) is the quantitative companion (counts α<0.001 bins facing the light in alcove-positioned probes; tracks how that count changes with Phase 3 toggle).

---

## Limitations

1. **Heatmap is per-pixel hemisphere-summed**: a pixel facing AWAY from the leak source can show low red even if the atlas has heavy leak in the opposite direction. Tilt the camera to see leak from different angles.

2. **Sky/surface conflation (critic-16 W2 false positive)**: Phase 2's α encoding uses **α = 0 for BOTH surface hit AND sky exit**. The heatmap formula `sum(rgb * wcos * (1 - α))` can't distinguish them — sky bins flag as red leak suspects even though sky radiance is not "leak through a wall." In scenes with large open areas (Cornell box top opening, Sponza outdoor, any scene with --use-env-fill ON), the heatmap **over-reports leak** with sky contributions. **Workaround**: visually exclude regions of the heatmap that correspond to sky-facing surfaces (top of Cornell box, vault openings). **True fix** would require restoring a sky sentinel in `.a` (e.g., α = -1 for sky, 0 for surface, 1 for miss — see critic 09 W3 / critic 15 N3), which is a separate format change deferred indefinitely.

3. **EMA-α temporal smoothing affects α (critic-16 W4)**: after the 2026-05-15 temporal-stability fix, atlas α is no longer binary {0, 1} but a soft EMA-blended value. A bin that was hit in some recent frames and missed in others can have α = 0.3, giving `(1 - α) = 0.7`. The heatmap then flags it as "70% leak" even though the soft α reflects **temporal oscillation** (probe jitter sampled different SDF values), not geometric leak through a wall. **The heatmap is most accurate on fully-converged probes with stable α** (e.g., after several hundred frames of static camera). For moving cameras or just-after-rebake, transient soft α inflates the heatmap.

4. **Divisor (default 0.5)** (critic-16 W5): chosen empirically for Cornell-scale scenes (mean luminance ~0.2, leak potential in 0.05-0.5 range). Bright outdoor scenes saturate to all-red without slider adjustment; very dark scenes show all-green. **No auto-calibration implemented**. Heuristic: try `divisor ≈ scene_mean_luminance × 2.5`. The GUI slider lets you tune live; lowering it surfaces tinier leaks at the cost of brighter overall heatmap.

5. **Hemisphere weighting**: only counts bins with `wcos > 0` (forward-facing). Pixels on grazing-angle surfaces have fewer contributing bins → less leak signal even if the probe's atlas has leak in side directions.

## Notes on metric interpretation (critic-16 W3)

The rev 1 (linear) results showed:
- OFF: 163,390 red pixels; ON: 152,541 red pixels = **−6.6% absolute**

The rev 2 (sqrt) results showed:
- OFF: 299,018 red pixels; ON: 294,872 red pixels = **−1.4% absolute**, but
- **42,288 pixels with >20/255 per-pixel diff**, max diff **178/255**

These look contradictory: aggregate red-pixel reduction is *smaller* under sqrt, but per-pixel response is *larger*. **The reconciliation**: sqrt scaling expands the baseline red-pixel count (more pixels qualify as "red" at the same divisor because sqrt steepens the response curve near low values). A fixed absolute reduction in leak (say, −10,000 pixels crossing the red threshold) appears as a *smaller percentage* of a larger baseline. But the per-pixel diff metric (how much each pixel's RGB shifted between OFF and ON) is the **more meaningful signal under sqrt** because that's what makes the toggle visually perceptible.

The aggregate red-pixel count is a coarse classification metric; the per-pixel diff is a continuous magnitude metric. Sqrt scaling trades the former for the latter — appropriate for "visual responsiveness" but less useful for "what fraction of the frame has leak."

## Note on "Phase 3 v3" (critic-16 W6)

"Phase 3 v3" refers to the **trilinear.rgb + WeightedSample.a multiplier** variant defined in [visibility_phase3_impl.md § "v3 — Trilinear.rgb + visibility-fraction multiplier"](visibility_phase3_impl.md). Briefly: v3 uses sampleUpperDirTrilinear's unbiased radiance for `.rgb` and consumes sampleUpperDirWeighted's visibility-fraction `.a` only as the merge-formula multiplier. Cornell GI dim −0.67%, alcove leak reduction −11.2% on C0. Default OFF in this commit; toggle via `--use-weighted-sample=1` or GUI "WeightedSample bake-side visibility (Phase 3)" checkbox.

---

## Files added

- [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag): `sampleProbeDirWithLeak` (per-probe), `sampleDirectionalGIWithLeak` (trilinear over 8 probes), render mode 14 branch
- [src/demo3d.cpp](../../src/demo3d.cpp): added mode 14 to render-mode picker (combo + tooltip), bumped `static_assert(kModeCount == 15)`
- No new uniforms; no CLI flags (existing `--render-mode=14` works)

---

## Future improvements (not done)

1. **Side-by-side viewport**: render mode 0 and mode 14 in split-screen so the leak-suspect overlay aligns with the rendered scene visually
2. **Per-cascade decomposition**: heatmap separately for C0, C1, C2, C3 to identify which cascade contributes most leak
3. **Diff heatmap**: render mode 14 with Phase 3 OFF, then with ON, compute pixel diff = where Phase 3 actually attenuates
4. **Numeric overlay**: HUD text showing total leak_potential summed across the frame as a single scalar
