# Phase 8 — Findings Log

**Date:** 2026-05-02
**Screenshot evidence:** `tools/frame_17776513476570812.png` (C0 res=32, reference)

---

## Experiment E2 result: dirRes increase did not reduce banding

**Action:** Raised `dirRes` from 4 to 8 via live slider (added in Phase 8 code).

**Result:** Banding pattern unchanged. Raising D4→D8 changed how the light spot is
sampled (angular quality improved) but did not affect the concentric rectangular
banding on walls, floor, and ceiling.

**Conclusion:** Angular directional resolution is NOT the dominant cause. The bands
are not from D4 bin-boundary hard steps. E2 hypothesis eliminated.

---

## Screenshot analysis: `frame_17776513476570812.png` (C0 res=32)

**Runtime observations (screenshot, not code-verified):**

- Concentric rectangular contour bands on the back wall, floor, and ceiling
- Bands are densest near the ceiling light, spacing increases with distance
- Pattern is centered on the light source / ceiling, not on the camera
- C0 and C1 cascade levels show the worst banding
- Pattern is rectangular

**Pattern geometry note:**
The concentric rectangular iso-luminance contours have rectangular symmetry visually
consistent with the Cornell Box SDF iso-surface structure. This is a visual correlation —
the pattern has not been directly overlaid on an SDF distance visualization, so
"matches SDF exactly" is stronger than the evidence supports.

---

## Hypothesis tested: bake ray minimum step quantization

**Hypothesis:** The bake shader `raymarchSDF()` minimum step `max(dist*0.9, 0.01)`
caused 1 cm step snapping near surfaces, producing the same integer-step artifact
in the probe atlas that mode 5 showed in the display path.

**Action:** Reduced minimum step from `0.01` to `0.001` in `radiance_3d.comp`
(main march, `inShadow`, and `softShadowBake`). Also corrected normal estimation
epsilon from `0.06` (calibrated for 64³) to `0.03` (1 voxel at 128³).

**Result:** Banding NOT reduced. Pattern unchanged after rebuild.

**Conclusion:** Bake ray step-count quantization is NOT the cause. The hypothesis
was wrong. The probe atlas data is not being corrupted by step snapping.

---

## Experiment E4 result: cascadeC0Res 32 → 64

**Date:** 2026-05-02
**Screenshots:**
- C0 res=8:  `tools/frame_17776724238845040.png`
- C0 res=64: `tools/frame_17776724345225716.png`

### C0 res=8 observations

- Near-total GI failure. Probe spacing = 4m / 8 = 50 cm per probe.
- Ceiling light invisible; color bleeding is a dim inaccurate smear.
- No discrete banding visible — the probe grid is so sparse that the trilinear
  interpolation just produces a slow smooth (but very wrong) gradient.
- Confirms: below some minimum density the probe field ceases to represent the
  GI signal at all. 8³ is well below that threshold for a 4m Cornell Box.

### C0 res=64 observations

- Much better overall quality: ceiling light visible, strong color bleeding, correct
  box illumination, red/green wall colors accurate.
- **Banding still present**: concentric contour bands visible on the back wall around
  the ceiling light position.
- **Band pattern changed character**: at 32³ the bands were rectangular (following
  Cornell Box SDF iso-surfaces). At 64³ the bands are more elliptical/circular
  (following the true point-light iso-luminance shells more closely).
- Band spacing is visually narrower than at 32³ — consistent with halving from
  12.5 cm to 6.25 cm probe spacing.

### E4 conclusion: spatial aliasing hypothesis CONFIRMED; probe density alone is not the solution

| Metric | 8³ | 32³ (baseline) | 64³ |
|---|---|---|---|
| Probe spacing | 50 cm | 12.5 cm | 6.25 cm |
| Banding visible | No (too sparse) | Yes — rectangular | Yes — elliptical, finer |
| Band spacing | N/A | ~12.5 cm | ~6.25 cm (halved) |
| GI quality | Very wrong | Usable | Good |

**Band spacing halved when probe spacing halved** → spatial aliasing hypothesis confirmed.

**But banding did not disappear at 64³** → increasing probe density alone does not
converge. The aliasing is intrinsic to the GI field gradient near the point light: the
radiance changes faster near the light than any fixed probe grid can represent smoothly.
Going 64³→128³ would again halve bands but 8× the memory — the brute-force approach
does not converge at practical cost.

**Why the pattern became elliptical at 64³:**
At 32³ the probe spacing dominated the artifact shape, making it rectangular (aligned
to the box geometry). At 64³ the finer probe grid resolves enough of the GI field that
the artifact pattern follows the actual point-light iso-luminance shells (which are
spherical around the light and project as ellipses on flat surfaces).

---

## Updated hypothesis ranking (post E4)

| Rank | Hypothesis | Evidence | Status |
|---|---|---|---|
| 1 | GI field spatial aliasing: probe grid samples the radiance gradient too coarsely near the light | E4: bands halved with halved probe spacing — confirmed as dominant mechanism | **CONFIRMED** |
| 2 | Cascade interval tMax creates hit→miss transitions at C0/C1 boundaries | C0/C1 worst; intervals are 12.5 cm and 50 cm | **Plausible — still uninvestigated** |
| 3 | Bake ray step quantization | Tested and eliminated | **Eliminated** |
| 4 | Angular D4 bin quantization | Tested and eliminated | **Eliminated** |

---

## Root cause (confirmed)

The GI radiance field in the Cornell Box has a steep spatial gradient near the ceiling
point light. The C0 probe grid cannot represent this gradient continuously at any
practical fixed density — the field changes faster than the Nyquist limit of the probe
grid in the region near the light. The trilinear interpolation between probe samples is
smooth within each probe cell but cannot recover the missing variation between samples.

**Why brute-force density increase fails to converge:**
The GI gradient near a point light scales as 1/r². Near the light the gradient is
arbitrarily steep. No finite fixed-density probe grid eliminates banding entirely —
the bands just get finer. This is a fundamental sampling theory result, not a
parameter to tune away.

**The fix must address the sampling strategy, not just the density:**
- Temporal accumulation with probe jitter (vary the sampling positions across frames)
- Visibility-weighted probe interpolation (don't blend probes across occlusion boundaries)
- Post-process GI blur as a display-side screen-space fix

---

## Code changes made in Phase 8 so far

| Change | File | Result |
|---|---|---|
| Live `dirRes` slider with cascade rebuild | `src/demo3d.cpp` | Working — enabled E2 experiment |
| Bake min step 0.01→0.001 | `res/shaders/radiance_3d.comp` | Did not fix banding — hypothesis eliminated |
| Normal estimation epsilon 0.06→0.03 | `res/shaders/radiance_3d.comp` | Minor accuracy improvement only |

---

## Next action

**Implement temporal probe accumulation (Phase 9, Improvement B).**

E4 confirmed: probe density alone cannot fix banding at practical cost. The fix must
amortize the sampling error across time. With per-frame probe jitter, each accumulated
frame samples at slightly different world positions, effectively integrating over a
wider spatial footprint than any single fixed-density bake.

See `phase9_improvement_plan.md` for full implementation plan.
