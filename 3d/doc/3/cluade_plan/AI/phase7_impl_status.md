# Phase 7 — Anti-Banding Implementation Status

**Date:** 2026-04-30
**Plan docs:** `phase7_banding_fix_plan.md`, `phase7b_anti_banding_revised_plan.md`

---

## Completed

### Defaults set (src/demo3d.cpp)

The confirmed best-quality configuration is now the launch default:

| Field | Old default | New default |
|---|---|---|
| `useColocatedCascades` | `true` | `false` — non-colocated: C0=32³, C1=16³, C2=8³, C3=4³ |
| `useScaledDirRes` | `false` | `true` — D4/D8/D16/D16 per cascade |
| `useDirectionalGI` | `false` | `true` — cosine-weighted 8-probe trilinear over C0 atlas |

These match the configuration under which the visual triage screenshots were captured.

---

### Experiment 1 — Smoothstep cascade blend weight (res/shaders/radiance_3d.comp:352-355)

Replaced the linear `clamp` ramp with a cubic `smoothstep` in the cascade blend weight,
preserving all existing safety guards:

```glsl
// Before:
float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
    ? 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0)
    : 1.0;

// After:
float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
    ? 1.0 - smoothstep(0.0, 1.0,
          clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0))
    : 1.0;
```

**Effect:** Removes the derivative kink at both endpoints of the blend zone. The
D4→D8 angular-resolution transition is now a smooth S-curve rather than a linear ramp
with hard corners. This is a partial improvement — it softens the cascade boundary
contribution to the contour banding but does not address the dominant root cause
(SDF voxel quantization — see analysis doc).

---

## Pending / Not yet applied

### Experiment 2 — Widen blendFraction (UI A/B)

No code change required. Test by dragging the `blendFraction` slider to 0.75 and 0.9
in the running app, pressing `P` at each value, comparing screenshots.

**Status:** Not yet tested. **Do this first** before Experiment 3.

---

### Experiment 3 — Raise dirRes to 8 (C0=D8, significant cost)

Would change `dirRes(4)` → `dirRes(8)` in the constructor, shifting C0 from 16 to 64
directional bins. Display cost: 128 → 512 atlas fetches per shaded pixel (4×).

**Status:** On hold pending Experiment 2 evaluation. Also partially superseded by the
SDF quantization finding — increasing dirRes will not fix the step-count banding.

---

### Experiment 4 — Blend zone dither (last resort)

Static per-probe hash jitter on the blend boundary. Produces stable noise rather than
coherent contour — only useful if structured banding persists after Experiments 1–3.

**Status:** Not applied. Last resort.

---

## Root cause update

After visual inspection of the step count heatmap (mode 5, `uRenderMode == 5`), the
dominant source of the rectangular contour banding has been revised. See
`phase7_sdf_quantization_analysis.md` for full details.

**Summary:** The banding is primarily caused by **SDF voxel quantization at 64³
resolution**, not by the cascade blend zone math. The cascade smoothstep (Experiment 1)
addresses a secondary contributor. The primary fix is increasing the SDF volume
resolution.

| Rank | Root cause | Addressed by |
|---|---|---|
| 1 (dominant) | SDF 64³ voxel quantization | Increase SDF resolution (not yet planned) |
| 2 | Cascade blend linear weight kink | **Done — smoothstep applied** |
| 3 | C0→C1 angular resolution jump (D4→D8) | Experiment 3 (pending) |
