# Phase 7 — Findings Summary

**Date:** 2026-05-01
**Status:** Experiments complete. Root cause narrowed to cascade GI data, not display-path raymarching.

---

## What was built in Phase 7

| Change | File | Status |
|---|---|---|
| Cascade blend weight: linear → smoothstep | `res/shaders/radiance_3d.comp:350-355` | Done |
| Constructor defaults: non-colocated, D-scaling, directional GI ON | `src/demo3d.cpp:116-121` | Done |
| Mode 7: ray travel distance heatmap | `res/shaders/raymarch.frag` | Done |
| `DEFAULT_VOLUME_RESOLUTION`: 64 → 128 | `src/demo3d.h:46` | Done |
| Analytic SDF toggle (checkbox + shader SSBO path) | `raymarch.frag`, `demo3d.cpp/h` | Done |

---

## Experiment results

### Experiment A: Analytic SDF toggle (display path)

**Setup:** Enable "Analytic SDF (smooth, no grid)" checkbox. Compare mode 5 and final render (mode 0) with toggle ON vs OFF.

**Result:**
- Toggle ON: edges become sharper (no trilinear blur), banding pattern unchanged.
- Toggle OFF (texture): slightly softer edges, same banding.

**Conclusion:** Banding is **not** caused by trilinear interpolation of the SDF texture. It is intrinsic to the Cornell Box SDF geometry — the iso-surfaces of `min(dist_to_walls, dist_to_boxes)` are naturally rectangular concentric shells. No SDF path removes this geometry.

---

### Experiment B: Mode 5 vs mode 7

**Setup:** Switch between mode 5 (integer `stepCount` heatmap) and mode 7 (continuous `t` heatmap) while looking at the same view.

**Result:**
- Mode 5: discrete rectangular banding bands, strong contour stepping.
- Mode 7: smooth continuous gradient, no discrete bands.

**Conclusion:** Mode 5 was a **flawed diagnostic**. The integer `stepCount` creates discrete color steps when adjacent pixels take 14 vs 15 iterations, even if their actual surface hit positions differ by only millimeters. The underlying ray `t` (continuous float, validated by mode 7) is smooth. Mode 5 does not measure SDF quality — it measures iteration count, which is a secondary effect.

**Mode 7 is the correct surface-distance diagnostic going forward. Mode 5 should not be used to draw conclusions about SDF accuracy.**

---

## Root cause revision

### What is NOT the cause of mode 0 banding

| Hypothesis | Status | Evidence |
|---|---|---|
| Trilinear SDF grid interpolation | **Eliminated** | Analytic toggle: banding persists unchanged |
| Integer step count discretization | **Eliminated from mode 0** | Mode 7: display-path `t` is smooth; step count doesn't affect mode 0 output |
| JFA approximation error | **Not applicable** | Analytic SDF path is active; JFA is future work |

### What IS the remaining source of mode 0 banding

The display-path surface hit positions are smooth (mode 7). Mode 0 banding must come from what is **sampled at those smooth positions** — the cascade GI data.

Two sub-hypotheses for cascade GI banding, not yet separated:

| Hypothesis | Mechanism | Experiment to run |
|---|---|---|
| C0 angular D4 quantization | 16 directions × 22.5°/bin → hard directional steps in indirect field | Toggle directional GI OFF (use isotropic grid); compare mode 0 |
| C0 spatial probe spacing | 32³ probes over 4m → 12.5 cm/probe → visible spatial banding | Increase cascade resolution or observe pattern spatial frequency |

### Partially addressed

| Source | Action taken | Remaining effect |
|---|---|---|
| Cascade blend kink at D4→D8 boundary | Smoothstep applied | Visual comparison pending — needs A/B screenshot at blend zone |

---

## Retired diagnostics

| Mode | Reason |
|---|---|
| Mode 5 (stepCount) | Integer visualization artifact — misleads about SDF quality |
| Analytic SDF toggle for SDF diagnosis | Confirmed trilinear is not the cause; toggle has no diagnostic value for mode 0 banding |

---

## Key architectural insight confirmed

The radiance cascade banding in mode 0 comes from the **bake pass output** (probe atlas + isotropic grid), not from the display-path raymarching. This means:

- Fixing the display-path SDF, step size, or interpolation will not fix mode 0 banding.
- The fix path is either: (a) higher angular resolution in the probe atlas (D8 vs D4), (b) higher spatial probe density, or (c) spatial/temporal filtering of the GI output.
- The smoothstep change (blend zone) addresses a secondary contribution only.
