# Reply to Review 10 — Phase 5h Shadow Ray Plan

**Date:** 2026-04-29
**Reviewer document:** `10_phase5h_shadow_ray_plan_review.md`
**Status:** All three findings accepted. F1 requires plan reframing. F2 requires a
concrete code fix (normal-offset bias). F3 requires softening one sentence.

---

## Finding 1 — High: "ground truth for Phase 5g" framing is wrong

**Accepted.** The plan claimed Phase 5h provides "ground-truth reference" for Phase 5g
and that the combined result would be "hard shadow core + soft penumbra from indirect
propagation."

This overstates the relationship between the two phases:

- Phase 5h measures **direct-light occlusion** from the shaded pixel to the point light.
  Binary result: 0 (occluded) or 1 (visible).
- Phase 5g estimates **diffuse irradiance** at the surface from probe-stored directional
  data. It captures all incoming radiance — not just direct shadow — averaged over the
  hemisphere.

These are related signals but different quantities. Phase 5h cannot serve as ground truth
for Phase 5g any more than a shadow map validates a global illumination probe.

The "soft penumbra" story specifically requires either:
- An area light (the current setup uses a point light), or
- Phase 5g producing smooth spatial transitions through probe interpolation (valid), but
  this is a GI-smoothing effect, not a physically correct direct-light penumbra.

**Plan doc updated:** Scope narrowed to "fix missing direct shadow term in the final
renderer." The Phase 5g relationship rephrased as: "Phase 5h makes the shadow
ground-truth visible in mode 4 so Phase 5g's indirect contribution can be judged
against a correct direct reference — not as a penumbra validator." The "hard core +
soft penumbra" table row rephrased as "hard shadow in direct + smoother indirect GI."

---

## Finding 2 — Medium: shadow ray origin should use normal offset, not fixed t=0.05

**Accepted.** The plan copied the bake shader's `t = 0.05` self-intersection offset
verbatim from `inShadow()` in `radiance_3d.comp`. The final renderer has the surface
normal already computed at the hit point — using it to offset the origin is strictly
better:

```glsl
float shadowRay(vec3 hitPos, vec3 normal, vec3 lightPos) {
    vec3  toLight   = lightPos - hitPos;
    float distLight = length(toLight);
    vec3  ldir      = toLight / distLight;
    // Normal-offset origin eliminates the self-intersection problem analytically:
    // moving along the outward normal guarantees starting outside the surface.
    // A small fixed offset handles the case where the normal and light direction
    // are near-perpendicular (grazing incidence).
    vec3  origin    = hitPos + normal * 0.02 + ldir * 0.01;
    float t         = 0.0;
    for (int i = 0; i < 32 && t < distLight; ++i) {
        float d = sampleSDF(origin + ldir * t);
        if (d >= 1e9) return 0.0;
        if (d < 0.002) return 1.0;
        t += max(d * 0.9, 0.01);
    }
    return 0.0;
}
```

`normal * 0.02`: pushes origin clearly outside the surface along the outward normal.
`ldir * 0.01`: additional offset along the light direction for grazing incidence.

The function signature is updated to accept `normal` as a parameter. At the call site
in `main()`, `normal` is already computed before the direct shading block.

**Plan doc updated:** `shadowRay()` function signature and body updated with normal-
offset origin. The plan no longer calls it "the same as `inShadow()`" — it is a
better version available only in the final renderer where the normal is known.

---

## Finding 3 — Medium: performance claim softened

**Accepted.** "Expected acceptable, verify at runtime" replaces "negligible." The 32
extra SDF reads stack on top of the primary march, normal estimation, and cascade
sampling. In a Cornell Box at 60fps this is likely fine, but the plan should not make
that claim before measuring it.

**Plan doc updated:** Performance section now reads "expected acceptable, measure
actual frame time with/without the shadow ray enabled."

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| "Ground truth for 5g" framing too strong | High | Plan reframed: 5h = direct shadow fix; relationship to 5g clarified |
| Fixed shadow bias — use normal offset instead | Medium | `shadowRay()` updated to `hitPos + normal*0.02 + ldir*0.01` origin |
| Performance claim "negligible" too confident | Medium | Softened to "expected acceptable, verify at runtime" |

The core plan — adding a 32-step shadow ray to `raymarch.frag` with a `uUseShadowRay`
toggle, no cascade rebuild required, applied to both mode 0 and mode 4 — is unchanged.
