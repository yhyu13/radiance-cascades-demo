# Reply to Review 27 — Phase 13 Quality Lift Plan Review

**Date:** 2026-05-02

All four findings accepted. 13c is removed from the plan. 13a and 13b are corrected
in-place. The performance conclusion is weakened.

---

## Finding 1: Part 13c is a no-op with the current 4-cascade formula — accepted, 13c removed

The review is correct. The live stagger loop at [src/demo3d.cpp:1224](../../../../../src/demo3d.cpp):

```cpp
int interval = std::min(1 << i, staggerMaxInterval);
```

For C3 (i=3): `1 << 3 = 8`. With any `staggerMaxInterval >= 8`, `std::min(8, N) = 8`
regardless of N. Setting the UI to 16 or 32 changes no runtime behavior — C3 still
updates every 8 frames.

The plan confused "the slider cap limits the max" with "raising the cap changes the schedule."
They are decoupled: the per-cascade base interval is `1<<i`, which for i=3 is permanently 8.
The slider only matters when `staggerMaxInterval < 8`.

**To actually give C3 a longer interval** the formula itself must change — e.g., a
per-cascade configurable interval array:

```cpp
// per-cascade overrides, C3 updateable every 32 frames:
static const int kIntervalTable[] = { 1, 2, 4, 32 };
int interval = std::min(kIntervalTable[i], staggerMaxInterval);
```

This is a real code and UI change, not a slider extension. It belongs in a future phase
when there is a concrete reason to extend C3's interval (e.g. a dynamic scene where C3's
staleness matters more). For the static Cornell box it is pointless either way.

**Doc update:** 13c is removed from `phase13_quality_lift_plan.md`. Its intent
(reducing far-cascade cost) is noted as correctly deferred to a phase with a per-cascade
interval table design.

---

## Finding 2: 13a convergence math used N=8 against a live default of N=4 — accepted, doc corrected

The review is correct. The live constructor defaults are:

```cpp
, jitterPatternSize(4)   // NOT 8 as the plan stated
, jitterHoldFrames(1)
, temporalAlpha(0.05f)
```

The plan's "8-position cycle" narrative was based on Phase 11's recommended default, not
the value actually in the code. The corrected convergence picture for the live defaults:

- N=4, α=0.05: EMA half-life ≈ 13.5 frames; pattern cycles in 4 frames (1 frame/position)
- Phase 11 recommendation for N-tap averaging: `α ≈ 1/N = 0.25` at N=4
- Current α=0.05 is 5× below the Phase 11 recommendation for the actual live N

So the corrective direction (raise α, raise jitter scale) is still correct — the problem
is more acute than stated (5× off for N=4, not the ~2.5× implied by comparing 0.05 to
1/8=0.125).

**Updated 13a targets:**

| Member | Old default | New default | Rationale |
|---|---|---|---|
| `probeJitterScale` | 0.05 | 0.25 | Phase 11 recommended minimum; ±0.125 cell breaks grid aliasing |
| `temporalAlpha` | 0.05 | 0.20 | Close to `1/N=0.25` at N=4; slightly conservative to reduce noise |
| `jitterHoldFrames` | 1 | 2 | Hold each of 4 positions 2 frames; 8-frame full cycle matches stagger interval |

`jitterPatternSize` stays at 4 (not increased to 8) — the 4-tap pattern at dwell=2 gives
an 8-frame cycle that aligns with the stagger=8 schedule naturally.

**Doc update:** corrected in `phase13_quality_lift_plan.md`; convergence math rebuilt
for N=4.

---

## Finding 3: 13b overconfident geometric diagnosis — accepted, claim corrected

The review is correct on both sub-points:

**Sub-point A — normals are perpendicular, not similar.** The back wall has normal ~(0,0,-1)
and the ceiling has normal ~(0,-1,0). Their dot product is 0, so `cosDiff = 1.0` — the
maximum value. The live bilateral already computes:

```glsl
float dNormal = cosDiff / max(uNormalSigma, 1e-4);
float wNormal = exp(-0.5 * dNormal * dNormal);
```

At `cosDiff=1.0`, even a generous `uNormalSigma=0.5` gives `wNormal ≈ exp(-2) ≈ 0.14` —
the bilateral already strongly downweights samples across the wall/ceiling seam. The
claim that depth and normal stops "cannot distinguish" these surfaces was wrong.

**Sub-point B — blur is not the root cause.** The seam softness identified in the burst
analysis is more likely from the GI probe interpolation itself (trilinear C1-discontinuity,
the "Type A" banding documented in `phase9c_probe_spatial_banding.md`) rather than the
bilateral blur over-smoothing. The bilateral is already stopping at the geometric seam.

**Corrected 13b claim:** A luminance edge-stop remains worth adding, but for a narrower
reason than stated: within-plane tonal transitions (e.g. bright bounce spot on the back
wall fading to darker regions at the same depth and normal as the center). The existing
depth+normal stops handle inter-surface boundaries; the luminance term handles
intra-surface tonal variation.

**The wall/ceiling seam** is a probe interpolation issue (Type A), not addressable by
the bilateral. If seam sharpness is a goal, the correct tools are higher probe resolution
(48³), tricubic interpolation (both deferred), or GI-only edge-preserve by clamping
the bilateral radius near detected seams — none of which are in Phase 13.

**Doc update:** 13b's geometric claim removed; replaced with "within-plane tonal
transitions not caught by depth/normal stops."

---

## Finding 4: Performance conclusion too strong for one CPU-side measurement — accepted, weakened

The review is correct. `cascadeTimeMs` is a CPU wall-clock around `updateRadianceCascades()`,
not a GPU pipeline timer. The 0.091ms figure is useful as a relative comparison between
settings within the same run, but it is not a robust cross-setting or cross-frame
performance truth.

**Doc update:** the performance verdict is softened from "already fast — throughput is no
longer a concern" to "one burst run suggests manageable throughput at current settings;
not a settled performance conclusion."

---

## Summary

| Finding | Status | Action |
|---|---|---|
| 13c no-op — `std::min(1<<i, N)` caps C3 at 8 regardless | Accepted — real formula error | 13c removed; per-cascade interval table noted as the correct future design |
| 13a used N=8 against live N=4 defaults | Accepted — narrative built on wrong N | Corrected: α target raised to 0.20 (`≈1/N` at N=4), dwell raised to 2 for 8-frame cycle |
| 13b geometric claim wrong — wall/ceiling normals perpendicular | Accepted — overclaimed | 13b restated: luminance stop for within-plane tonal transitions; seam softness is probe interpolation (Type A), not bilateral |
| Performance conclusion overstated from one CPU-side measurement | Accepted — too strong | Weakened to "current-observation" language |
