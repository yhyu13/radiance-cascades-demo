# Reply to Review 08 — Phase 7 Findings Summary

**Date:** 2026-05-01
**Reviewer document:** `codex_critic/08_phase7_findings_summary_review.md`
**Status:** All four findings accepted.

---

## Finding 1 — High: "root cause narrowed to cascade GI data" is too strong

**Accepted.**

What the experiments actually support:

- The display-path SDF sample in `raymarch.frag` is probably not the dominant cause of
  mode-0 banding (analytic toggle: banding persists).
- Mode 5 integer step count is a misleading diagnostic (mode 7: `t` is smooth).

What they do not prove:

- That the remaining source is exclusively cascade atlas/bin/probe quantization.
- That the bake-path SDF (still texture-based in `radiance_3d.comp`) has no influence
  on the probe hit positions and therefore on the indirect field banding.

**Revised:** Changed "root cause narrowed to cascade GI data" to
"leading remaining hypothesis: cascade-side quantization (angular or spatial)."
The display-path SDF is a weak contributor or non-contributor. The bake-path SDF
influence is unresolved and will be revisited if E1–E3 (Phase 8) do not explain the
banding magnitude.

---

## Finding 2 — Medium: analytic toggle conclusion is broader than the toggle proves

**Accepted.**

The analytic toggle swaps the **display-path** SDF sample only. It does not affect:

1. The bake rays in `radiance_3d.comp` — still read `texture(uSDF, uvw).r` via the
   texture-path `sampleSDF()`. Probe hit positions used to bake the atlas can still
   carry texture-SDF discretization effects.
2. Albedo lookup — still from the precomputed albedo volume texture.
3. Cascade reconstruction — probe atlas already written; toggle has no effect on it.

**Revised:** The analytic toggle conclusion now explicitly states:
"This eliminates display-path trilinear SDF interpolation as a cause of mode-0 banding.
It does not eliminate SDF texture effects on the cascade bake: bake-path rays still use
the texture SDF, and the probe atlas retains whatever hit-position quantization the
bake introduced."

---

## Finding 3 — Medium: "22.5°/bin" oversimplifies the D4 octahedral scheme

**Accepted.**

The D4 parameterization places `4×4 = 16` bins on the unit octahedron using an
octahedral projection, not in uniform angular sectors of 22.5°. The solid angle per bin
is non-uniform across the octahedron map — bins near octahedron face centers cover a
different solid angle than bins near the seam at the hemisphere equator. Saying
"22.5°/bin" implies a uniform spherical partition, which is wrong.

**Revised:** "D4 → 16 bins on the octahedral hemisphere map (non-uniform solid-angle
distribution; approximately covers the hemisphere but bin widths vary)."

The qualitative point remains: 16 bins at D4 gives coarse angular resolution, and the
boundaries between bins are a plausible source of banding. The "22.5°" shorthand is
removed.

---

## Finding 4 — Low: "experiments complete" but smoothstep A/B is still pending

**Accepted.**

The smoothstep cascade blend change was applied in Phase 7 but no A/B screenshot
comparison has been captured at the blend zone boundary. It is not appropriate to call
experiments complete when one listed experiment has not been run.

**Revised:** Status header changed to "Main diagnostics complete; smoothstep visual
comparison pending."

The smoothstep entry in the "Partially addressed" table is clarified:
"Smoothstep applied — removes derivative kink at blend zone endpoints.
Visual A/B at D4→D8 boundary not yet captured."

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| "Root cause narrowed to cascade GI" is too strong | High | **Accepted — softened to "leading remaining hypothesis"** |
| Analytic toggle conclusion broader than toggle proves | Medium | **Accepted — scoped explicitly to display-path SDF only** |
| "22.5°/bin" oversimplifies octahedral D4 angular description | Medium | **Accepted — replaced with "16 bins on octahedral map, non-uniform solid angle"** |
| "Experiments complete" inconsistent with pending smoothstep A/B | Low | **Accepted — softened to "main diagnostics complete"** |
