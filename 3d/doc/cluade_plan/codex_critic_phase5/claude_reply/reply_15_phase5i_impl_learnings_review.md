# Reply to Review 15 — Phase 5i Implementation Learnings

**Date:** 2026-04-30
**Reviewer document:** `15_phase5i_impl_learnings_review.md`
**Status:** One finding accepted. Doc fixed in two places.

---

## Finding 1 — Medium: shared `k` overstated as making bake and display "consistent"

**Accepted.** The original text said:

> "Both paths share the same `k` parameter (penumbra width). This simplifies the UI and
> makes bake and display shadows consistent when both are enabled."

And the "Shared `k` Parameter Design" section said:

> "both represent the same physical concept (penumbra width for the same point light)"

Both claims overstate the coupling. The two paths differ in two concrete ways:

1. **Origin convention**: display path uses `hitPos + normal * 0.02 + ldir * 0.01`
   (normal-offset, from Phase 5h's `shadowRay()`). Bake path uses a fixed `t=0.05`
   starting offset (no surface normal is available in the compute shader's ray-march
   context). The same `k` value will produce a different apparent penumbra because the
   initial clearance from the surface is different.

2. **Signal path**: display soft shadow modifies the direct-light term that the user
   sees directly. Bake soft shadow modifies the direct shading stored per-probe, which
   then propagates through the cascade hierarchy (angular averaging in the reduction,
   directional merging across cascade levels) before contributing to the final indirect
   GI signal. Any penumbra written into the bake has been spatially and directionally
   filtered by the time it appears on screen.

Calling these paths "consistent" implies a user could set `k` once and get matching
shadow appearance in both direct and indirect, which is not the case. The correct
characterisation is: shared `k` is an authoring convenience that reduces UI clutter and
keeps the two approximations in a roughly similar range, not a physically meaningful
coupling.

**Fixes applied** in `phase5i_impl_learnings.md`:

1. Opening paragraph (lines 24–25): replaced "makes bake and display shadows consistent
   when both are enabled" with explicit acknowledgement that the two paths differ in
   origin convention and signal path, and that shared `k` is an authoring convenience.

2. "Shared `k` Parameter Design" section: removed "both represent the same physical
   concept" language. Replaced with an explicit two-bullet list of the differences
   (origin offset, signal path), followed by: "Shared `k` is an authoring convenience,
   not a physically meaningful coupling."

The "Physical Accuracy Note" at the end of the document was already correct and
unchanged — the reviewer confirmed it as a strong point.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Shared `k` described as making bake/display "consistent" — overclaims technical coupling | Medium | **Fixed**: two passages reworded to describe shared `k` as UI convenience, not physical consistency |

The structural content (separate display and bake paths, rebuild trigger logic, mode 6
fix wiring) was confirmed accurate and is unchanged.
