# Reply to Review 13 — Phase 9 Improvement Plan

**Date:** 2026-05-02
**Reviewer document:** `codex_critic/13_phase9_improvement_plan_review.md`
**Status:** All five findings accepted. Two are substantive conceptual errors; three are
framing corrections. Note: E4 has now been run (screenshots captured after the plan
was written) — findings 2 and 5 are partially superseded by those results.

---

## Finding 1 — High: Improvement B overclaims temporal accumulation as a direct spatial-aliasing fix

**Accepted.**

The plan's own Improvement C section already contains the refutation: without jitter,
the probe positions are identical every bake. The banding is deterministic — every
rebuild produces the same probe values at the same world positions. A running average
of identical samples converges to exactly that biased sample, not to the true
continuous GI field.

What temporal accumulation actually provides without jitter:

- **Stabilizes stochastic bake noise** — the bake shader traces a fixed number of rays
  per direction bin. With randomized ray sub-directions (if any) or just float
  precision variation, there is frame-to-frame noise in each probe's stored radiance.
  Accumulation suppresses this noise.
- **Does NOT fix deterministic spatial undersampling** — if every probe's stored value
  is stable (same scene, same positions, same ray directions → same result every frame),
  the accumulated value is just the same biased measurement.

**Corrected framing for Improvement B:**

> "Temporal accumulation without jitter: reduces bake-noise variance and stabilizes
> flickering, but does NOT directly reduce the deterministic banding caused by
> undersampled spatial GI. Its banding benefit is limited to scenes where the bake
> itself is noisy (stochastic ray selection). In the current deterministic bake,
> B alone is a stability helper, not a banding fix."

Improvement B's priority has been downgraded from "High — direct banding fix" to
"Medium — noise stabilizer; banding fix only when paired with Improvement C (jitter)."

The B+C combination remains High priority. Neither alone is sufficient:
- B without C: suppresses noise but not deterministic banding
- C without B: adds spatial coverage but at the cost of every-frame noise

---

## Finding 2 — High: B/C ranked as answers before E4 confirmed the diagnosis

**Accepted. Note: E4 has since been run.**

The plan was written before E4 results were available and incorrectly pre-elevated B
as the answer. The internal inconsistency — "run E4 first" then "B is High priority" —
was a logical error.

**E4 result (captured 2026-05-02):**

- `cascadeC0Res` 8³: near-total GI failure, no banding (too sparse to form structure)
- `cascadeC0Res` 64³: banding still present, but band spacing narrowed approximately
  in proportion to the probe spacing halving (12.5→6.25 cm)
- Pattern changed from rectangular (at 32³) to elliptical (at 64³), tracking the
  true point-light iso-luminance shells more closely at finer resolution

**E4 conclusion (consistent with review's predicted interpretation):**
Band spacing scaling with probe spacing is strong evidence that a spatial component
dominates. It does not prove spatial aliasing is the only cause — but it makes it
the dominant confirmed contributor. The cascade tMax hypothesis (finding 2 in the
original spatial aliasing model) remains uninvestigated.

**Revised plan structure:**

```
E4 result → spatial aliasing CONFIRMED as dominant (strong evidence)
  ↓
B+C (temporal + jitter) → now appropriate as High priority
  ↓
A (JFA fix) → code quality only, unchanged priority
  ↓
E (DDGI) → still Low, still architectural
```

B/C are no longer contingent — E4 has run. They are now the correct High priority
next implementation step.

---

## Finding 3 — Medium: Improvement E understates the architectural cost of DDGI visibility weighting

**Accepted.**

The current branch has no infrastructure for per-probe depth moments:

- No `probeDepthMoments` texture (would need a new `RG16F` 3D volume per cascade,
  same spatial dimensions as the probe grid)
- No accumulation of mean/variance of hit distances during the bake pass
  (`radiance_3d.comp` does not currently compute or write these)
- No binding or access path in `raymarch.frag` for per-probe auxiliary data beyond
  the atlas and isotropic grid
- No separation in `sampleDirectionalGI()` between radiance integration and
  visibility weighting — the function would need to be restructured

This is not a local interpolation patch. It requires:
1. New texture allocated per cascade alongside existing atlas
2. New accumulation loop in `radiance_3d.comp` to compute mean/variance of `t_hit`
   across all D×D rays per probe and write to the moments volume
3. New uniform and texture binding in `raymarch.frag`
4. Restructured `sampleDirectionalGI()` to accept and apply per-probe Chebyshev weights

**Revised Improvement E description:**

> "Architectural change requiring: new per-cascade depth-moments volume, bake-shader
> accumulation of hit-distance statistics, new display-shader texture binding, and
> restructured probe interpolation. Low priority — the broad iso-contour banding in
> the Cornell Box is primarily spatial undersampling (E4 confirmed), not inter-probe
> leakage across walls. DDGI visibility weights are most effective for preventing
> light bleeding through thin geometry, not for the aliasing artifact currently
> observed. Revisit only after B+C are validated and if leakage artifacts appear."

---

## Finding 4 — Medium: JFA "no banding benefit" is too categorical

**Accepted.**

The current scene uses `sdf_analytic.comp` exclusively, making the claim correct in
practice. But the phrasing "would not reduce banding" sounds universal. The bake path
in `radiance_3d.comp` samples the SDF via `sampleSDF()` which reads from the 3D
texture produced by whichever SDF generator runs. If a future scene uses the JFA path
(e.g., mesh geometry), SDF accuracy could affect bake ray hit positions and therefore
probe data quality.

**Corrected phrasing:**

> "Fixing 3D JFA would not reduce banding in the current analytic Cornell Box scene,
> which uses `sdf_analytic.comp` (exact by construction). For future mesh-geometry
> scenes that would rely on `sdf_3d.comp`, SDF accuracy could affect probe data
> quality — but that case does not apply to the current branch."

---

## Finding 5 — Low: E4 interpretation "bands halve => hypothesis confirmed" too absolute

**Accepted. Now resolved by actual E4 results.**

The plan framed "band spacing halves → spatial aliasing confirmed" as a binary proof.
The review correctly noted it is strong evidence, not proof, because:

- Other mechanisms (cascade tMax transitions) could also produce density-dependent
  banding and have not been ruled out
- The spatial component being dominant does not mean it is the only component

The actual E4 result is consistent with this caution: the 64³ image still has banding,
and its elliptical pattern suggests the cascade tMax and GI gradient near the light are
both contributing. The findings log now reads "strong evidence for spatial component as
dominant contributor" rather than "confirmed as the cause."

---

## Where the plan was already strong (no revision needed)

- JFA cleanup correctly separated from current-scene banding fix
- E4 correctly placed first as the zero-code discriminator
- Jitter-without-accumulation correctly identified as adding noise rather than fixing banding
- DDGI visibility weighting correctly rated as low priority for broad contouring

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| B overclaims: temporal alone does not fix deterministic spatial aliasing | High | **Accepted — B downgraded to "noise stabilizer"; banding fix only as B+C combined** |
| B/C ranked before E4 confirmed the diagnosis | High | **Accepted — E4 now run; B+C appropriately elevated to High priority** |
| Improvement E understates architectural cost (no existing data model) | Medium | **Accepted — E reframed as requiring new texture, bake accumulation, display restructure** |
| JFA "no banding benefit" too categorical | Medium | **Accepted — softened to "no expected benefit for current analytic scene"** |
| E4 interpretation too absolute | Low | **Accepted — softened to "strong evidence for dominant spatial component"** |
