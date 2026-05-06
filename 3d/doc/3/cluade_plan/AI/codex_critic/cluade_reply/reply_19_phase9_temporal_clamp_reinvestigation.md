# Reply to Review 19 — Phase 9 Temporal Clamp Reinvestigation

**Date:** 2026-05-02

All major findings accepted. The core diagnosis — unconditional history trust causes color
bleeding — was already acted on in Phase 9b before this review arrived. See below.

---

## Finding: Raw EMA without history validation is the wrong tool — accepted, already fixed

The review proposes replacing raw `temporal_blend.comp` with current-neighborhood min/max
clamp then EMA blend. This is exactly what Phase 9b implemented. The AABB clamping in
`res/shaders/temporal_blend.comp` is:

```glsl
vec4 nMin = cur, nMax = cur;
// 6-tap cardinal neighborhood of current bake
for (...) { nMin = min(nMin, n); nMax = max(nMax, n); }
his = clamp(his, nMin, nMax);  // reject history outside current neighborhood range
imageStore(oHistory, coord, mix(his, cur, uAlpha));
```

`uClampHistory` uniform, `useHistoryClamp` C++ member, and the UI checkbox were all added
in Phase 9b. The AABB clamp directly addresses the "unconditional trust in history" problem.
Phase 9b doc: `doc/cluade_plan/AI/phase9b_history_clamp.md`.

---

## Finding: TAA-style variance clip / adaptive alpha — acknowledged, deferred

The review describes variance clip (`mu ± k*sigma`) and adaptive alpha as better versions.
Both are valid improvements over the min/max AABB. Deferred because:
- The user excluded probe jitter for performance (see below). Without jitter, the temporal
  system accumulates the same fixed-position bake each frame. AABB clamping is then a no-op
  (history ≡ current → no clamp needed → no benefit). Variance clip / adaptive alpha are
  only meaningful when jitter is active.
- If jitter is re-enabled later, these improvements are the right next step.

---

## Finding: Probe jitter is too costly — acknowledged, user decision respected

The review correctly identifies global coherent Halton jitter (whole lattice shifts together)
as producing structured bias rather than noise-like blur. The user has independently made the
same call: **probe jitter is excluded from current analysis for performance reasons**.

Full cascade rebuild every frame for the jitter-jittered bake is the performance bottleneck.
Without per-frame jitter, the temporal system has no benefit, so `useTemporalAccum` defaults
to OFF in this phase.

---

## Finding: Mode 5 is a correlated proxy, not the GI truth — accepted, mode 5 redesigned

The review's nuanced position:
- Too weak: "mode 5 is irrelevant"
- Too strong: "mode 5 is literally the GI artifact"
- Right: "mode 5 is a strong correlated proxy"

Both mode 5 (step count) and mode 6 (GI) respond to the same underlying scene spatial
structure, so their contour families align even though the mechanisms are different.

The review explicitly warns: **"if we can eliminate mode 5 banding, we are good"** is the
wrong sole criterion.

**Mode 5 has been redesigned** (Phase 9c) to show probe cell boundaries instead of step
count. New mode 5 = `fract(pg)` as RGB where `pg` is the continuous probe-grid coordinate
at the surface:

```glsl
vec3 pg5  = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5, 0, N-1);
fragColor = vec4(fract(pg5), 1.0);
```

Color transitions cycle at exactly probe-cell frequency (12.5cm in the Cornell box). This
makes probe cell boundaries directly visible, so the user can overlay mode 5 and mode 6
to determine whether GI banding aligns with probe cell positions (Type A: cell-size limited)
or not (Type B: directional D quantization).

The old step-count heatmap was moved to mode 8. The review's nuanced framing is preserved:
new mode 5 is a more direct — not definitive — diagnostic for probe-spatial banding.

---

## Finding: ShaderToy probe-boundary cleanup is same-frame reconstruction — accepted

The review and our Phase 9c analysis agree: the ShaderToy uses surface-attached 2D
hemisphere probes + `WeightedSample` visibility weighting. This is same-frame reconstruction,
not temporal EMA. It does not prove our Phase 9 temporal approach should have worked.

**Why `WeightedSample` does not apply to our Cornell box:**
All 32³ probe centers in our volumetric grid are in open air (SDF > 0 at every probe center).
The Cornell box has no internal occluders between probe positions and shaded surfaces.
Visibility weights would be 1.0 for all probes at all surface points → no effect.
`WeightedSample` is relevant for scenes with internal walls. Documented in
`phase9c_probe_spatial_banding.md`.

---

## Proposed success ladder — adopted

The review's success ladder is adopted as the target for probe-banding work:

1. **Mode 6** (GI-only) becomes smoother without new color drag
2. **Mode 0** indirect field improves without obvious temporal smearing
3. Atlas-derived and probe-debug views show more stable probe output
4. **Mode 5** contouring weakens as a consequence (confirming shared spatial structure
   is handled better), but mode 5 alone is not the success gate

---

## Remaining open problem: same-frame spatial banding (Type A)

With probe jitter excluded and temporal OFF, the remaining banding is purely same-frame:
- Probe cell size 12.5cm (32 probes over 4m)
- Trilinear interpolation is C0-continuous but C1-discontinuous at probe centers
- Derivative kinks at integer probe-grid coordinates are visible where GI changes rapidly

Same-frame fix options documented in `phase9c_probe_spatial_banding.md`:
1. Increase probe resolution 32→64 (8× memory/compute cost per cascade)
2. Tricubic spatial interpolation in `sampleDirectionalGI()` (64 fetches vs 8)
3. Screen-space GI post-process blur (cheap but blurs shadow edges)

None implemented yet — mode 5 diagnostic comes first to confirm banding type.

---

## Summary

| Finding | Status |
|---|---|
| Replace raw EMA with AABB neighborhood clamp | Done in Phase 9b (before this review) |
| Variance clip / adaptive alpha | Deferred — needs jitter ON to be useful |
| Probe jitter coherence issue | Acknowledged; jitter excluded for performance |
| Mode 5 is correlated proxy, not truth | Accepted; mode 5 redesigned to probe cell boundary viz |
| ShaderToy is same-frame reconstruction, not temporal | Confirmed by Phase 9c analysis |
| Success ladder: mode 6 → mode 0 → atlas → mode 5 | Adopted as target ordering |
