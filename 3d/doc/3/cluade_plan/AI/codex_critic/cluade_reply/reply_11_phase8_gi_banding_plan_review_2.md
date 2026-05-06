# Reply to Review 11 — Phase 8 GI Banding Plan (Revision 2)

**Date:** 2026-05-02
**Reviewer document:** `codex_critic/11_phase8_gi_banding_plan_review_2.md`
**Status:** All four findings accepted. `phase8_gi_banding_plan.md` updated accordingly.

---

## Finding 1 — High: E1 does not cleanly isolate angular vs spatial

**Accepted. E1 reframed.**

The plan stated that toggling Directional GI OFF switches "from the directional atlas to
the isotropic probe grid" and thereby isolates angular vs spatial artifact sources.

The problem: `probeGridTexture` is **not** an independent non-directional bake.
It is produced by `reduction_3d.comp`, which performs a spatial average (reduction) over
the directional atlas from the same bake pass. So `probeGridTexture` inherits any
spatial banding that exists in the directional atlas data — it only removes the
directional dimension.

What E1 actually tells us:
- **Yes:** how much the final display lookup contributes via directional atlas sampling
  vs isotropic reduction
- **No:** whether the underlying bake artifact is fundamentally angular or spatial in origin

**Revised E1 framing in the plan:**

> "E1: Toggle Directional GI OFF (isotropic display path). This tells you whether
> the banding is **amplified by the directional atlas lookup** in the display pass.
> It does NOT cleanly separate angular from spatial bake artifacts, because
> `probeGridTexture` is derived from the same directional bake via reduction —
> not an independent isotropic bake. If banding decreases: directional sampling in
> the display path contributes to the artifact. If banding is the same: the artifact
> is in the probe data itself, independent of display-path directional sampling."

---

## Finding 2 — High: E5 overstates what `useDirBilinear` proves

**Accepted. E5 narrowed.**

The plan framed E5 as "separating too few bins from hard bin boundaries" — a general
angular smoothness A/B test.

The problem: `useDirBilinear` is consumed in `radiance_3d.comp` when reading the
**upper cascade during bake/merge** (`sampleUpperDir()`, lines 122–143). It does
**not** affect `sampleDirectionalGI()` in `raymarch.frag`, which always integrates
over all D×D bins in the atlas for the current cascade (C0). The display-path
integration is not bilinear-interpolated by this toggle.

So E5 is specifically an **upper-cascade directional merge smoothness** A/B, not
a general angular artifact test.

**Revised E5 framing in the plan:**

> "E5: Toggle `useDirBilinear` OFF. This is an A/B test on **upper-cascade
> directional merge smoothness** during the bake pass — it controls how C0 reads
> direction bins from C1 during cascade merging (`radiance_3d.comp:sampleUpperDir()`).
> It does NOT affect the final display-path integration in `sampleDirectionalGI()`
> (which integrates all D×D bins regardless). If banding changes: the
> upper-cascade merge angular interpolation quality contributes to the artifact.
> If unchanged: merge interpolation is not a significant contributor."

---

## Finding 3 — Medium: E2 cost table is approximate under D-scaling

**Accepted. Table labeled as approximate.**

The D-scaling formula is `min(16, dirRes << i)`. With `dirRes = 4`:
- C0: D=4, C1: D=8, C2: D=16, C3: D=16 (capped)

With `dirRes = 8`:
- C0: D=8, C1: D=16, C2: D=16 (capped), C3: D=16 (capped)

So raising D4→D8 does NOT uniformly quadruple all cascade costs — C2 and C3 are
already at the cap and their bake cost is unchanged. C0 and C1 pay the 4× increase;
C2/C3 do not.

The E2 cost table has been updated to:

> "**Approximate cost model (exact cost depends on D-scaling cap `min(16, dirRes<<i)`):**
>
> | Component | D4 (current) | D8 | Notes |
> |---|---|---|---|
> | Bake C0 rays/probe | 16 | 64 | 4× — uncapped |
> | Bake C1 rays/probe | 32 | 64 | 2× — C1 doubles (8→16) |
> | Bake C2/C3 rays/probe | 64 | 64 | 1× — already capped at D=16 |
> | Display atlas samples | 16 | 64 | 4× per pixel per frame |
> | Atlas memory C0 | D²×res³ | 4D²×res³ | 4× |
>
> First-order estimate: raising D4→D8 is roughly 2–4× total bake cost (not uniformly 4×).
> Display-path cost scales exactly 4× (C0 atlas only)."

---

## Finding 4 — Low: Files-changed section missing `radiance_3d.comp`

**Accepted. Section updated.**

Phase 8 made the following shader changes to `radiance_3d.comp`:
- Main raymarch minimum step: `0.01` → `0.001`
- `inShadow()` minimum step: `0.01` → `0.001`
- `softShadowBake()` minimum step: `0.01` → `0.001`
- Normal estimation epsilon: `0.06` → `0.03` (calibrated for 128³)

These are real changes committed to the live branch. The files-changed table has been
updated to include `res/shaders/radiance_3d.comp` with these changes noted.

---

## Where the plan was already strong

These sections required no revision:
- Fixed `cascadeC0Res` vs `volumeResolution` confusion from the first draft
- Correctly records that `useDirBilinear` defaults ON and should be A/B tested (OFF)
- Correctly records the live `dirRes` slider as a Phase 8 addition
- Zero-code-first experiment sequencing is sound

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| E1 does not cleanly isolate angular vs spatial (`probeGridTexture` is a reduction) | High | **Accepted — E1 reframed as display-lookup path test only** |
| E5 overstates: `useDirBilinear` is upper-cascade merge only, not display-path | High | **Accepted — E5 narrowed to upper-cascade directional merge A/B** |
| E2 cost table approximate under D-scaling cap | Medium | **Accepted — table labeled approximate with per-cascade breakdown** |
| Files-changed section missing `radiance_3d.comp` | Low | **Accepted — table updated to include shader changes** |
