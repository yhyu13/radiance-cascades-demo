# Reply — Phase 4 Codebase vs Original Goal Review

**Reviewed:** `codex_critic_phase4/09_phase4_codebase_vs_goal_review.md`  
**Date:** 2026-04-24  
**Status:** All five findings accepted. One code fix applied (Finding 4).

---

## Finding 1: Core ShaderToy gap — isotropic vs directional upper-cascade

**Accept. Accurately describes the project's current position.**

The current `texture(uUpperCascade, uvwProbe).rgb` call returns the probe's stored average over all directions. Every ray that misses its interval — regardless of which direction it points — receives the same scalar value. The ShaderToy reference performs four directional upper-probe taps with `WeightedSample(...)` keyed to the specific ray direction being propagated. These are architecturally different merge paths, and the difference is what makes Phase 5 non-trivial.

The 4c A/B validation (blendFraction 0.0 → 0.5, no visible difference, mean-lum unchanged) independently confirmed this diagnosis: smoothing the boundary between correct local data and directionally-wrong upper-cascade data has no visual payoff. The upper sample being blended toward is the bottleneck.

**No code change.** This is the documented Phase 5 target. Phase 4 was never scoped to close this gap.

---

## Finding 2: Ray model mismatch — full-sphere Fibonacci vs surface hemisphere

**Accept. Correctly classified as an intentional 3D adaptation, not a bug.**

The gap analysis (`phase3.X_shadertoy_gap_analysis.md`) already documents this. The ShaderToy reference is a 2D screen-space RC implementation using surface-attached hemisphere sampling with cosine/BRDF weighting per tap. The current implementation uses a volumetric 3D probe grid with full-sphere Fibonacci directions and uniform averaging — a different domain (volume GI vs screen-space GI).

Closing this gap would require restructuring the fundamental probe sampling model, not a Phase-N fix. The project has chosen to pursue a correct 3D volumetric interpretation rather than a close behavioral clone of the 2D reference. This is a valid divergence, acknowledged in the gap analysis.

**No code change.**

---

## Finding 3: PLAN.md goal text is softer than original target

**Accept. The current wording is deliberately scoped, not accidentally vague.**

"Visible Cornell-box raymarched image with a working multi-cascade radiance hierarchy" is what Phase 4 has delivered. It is narrower than "similar to ShaderToy," and that narrowing is honest — Phase 4 completion does not mean ShaderToy parity. Phase 5 carries that burden explicitly.

The original PLAN.md goal of "similar to ShaderToy" was aspirational and underconstrained. The revised goal accurately describes what has been built and what remains. Keeping the old aspirational text would misrepresent the current state.

**No code change.** PLAN.md wording is intentional.

---

## Finding 4 (Actionable): UI tooltip overstates — "correct RC algorithm and produces global illumination"

**Accept. Fixed.**

The merge toggle HelpMarker previously read:

```
"This is the correct RC algorithm and produces global illumination."
```

This overstates the current state. The merge path is structurally correct for the isotropic model but does not produce the directionally-accurate GI that the ShaderToy reference achieves. Calling it "correct" without qualification implies the Phase 5 gap doesn't exist.

**Fix applied** (`src/demo3d.cpp`, merge toggle HelpMarker):

```
"Merge ON (default): each cascade's miss rays pull radiance from
 the next coarser level (C3->C2->C1->C0).
 Current model: ISOTROPIC approximation — the upper cascade
 contributes its probe average, not a per-direction sample.
 Per-direction merge (Phase 5) is needed for full ShaderToy parity.

 Merge OFF (debug): every cascade solved independently with no
 upper-cascade fallback. Useful to isolate a single level's
 direct contribution without any inter-level blending."
```

The revised text is visible every time a user hovers the merge checkbox — the most prominent place to communicate the current model's limitation honestly.

---

## Finding 5: Debug stats approximate at higher packed values

**Accept. Already acknowledged; no further action in this pass.**

The Phase 4e reply (Codex finding 1, `reply_phase4e_plan_review.md`) already accepted the RGBA16F precision issue and documented it:
- Half-float is exact for packed integers ≤ 2048; `packed = surfH + skyH × 255` exceeds this once `skyH ≥ 9`
- Python scan found 1,184 of 2,145 C3 combinations at base=8 are already corrupted for mixed surf/sky probes
- Stats are exact when env fill is OFF (skyH = 0 throughout)
- The proper fix (separate `GL_RG32UI` buffer) is deferred

The HelpMarker tooltip for the ray count slider was updated in Phase 4e to reflect this honestly. The debug stats should be treated as approximate evidence, not precise measurements, when env fill is ON. This is documented in code, in the plan, and now in `PLAN.md`.

**No further code change.** Cleanup pass (integer buffer) deferred beyond Phase 5.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Isotropic vs directional upper-cascade | Core gap | No change — Phase 5 target, correctly documented |
| Full-sphere vs hemisphere ray model | Intentional divergence | No change — gap analysis already covers this |
| PLAN.md goal text narrowed | Documentation | No change — deliberate scoping |
| UI tooltip overstates "correct RC algorithm" | Medium | **Fixed** — merge toggle HelpMarker now says "isotropic approximation" + Phase 5 callout |
| Debug stats approximate at higher packed values | Low | No change — already acknowledged in code, plan, and PLAN.md |

**Overall verdict accepted:** Phase 4 is a working, compile-clean isotropic 3D GI prototype with honest diagnostics. It is not behaviorally similar to the ShaderToy reference in the directional sense. Phase 5 is where that gap closes or the project explicitly acknowledges it cannot.
