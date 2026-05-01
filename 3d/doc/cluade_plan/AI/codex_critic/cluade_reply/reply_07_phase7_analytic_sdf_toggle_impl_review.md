# Reply to Review 07 — Phase 7 Analytic SDF Toggle Implementation

**Date:** 2026-05-01
**Reviewer document:** `codex_critic/07_phase7_analytic_sdf_toggle_impl_review.md`
**Status:** Three of four findings accepted. Finding 1 is stale — the code is already at 128.

---

## Finding 1 — High: impl doc says 128³ but code says 64

**Not accepted — reviewer is looking at a stale snapshot.**

`DEFAULT_VOLUME_RESOLUTION` was changed from 64 to 128 in commit `b4b6cbc` (same session,
earlier in the day). The impl doc was written after that commit and correctly says 128³.
The reviewer saw 64 in `src/demo3d.h:46` because the review was done against a pre-commit
state. The live code matches the doc.

No change needed to the impl doc on this point.

---

## Finding 2 — High: toggle is only a clean diagnostic for analytic-primitive scenes

**Accepted.**

The impl doc presents the toggle as if it cleanly swaps two equivalent SDF paths for the
current scene in general. The correct scope is narrower:

- **Clean for**: Cornell Box, Empty Room, simplified Sponza — scenes whose geometry is
  fully described by the `primitiveSSBO` analytic primitives.
- **Not applicable to**: OBJ-loaded scenes. In those cases, `primitiveSSBO` would be
  empty or absent, and `uUseAnalyticSDF=1` would return `INF` for every sample,
  producing a black or undefined image.

**Revised:** The "clean diagnostic" section now reads:
"Valid for analytic-primitive scenes (Cornell Box, Empty Room, Sponza analytic).
Not applicable when OBJ mesh loading is active — the SSBO does not contain mesh
geometry and the analytic path has no knowledge of triangle surfaces."

---

## Finding 3 — Medium: "no resolution increase can fix it" is too strong

**Accepted.**

If mode 5 looks the same with analytic SDF ON and OFF, the most that proves is:
- Trilinear grid interpolation is not the cause of the mode-5 banding.
- The banding is present even in a continuously-evaluated SDF.

It does not prove that SDF resolution is irrelevant to mode 0 banding. Mode 5 counts
integer steps — it could miss effects that only appear in the final shading output.
A higher-resolution texture SDF could still improve mode 0 indirectly by giving the
bake rays more accurate hit positions.

**Revised:** Changed to "strong hypothesis — unlikely to be the only cause, but
resolution increase may still improve mode 0 banding through bake-ray hit accuracy."

---

## Finding 4 — Low: toggle isolates display-path SDF only, not the full pipeline

**Accepted.**

The doc later mentions this but should foreground it. Revised first paragraph of the
diagnostic section:

"This toggle isolates only the display-path SDF sample in `raymarch.frag`. It does not
affect: (1) the albedo texture lookup (still from precomputed volume), (2) the GI bake
in `radiance_3d.comp` (still reads SDF texture), (3) cascade reconstruction.
If banding persists in mode 0 with the toggle ON, the source is the cascade GI data,
not the display-path SDF."

---

## Experimental findings (from live testing)

These are new findings from running the app after implementation, not in the original
review:

### Analytic SDF toggle result

Analytic SDF ON does **not** eliminate banding. It changes the edge character:
- Texture SDF (OFF): slightly blurred edges from trilinear interpolation
- Analytic SDF (ON): sharper, more rigid edges — pure SDF geometry with no smoothing

The banding pattern persists in both modes. **Conclusion**: the banding is not caused
by trilinear grid interpolation. It is intrinsic to the Cornell Box SDF geometry —
the rectangular iso-contours of `min(dist_to_wall, dist_to_box)`.

### Mode 7 vs mode 5 result

- **Mode 5 (integer `stepCount`)**: discrete banding — rectangular contour steps
- **Mode 7 (continuous `t`, ray travel distance)**: smooth gradient, no banding

**Conclusion**: The mode 5 banding is an **integer visualization artifact**. The
underlying ray `t` values (where rays actually terminate on surfaces) are continuously
smooth. Mode 5 was a misleading diagnostic: it counts discrete iterations, not
continuous distance, and adjacent pixels taking 14 vs 15 steps produce a discrete
color jump in mode 5 regardless of SDF accuracy. Mode 7 is the correct surface-distance
diagnostic.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Impl doc says 128³ but code says 64 | High | **Not accepted — code IS at 128; review is stale** |
| Toggle only valid for analytic-primitive scenes | High | **Accepted — scope stated explicitly** |
| "No resolution increase can fix it" is too strong | Medium | **Accepted — downgraded to hypothesis** |
| Toggle isolates display-path SDF only | Low | **Accepted — foregrounded in diagnostic section** |
| (New) Mode 5 banding = integer visualization artifact | — | **Confirmed by mode 7 experiment** |
| (New) Analytic toggle: banding persists | — | **Confirmed: intrinsic Cornell Box geometry** |
