# Phase 8 — GI Banding: Isolate and Reduce Cascade Probe Artifacts

**Date:** 2026-05-01
**Depends on:** Phase 7 findings summary (`phase7_findings_summary.md`)
**Goal:** Identify which component of the cascade GI reconstruction is the dominant
source of mode 0 banding, and apply targeted fixes.

---

## Context

Phase 7 experiments established:
- Display-path raymarching: surface hit positions are smooth (mode 7 proven). Not the cause.
- SDF path (analytic or texture): banding persists with both. Not the cause.
- **Working hypothesis:** mode 0 banding originates in cascade GI probe data —
  either angular (D4 bins) or spatial (C0 probe spacing) quantization.

The bake shader (`radiance_3d.comp`) marches rays from each C0 probe in 16 directions
(D4, `dirRes=4`). It writes results to the probe atlas. The display path samples
the atlas with cosine-weighted directional integration. Every step of that chain can
contribute banding.

---

## Experiment sequence

### E1 — Isolate angular vs spatial: toggle directional GI

**Action:** In the UI, switch "Directional GI" toggle OFF. Mode 0 now samples the
isotropic probe grid (spatial average) instead of the directional atlas.

**Observe:** Does the banding character change significantly?

| Result | Interpretation |
|---|---|
| Banding disappears or changes shape | Angular D4 quantization is a major contributor |
| Banding unchanged | Spatial probe spacing dominates; angular is secondary |
| Banding changes intensity only | Both contribute; intensity ratio is informative |

**No code change needed.** Toggle already exists in UI.

---

### E2 — Measure angular contribution: increase dirRes to 8

**Action:** Change `dirRes` from 4 to 8 in the cascade rebuild UI slider (or set as
default). Rebuild cascades. Compare mode 0.

D8 gives 64 bins vs 16 bins at D4, reducing angular error from 22.5°/bin to 5.6°/bin.

**Cost:** 4× cascade bake time (64 vs 16 directions per probe per dispatch).

**Observe:** Does the banding reduce proportionally to the angular bin count?

If banding is halved or more: angular resolution is the dominant cause.
If banding is unchanged: spatial probe grid is dominant.

**No shader change needed.** `uDirRes` uniform already drives the atlas layout.

---

### E3 — Measure spatial contribution: examine probe spacing banding frequency

**Action:** In mode 0, measure the spatial frequency of the banding bands (pixels per band → 
world-space distance per band). Compare to:
- C0 probe spacing: 4m / 32 = **12.5 cm per probe**
- C0 probe cell diagonal: 12.5 cm × √3 ≈ **21.6 cm**

If banding period matches probe spacing: spatial quantization confirmed.
If banding period is finer or coarser: not spatial probe grid.

**No code change needed.** Use a screenshot + ruler tool.

---

### E4 — Spatial fix: increase cascade probe count

If E3 confirms spatial probe spacing is dominant:

| Parameter | Current | Proposed | Memory impact |
|---|---|---|---|
| C0 probe grid | 32³ = 32,768 probes | 48³ = 110,592 probes | ~3× atlas |
| C0 probe spacing | 12.5 cm | 8.3 cm | — |

**Implementation:** Change `volumeResolution` (controls probe count) or add a separate
`probeResolution` parameter decoupled from SDF resolution.

This is a significant memory change — defer until E1/E2/E3 identify it as dominant.

---

### E5 — Angular fix: directional atlas bilinear interpolation

If E1/E2 confirm angular D4 quantization is dominant, and D8 is too expensive:

The atlas already supports bilinear interpolation between directional bins
(`uUseDirBilinear` in `radiance_3d.comp`). If it is not already ON by default,
enable it. This does not reduce angular resolution but smooths the hard bin boundaries.

**Check:** Verify `uUseDirBilinear` default state. If OFF, add UI toggle and test.

---

### E6 — GI output spatial filter (last resort)

If the banding cannot be reduced by probe count or angular resolution changes, apply
a spatial low-pass filter to the indirect lighting output before it is combined with
direct lighting in mode 0.

Options:
- Box filter over 3×3 probe neighborhood in the probe grid texture (blur in world space)
- Temporal accumulation (rolling average over N frames with jittered probe positions)
- Screen-space bilateral filter on the GI contribution (separate from direct lighting)

Cost: moderate (1 extra pass for spatial blur, or frame history for temporal).
Risk: blurs sharp GI boundaries (door frames, occlusion edges). Requires a threshold.

---

## Implementation priority

```
E1 (toggle, no code) → E2 (UI slider, no code) → E3 (measure, no code)
    ↓                       ↓
  angular dominant?       spatial dominant?
    ↓                       ↓
  E5 (bilinear)           E4 (probe count)
    ↓
  E6 if none of above sufficient
```

---

## Success criteria

| Criterion | Target |
|---|---|
| Mode 0 banding reduced to not-distracting | Banding not visible at normal viewing distance (no sharp rectangular contour lines in indirect field) |
| Mode 6 (GI only) banding reduced | Same — GI-only view should show smooth indirect field without rectangular steps |
| No regression in cascade blend quality | Mode 0 final render still shows smooth cascade boundary (no visible D4→D8 seam) |
| Performance within budget | Final render frame time ≤ 2× current (directional GI sampling already dominates) |

---

## Files likely to change

| File | Change |
|---|---|
| `src/demo3d.cpp` | Default `dirRes` change; probe count change; bilinear toggle |
| `src/demo3d.h` | New probe resolution parameter if decoupled from SDF |
| `res/shaders/radiance_3d.comp` | Bilinear atlas sampling (if not already active) |
| `res/shaders/raymarch.frag` | GI spatial filter pass (E6 only) |

---

## What does NOT need to change

- Display-path SDF sampling (mode 7 proved it is smooth)
- Analytic SDF toggle (diagnostic only, no production value for this problem)
- Smoothstep cascade blend weight (already applied in Phase 7 — addresses secondary seam)
- Mode 5 step count (retired as SDF diagnostic; keep for legacy but do not interpret as SDF quality)
