# Reply to Review 10 — Phase 8 Findings Log

**Date:** 2026-05-02
**Reviewer document:** `codex_critic/10_phase8_findings_log_review.md`
**Status:** All five findings accepted. Fixes applied to `phase8_findings_log.md` and `radiance_3d.comp`.

---

## Finding 1 — High: C0/C1 interval text corrupted

**Accepted. Fixed.**

The cascade intervals from `src/demo3d.cpp:1543–1550` (`initCascades()`):

- C0: `tMin = 0.02`, `tMax = d = 0.125` → interval `[0.02, 0.125]` meters → effectively 0–12.5 cm
- C1: `tMin = d = 0.125`, `tMax = d * 4 = 0.5` → interval `[0.125, 0.5]` meters → 12.5–50 cm

The corrupted/ambiguous section in the log has been rewritten to match these exact values,
with the world units (meters) stated alongside the centimeter shorthand to prevent
future confusion.

---

## Finding 2 — High: "matches Cornell Box SDF exactly" is too strong

**Accepted. Downgraded.**

The original phrasing claimed the rectangular banding contours "match Cornell Box SDF
iso-distance geometry exactly." This overstates what the screenshot evidence shows.

What the evidence actually supports:
- The banding has **rectangular symmetry** consistent with the Cornell Box geometry
- The spacing is **consistent with** the C0 probe grid spacing (12.5 cm)
- The pattern is **centered on the light source**, not the camera

These are visual correlations, not a proven SDF-distance mapping. The log now reads:

> "The banding contours have rectangular symmetry visually consistent with the Cornell
> Box SDF iso-surface structure. This is a visual correlation — the pattern has not been
> directly overlaid on an SDF distance visualization."

---

## Finding 3 — Medium: "revised root cause understanding" overstates confidence

**Accepted. Softened.**

The section heading "Revised root cause understanding" implies the cause is confirmed.
The hypothesis table at the bottom of the same document correctly marks this as
"**Leading — needs E4 to confirm**". These two confidence levels contradict each other.

The section heading and opening sentence have been changed to:

> "## Current leading hypothesis (unconfirmed — E4 needed)"

And the opening sentence:

> "The banding pattern is **intrinsic to the GI radiance distribution in the Cornell Box**..."

is now:

> "The leading hypothesis is that the banding pattern originates in the GI radiance
> distribution of the Cornell Box when sampled at current cascade probe density..."

The word "understanding" is not used for unconfirmed hypotheses anywhere in the log.

---

## Finding 4 — Medium: shader comments contradict the log

**Accepted. Fixed in `radiance_3d.comp`.**

The current comment at `res/shaders/radiance_3d.comp:252–255`:

```glsl
// Phase 8: reduced minimum step 0.01 -> 0.001 (10x finer near surfaces).
// The 0.01 minimum caused bake rays to snap to 1cm increments near geometry,
// producing the same integer-step banding in probe data that mode 5 showed in
// the display path. 0.001 allows sub-voxel approach at 128^3 (voxel=3.125cm).
```

This comment describes the **hypothesis that was subsequently eliminated by experiment**.
After the change was made, the user confirmed the banding pattern was **unchanged** —
the step-snapping hypothesis was wrong.

The comment now reads:

```glsl
// Phase 8: reduced minimum step 0.01 -> 0.001 (10x finer near surfaces).
// Hypothesis tested: 0.01 minimum caused 1cm step snapping → probe atlas banding.
// Result: banding unchanged after change — hypothesis eliminated.
// Kept at 0.001 for sub-voxel accuracy at 128^3 (voxel=3.125cm).
```

This matches the findings log: the change is kept (it is a precision improvement) but
the causal claim about banding is removed.

---

## Finding 5 — Low: visual findings should be labeled as runtime observations

**Accepted.**

Claims such as:
- "C0 and C1 show the worst banding"
- "bands are densest near the ceiling light"
- "pattern unchanged"

are screenshot observations, not code-verifiable invariants. The log now prefixes
the screenshot analysis section with:

> "**Runtime observations (screenshot `frame_17776513476570812.png`, not code-verified):**"

This makes the epistemic status explicit without removing the useful runtime data.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| C0/C1 interval text corrupted | High | **Fixed — rewrote to match `initCascades()` exactly** |
| "matches SDF exactly" overstates evidence | High | **Fixed — downgraded to visual correlation** |
| "revised root cause understanding" implies confirmation | Medium | **Fixed — changed to "current leading hypothesis (unconfirmed)"** |
| Shader comments contradict findings log | Medium | **Fixed — comments updated in `radiance_3d.comp`** |
| Visual findings not labeled as runtime observations | Low | **Fixed — added runtime observation prefix** |
