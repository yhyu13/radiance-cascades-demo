# Phase 8 — GI Banding: Isolate and Reduce Cascade Probe Artifacts

**Date:** 2026-05-01
**Revised:** 2026-05-01 (post reviews 08 + 09)
**Depends on:** Phase 7 findings summary (`phase7_findings_summary.md`)
**Goal:** Identify which component of the cascade GI reconstruction is the dominant
source of mode 0 banding, and apply targeted fixes.

---

## Context

Phase 7 identified the **leading remaining hypothesis**: banding in mode 0 originates
in cascade-side quantization (angular or spatial), not in the display-path raymarching.

This hypothesis is not yet proven. The bake-path SDF (still texture-based in
`radiance_3d.comp`) may also contribute to probe hit-position quantization —
this influence has not been separately measured. Phase 8 experiments proceed on the
leading hypothesis while keeping it falsifiable.

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

**Code needed:** None. Toggle already exists in UI.

---

### E2 — Measure angular contribution: increase dirRes to 8

**Action:** Use the new live `dirRes` slider (added in Phase 8 code changes) to change
`dirRes` from 4 to 8. Cascades rebuild automatically. Compare mode 0 and mode 6.

D8 gives 64 bins vs 16 bins at D4. The octahedral bin coverage becomes finer but the
bins are still non-uniform in solid angle — this is an angular undersampling improvement,
not a uniform-sphere fix.

**Cost — both bake and display path:**

| Component | D4 (current) | D8 (proposed) | Ratio |
|---|---|---|---|
| Bake: rays dispatched per probe per cascade | 16 | 64 | 4× |
| Display: atlas samples in `sampleDirectionalGI()` per surface hit pixel | 16 | 64 | 4× |
| Atlas memory per cascade | proportional to D² | proportional to 4D² | 4× |

**Run E1 first.** Only attempt D8 if E1 confirms angular resolution is a major
contributor — the 4× cost affects every frame in both bake and display.

**Observe:** Does banding reduce proportionally to the bin count?
- Halved or more → angular resolution is the dominant cause.
- Unchanged → spatial probe grid is dominant.

**Code needed:** Yes — live `dirRes` slider with cascade rebuild. Added in Phase 8.

---

### E3 — Measure spatial contribution: examine probe spacing banding frequency

**Action:** In mode 0, measure the spatial period of the banding (pixels per band →
world-space distance per band at the known viewport mapping). Compare to:
- C0 probe spacing: `volumeSize / cascadeC0Res` = 4m / 32 = **12.5 cm per probe**
- C0 probe cell diagonal: 12.5 cm × √3 ≈ **21.6 cm**

If banding period matches probe spacing: spatial quantization confirmed.
If banding period is finer or coarser: not probe-grid spatial origin.

**Code needed:** None. Use screenshot + ruler.

---

### E4 — Spatial fix: increase cascade probe count via `cascadeC0Res`

If E3 confirms spatial probe spacing is dominant:

Use the **existing** `cascadeC0Res` combo in the UI ("C0 probe resolution"). This is
the correct knob — `cascadeC0Res` controls probe density directly:

```
probe spacing = volumeSize / cascadeC0Res = 4m / 32 = 12.5 cm (current)
                                                 / 48 = 8.3 cm  (proposed — not in combo)
                                                 / 64 = 6.25 cm (in combo, high cost)
```

The UI combo offers `8 / 16 / 32 / 64` — change to 64 to halve probe spacing.
The change triggers `destroyCascades()` + `initCascades()` automatically.

Note: 64³ with D-scaling ON is ~340 MB VRAM (per existing UI warning).
Adding 48³ to the combo is a one-line code change if a mid-step is needed.

**Code needed:** None for 64³ (already in combo). One-line change to add 48³ option.

> `volumeResolution` controls SDF/albedo texture resolution, NOT probe density.
> Do not confuse the two. `cascadeC0Res` is the spatial probe knob.

---

### E5 — Angular smoothness: A/B test `useDirBilinear` ON vs OFF

`useDirBilinear` defaults to `true` in the live code (`src/demo3d.cpp:118`).
The UI toggle already exists and triggers a cascade rebuild on change.

**Action:** Toggle `useDirBilinear` OFF in the UI. Compare mode 0 and mode 6.

| Result | Interpretation |
|---|---|
| Banding increases noticeably with bilinear OFF | Bilinear is actively smoothing bin-boundary artifacts; angular undersampling (D4) still remains |
| Banding unchanged with bilinear OFF | Bin-boundary smoothing contributes negligibly; artifact is from D4 bin count, not boundaries |

This separates "too few bins" from "hard bin boundaries" as the angular artifact source.

**Code needed:** None. Toggle already exists and is already ON.

---

### E6 — GI output spatial filter (last resort)

If E1–E5 do not reduce banding to an acceptable level:

Options:
- Box filter over 3×3 probe neighborhood in the probe grid texture (world-space blur)
- Temporal accumulation with jittered probe positions (rolling average over N frames)
- Screen-space bilateral filter on the GI contribution only

Cost: moderate. Risk: blurs sharp GI boundaries at occlusion edges.

**Code needed:** Yes — new compute or fullscreen pass.

---

## Implementation priority

```
E1 (no code)  →  E3 (no code)  →  E5 (no code)
     ↓                                   ↓
angular dominant?              bin-boundary or bin-count?
     ↓                                   ↓
E2 (new slider, 4× cost)         already ON → E2 needed
     ↓
E4 (no code — use cascadeC0Res combo)
     ↓
E6 (last resort — new pass)
```

Run all zero-code experiments (E1, E3, E4, E5) before writing any new code.
E2 is the only experiment that required new code (live `dirRes` slider).

---

## Code changes in Phase 8

| Change | File | Status |
|---|---|---|
| Live `dirRes` slider with cascade rebuild | `src/demo3d.cpp` | Done |

---

## Success criteria

| Criterion | Target |
|---|---|
| Mode 0 banding reduced to not-distracting | No sharp rectangular contour lines in indirect field at normal viewing distance |
| Mode 6 (GI only) banding reduced | Same — GI-only view shows smooth indirect field |
| No regression in cascade blend quality | No visible D4→D8 seam in mode 0 |
| Performance within budget | Frame time ≤ 2× current when banding fix is active |

---

## Files that changed in Phase 8

| File | Change |
|---|---|
| `src/demo3d.cpp` | `dirRes` change detection + rebuild; live `dirRes` slider in UI |

## Files that do NOT need to change (confirmed)

| File | Reason |
|---|---|
| `res/shaders/radiance_3d.comp` | Bilinear atlas sampling already active (`useDirBilinear=true`) |
| `res/shaders/raymarch.frag` | Display-path SDF confirmed not the cause (Phase 7) |
| `src/demo3d.h` | No new probe resolution parameter needed; `cascadeC0Res` already exists |
