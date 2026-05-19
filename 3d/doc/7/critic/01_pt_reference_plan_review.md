# Critique: SDF Path-Traced Reference (compute-shader, progressive)

**Document reviewed:** `doc/7/pt_reference_plan.md`
**Date:** 2026-05-18

---

## Strengths

1. **Addresses a real structural gap.** The project has been measuring quality only via relative A/B comparisons. A ground-truth PT reference that renders the same SDF/albedo/lighting is the correct fix — it makes "quality" a measurable quantity, not a vibe.

2. **Correct scoping.** Diffuse-only, single point light, no NEE, no glossy materials — v1 is intentionally minimal. The "hard non-goals" section explicitly says "NOT trying to be fast" and "NOT replacing the cascade renderer." This prevents scope creep into building a production PT engine.

3. **Shader design is well-structured.** The `tracePath` loop, `Hit` struct, `traceSDF`, `isDirectlyLit`, and progressive accumulation are all cleanly separated. The code is readable and follows established conventions (cosine sampling, Russian roulette, running-mean accumulation).

4. **Honest about SDF fidelity.** The risk section acknowledges that PT gives "ground truth FOR THE SDF GEOMETRY, not for the original OBJ mesh." Since the cascade renderer also uses the SDF, this is a fair comparison — both are approximating the same discretized geometry. This framing prevents the common mistake of treating SDF-PT as pixel-perfect truth.

5. **Day-by-day sequencing with incremental verification.** Each day has a concrete deliverable and a verification checkpoint. Day 1 (test pattern), Day 2 (direct-only), Day 3 (full PT), Day 4 (accumulation + GUI). This is a good iteration structure.

6. **Open questions section resolves key ambiguities upfront.** Question 2 (ambient floor in PT) is particularly well-handled: "the reference is 'what the cascade would converge to if integrated infinitely well,' not 'what physical PT gives.'" This makes the ambient floor inclusion correct for A/B purposes.

---

## Weaknesses / Concerns

### W1 (HIGH) — Direct lighting via shadow ray is NOT "Lambertian direct" / implicit NEE — it's explicit direct lighting with NO importance sampling, and the throughput accounting is wrong

The plan says:

> "Direct lighting via shadow ray at every bounce: faster convergence than waiting for random hemisphere ray to hit the light by chance. Standard PT optimization called 'Lambertian direct' or 'implicit direct light evaluation.'"

This is incorrect terminology and the code implementation has a subtle but important error.

What the code does: at each bounce, it casts a shadow ray to the point light and adds `throughput * albedo * uLightColor * cosTheta * falloff` to accumulation. This is **explicit direct illumination** (also called next-event estimation, NEE) — evaluating the direct lighting integral analytically at every bounce.

The problem: **in a proper path tracer with NEE, the indirect bounce must NOT also hit the light.** Otherwise you double-count direct lighting — the hemisphere sample might randomly hit the light (which the cosine-sampled direction can do for a point light), AND the shadow ray also evaluates it. For a **point light** (zero area), the probability of a random hemisphere sample hitting it is exactly zero, so double-counting doesn't happen. But the plan's v2 proposes adding a **sphere light** (nonzero area) — and with NEE + sphere light, you MUST use MIS (multiple importance sampling) to avoid double-counting. The current code will produce correct results for a point light (zero area = zero hit probability in hemisphere sampling), but the terminology is misleading and the v2 extension will be architecturally wrong if this isn't understood.

**More critically:** the current code evaluates direct lighting at EVERY bounce, including secondary bounces. For diffuse surfaces, the contribution at bounce 2+ is `throughput * albedo² * uLightColor * cosTheta * falloff`. This is correct — secondary bounces can see the light directly (e.g., a floor bounce seeing a ceiling light). But it means the PT is already doing what the plan calls "NEE" at every bounce, which contradicts the "v2 adds NEE" claim. v1 IS doing NEE (for a point light); v2 would need to add MIS for sphere lights.

**Fix:** Rewrite the terminology. §4.7 should say "explicit direct lighting evaluation (a form of NEE) at every bounce, which is correct for zero-area point lights. For nonzero-area lights (v2), MIS weighting must be added to avoid double-counting with the hemisphere sample." Also remove or correct the statement that v2 "adds NEE" — v1 already has NEE; v2 adds MIS + sphere lights.

### W2 (HIGH) — The ambient floor addition in PT breaks the "ground truth" framing

The plan says (§10, open question #2):

> "Should ambient floor (`uAmbientBakeStrength`) be added to PT? Yes for fair A/B with cascade renderer that includes it. The reference is 'what the cascade would converge to if integrated infinitely well,' not 'what physical PT gives.'"

This reasoning is flawed. The cascade renderer's ambient floor (`uAmbientBakeStrength = 0.05`) is a **bias** — it adds constant radiance regardless of whether any light reaches the surface. A true path tracer would NOT include this term because it's not physically meaningful (it doesn't correspond to any light source or bounce).

Adding the ambient floor to PT means the PT reference is also biased. The A/B comparison then measures "cascade GI vs PT-with-the-same-bias" — which tells you whether the cascade integration is correct relative to the biased baseline, but NOT whether the overall rendering is physically correct. If both cascade and PT include the ambient floor, you can't tell whether removing the floor improves or worsens physical correctness, because you have no unbiased reference to compare against.

**Two options:**
- **(A) PT WITHOUT ambient floor (recommended).** This gives you a physically-grounded reference. You can then: (1) compare cascade-vs-PT to measure integration error, (2) compare cascade-vs-PT-with-floor-added to measure the floor's effect, (3) decide whether the floor should stay or go based on the unbiased reference.
- **(B) PT WITH ambient floor (plan's current choice).** This gives you "cascade convergence target" — useful for measuring whether cascade GI converges to the biased baseline, but useless for measuring whether the baseline itself is correct.

**Recommendation:** Implement PT WITHOUT ambient floor as the default, and add a `--pt-ambient-floor=N` CLI flag for "cascade convergence target" mode. The unbiased PT is the primary reference; the biased PT is a secondary comparison mode.

### W3 (MEDIUM) — `uCamBasis` as `mat3` with columns (right, up, -forward) needs explicit derivation from the existing camera state

The plan specifies `uCamBasis` as a `mat3` where columns are (right, up, -forward). The existing `Demo3D` class uses a `Camera3DConfig` with `position`, `target`, and presumably yaw/pitch. The derivation from camera config to a basis matrix is not trivial (need to compute right/up/forward vectors from position+target+up-vector). The plan doesn't specify how to compute this on CPU side.

If the basis is computed incorrectly (e.g., column vs row convention mismatch, or forward direction sign), the PT will render from a wrong camera orientation and all convergence will produce a garbage image that doesn't match the cascade renderer's viewpoint.

**Fix:** Add an explicit CPU-side derivation snippet showing how to compute `uCamBasis` from the existing camera state. Example:

```cpp
glm::vec3 forward = glm::normalize(camera.target - camera.position);
glm::vec3 right   = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
glm::vec3 up      = glm::cross(right, forward);
glm::mat3 basis(right, up, -forward);  // columns
```

Also verify this matches the cascade renderer's camera convention (which may use a different up-vector or forward convention).

### W4 (MEDIUM) — The shadow ray `0.02` start offset and the `0.002` surface threshold in `traceSDF` need consistency justification

`traceSDF` uses `dist < 0.002` as the surface-hit threshold. `isDirectlyLit` uses `t = 0.02` as the shadow ray start offset. The `tracePath` loop uses `h.pos + h.normal * 0.002` for self-intersection offset after bounce.

These three values (0.002, 0.02, 0.002) are inconsistent. The shadow ray starts at 10× the surface threshold distance. This means shadow rays skip the first 0.02 units of travel — which can cause light leaks on thin geometry where the surface is within 0.02 units of the hit point on the other side. The cascade renderer uses similar values, but the cascade renderer doesn't trace shadow rays from secondary bounce positions (it traces from the primary hit only).

**Fix:** Use consistent offsets. Either:
- (A) Use `0.002` everywhere (surface threshold, shadow ray start, bounce offset). This is tighter but may cause self-intersection artifacts.
- (B) Use `2 × surface_threshold` everywhere (0.004). This gives a small safety margin while keeping the shadow ray tight.
- (C) Justify the inconsistency explicitly: "shadow rays use a larger offset (0.02 vs 0.002) because they start from a potentially-noisy SDF-normal offset, and the larger offset avoids self-shadowing on secondary surfaces that are close to the primary hit."

### W5 (MEDIUM) — The `uTanHalfFovY` + `uViewportSize` camera parameterization differs from the cascade renderer's camera setup

The cascade renderer uses Raylib's `Camera3D` which has `fovy`, `position`, `target`, and `up`. The PT shader uses `uCamPos`, `uCamBasis` (mat3), and `uTanHalfFovY`. These are equivalent but require careful derivation. The plan doesn't specify whether the cascade renderer's aspect ratio handling (which may differ from `uViewportSize.x / uViewportSize.y` if the render target isn't full-screen) is correctly reflected in the PT shader.

If the cascade renderer renders to a non-fullscreen viewport (e.g., for the radiance debug viewer occupying part of the screen), the PT shader's `uViewportSize` must match the cascade render's actual viewport dimensions, not the full window size.

**Fix:** Use the same viewport resolution that `raymarch.frag` uses. Pass it as a uniform from the same source that `raymarch.frag` gets its resolution from. Don't independently compute it from `GetScreenWidth/Height`.

### W6 (MEDIUM) — Progressive accumulation formula is correct but the implementation detail in `main()` has a subtle precision issue for large spp

The accumulation formula: `merged = (prev.rgb * totalBefore + frameSum) / totalAfter`. For `totalBefore` approaching 10,000+ and `frameSum` being a small addition, the multiply `prev.rgb * 10000` can produce large intermediate values in RGBA32F (max ~3.4 × 10³⁸ for float32, so numerically safe for reasonable radiance values). However, the `imageLoad → multiply → divide → imageStore` roundtrip introduces float32 rounding at each step. After ~10,000 samples, the per-frame contribution is ~1/10000 of the running mean, and float32 has ~7 decimal digits of precision — so the per-frame update is at the ~0.001 relative precision level. This is fine for visual convergence but may produce measurable drift if the reference is used for pixel-exact RMSE comparison.

**Not a blocker for v1** — float32 is sufficient for visual reference. But if PT output is later used for per-pixel RMSE comparison at <0.001 precision, a float64 accumulation buffer or a "store running sum + spp separately" approach would be needed. Document this as a v2 precision consideration.

### W7 (LOW) — The plan says "cascade pipeline continues to run (wasteful but simple)" without quantifying the waste

§5.4 and §8 acknowledge that the cascade pipeline runs even when PT is selected. At 1080p, cascade bake + raymarch costs ~50 ms per frame (per Phase 1 RenderDoc data). PT itself costs ~10 seconds per frame (plan's estimate). So the cascade overhead is ~50 ms / 10,000 ms = 0.5% — negligible. But at lower resolutions or with faster PT dispatch, the cascade overhead becomes proportionally larger. The plan should quantify this: "cascade overhead is ~0.5% of total frame time at 1080p with 6 spp; acceptable for v1."

### W8 (LOW) — The `sampleSDF` helper is referenced but its definition in `radiance_3d.comp` may not be portable to `pt_reference.comp`

The plan says `traceSDF` adapts `raymarchSDF` from `radiance_3d.comp`. But `raymarchSDF` in `radiance_3d.comp` may depend on uniforms or helper functions specific to that shader (e.g., `uGridOrigin`, `uGridSize`, `uVolumeMax`, SDF sampling conventions). The PT shader needs the same helpers but in a different compilation unit. GLSL compute shaders don't share code across compilation units — `pt_reference.comp` must either:
- (A) Duplicate the relevant helper functions (RNG, SDF sampling, etc.) into its own file
- (B) Use `#include` (not standard GLSL; needs extension or build-system support)
- (C) Link shared code via a common header (requires CMake shader-inclusion preprocessing)

The plan doesn't address this. For a ~250-line shader, option A (duplicate) is pragmatic but creates the same maintenance coupling issue as critic 16 W1 (duplicated `sampleProbeDirWithLeak`). If `sampleSDF` or `raymarchSDF` is later modified in `radiance_3d.comp`, the PT reference silently diverges.

**Fix:** Document the duplication explicitly and add a comment in `pt_reference.comp`: "SDF helpers duplicated from radiance_3d.comp. Any changes to SDF intersection in radiance_3d.comp MUST be mirrored here."

### W9 (LOW) — RNG seeding strategy may produce correlation patterns

The RNG seed: `hash(uint(pix.x) ^ (uint(pix.y) << 16) ^ (uFrameIndex * 2654435761u))`. This is a standard frame-index-seeded hash. But with 6 rays per pixel per frame, all 6 rays for a given pixel share the same initial seed (the frame seed) and diverge via the LCG sequence. At low spp, adjacent pixels may show correlation because their initial seeds differ by only 1 in `pix.x`, and `hash(x)` for small x differences can produce correlated outputs depending on the hash function quality.

**Not a blocker for v1** (convergence at 10k spp will average out any correlation). But the plan should note that blue-noise sampling (referenced in §10 risks) would improve convergence at low spp and reduce visible correlation artifacts.

---

## Summary

A well-scoped, well-structured plan for a much-needed ground-truth reference. The main concerns are:

1. **W1 (HIGH):** Direct lighting is already NEE (not "implicit direct") — the terminology is wrong and the v2 "adds NEE" claim is incorrect. v1 does NEE for point lights; v2 needs MIS for sphere lights.
2. **W2 (HIGH):** Adding the ambient floor to PT compromises its ground-truth status. PT should default to NO ambient floor (unbiased reference), with an opt-in biased mode for cascade convergence comparison.
3. **W3 (MEDIUM):** Camera basis derivation from existing camera state is unspecified — wrong basis = garbage PT output.
4. **W4 (MEDIUM):** Inconsistent self-intersection offsets (0.002 surface threshold vs 0.02 shadow ray start) need justification or normalization.
5. **W8 (LOW):** SDF helper duplication between `radiance_3d.comp` and `pt_reference.comp` creates maintenance coupling — document it explicitly.