# Phase 2.6 — Standby Notes (NOT a Plan)

**Date:** 2026-05-15
**Status:** **Filed indefinitely. Reading the ShaderToy ground-truth shifted my recommendation: probably don't attempt Phase 2.6 at all.** ShaderToy's `WeightedSample` (the actual reference algorithm for RC visibility) uses **binary** visibility, not soft α. Our pursuit of "soft α via smoothstep on something" was an unmotivated invention; ground-truth doesn't validate it.

This is a standby doc, not a plan. If anyone returns to soft α, this is the starting context.

---

## The original Phase 2.6 framing (which I no longer endorse)

Phase 2.5d's M1 measurement showed that Phase 2.5b's binary-α surface bins flipped between α=0 and α=1 abruptly at hit/miss boundaries, causing visible per-cell-boundary flicker. The proposed "soft α" was a smoothstep over some metric that gradually transitions α between the boundaries.

Three candidate metrics were filed (post-critic-11):
1. **Ray-vs-surface-normal angle** (`dot(rayDir, normalize(grad SDF))`) — would need 6 SDF samples per bin to estimate the gradient via finite differences
2. **Distance from hit to nearest probe-cell boundary** — purely geometric, cheap
3. **Hit-distance fraction within cascade interval** (`hit.a / tMax`) — overlaps with the existing smoothstep `l`

Phase 2.5b tried a fourth (SDF-half-voxel-before-hit); it failed Tier 3 (Sponza dimmed 32%).

---

## What ShaderToy actually does (post-2026-05-15 ground-truth review)

Reference: [shader_toy/CubeA.glsl:21-42](../../shader_toy/CubeA.glsl) — the `WeightedSample` function.

```glsl
vec4 WeightedSample(vec2 luvo, vec2 luvd, vec2 luvp, vec2 uvo, vec3 probePos,
                    vec3 gTan, vec3 gBit, vec3 gPos, float lProbeSize) {
    // Compute upper probe's world position
    vec3 lastProbePos = gPos + gTan*(luvp.x*lProbeSize/256.) + gBit*(luvp.y*lProbeSize/256.);
    vec3 relVec = probePos - lastProbePos;

    // Cone half-angle for the upper probe's directional bin
    float theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*PI*0.5;

    // Pick the upper probe's direction bin closest to (-relVec) — i.e., the
    // direction the upper probe would have looked AT this lower probe
    float phi = atan(-dot(relVec, gTan), -dot(relVec, gBit));
    // ... bin index calculation ...

    // Read the upper probe's stored ray distance for that direction bin
    float lProbeRayDist = TextureCube(luvo + floor(phiUV)*uvo + luvp).w;

    // Visibility test:
    if (lProbeRayDist < -0.5                                    // sky exit (always visible)
        || length(relVec) < lProbeRayDist*cos(PI*0.5 - theta) + 0.01) {
        // Visible — sum 4 directional bins of upper probe
        return vec4(SUM_OF_4_BINS_RGB, 1.0);
    }
    return vec4(0.);
}
```

**Key observations:**

1. **Visibility output is BINARY** — `vec4(SUM, 1.0)` if visible, `vec4(0.)` if occluded. **No soft α.**
2. **The "soft" behavior comes from BILINEAR INTERPOLATION across 4 upper probes** ([CubeA.glsl:207-216](../../shader_toy/CubeA.glsl#L207)). Some of the 4 corners may be visible, others not. Their 0/1 weights interpolate to fractional values via the bilinear `mix(mix(S0, S1, fx), mix(S2, S3, fx), fy)`. The **smoothness is a 2D probe-grid interpolation effect**, not a smoothstep on a scalar.
3. **The "head-on vs grazing" distinction we wanted soft α to capture** isn't present in `WeightedSample`. Instead, `WeightedSample` uses **the upper probe's stored ray distance** as the visibility-relevant data — exactly like our shipped Phase 1 Mode 4.
4. **The cone-angle correction** (`cos(PI*0.5 - theta)`) IS there — same idea as the "Path A cone correction" we filed pre-Phase-1. ShaderToy validates that this is a real concern.

---

## Implication for Phase 2.6

**Stop pursuing "soft α via smoothstep on a metric."** None of the four proposed metrics (SDF-proximity, ray-normal dot, cell-boundary distance, hit-distance fraction) match what ShaderToy does. Continuing this line is unmotivated by ground truth.

If the goal is "smoother hit/miss boundaries," the actual ShaderToy approach is **bilinear interpolation across upper-cascade probes with binary per-corner visibility**. Our renderer already does this (the trilinear-in-3D version of ShaderToy's bilinear-in-2D); the smoothness ceiling we're at is already roughly what ShaderToy gets.

**The real source of remaining hit/miss flicker** in our project (if anyone observes it) is more likely:
- Octahedral non-uniformity (per Phase 2 critic chain H2)
- Near-cell-boundary trilinear interpolation across probes that disagree (pre-existing 2D smoothness vs 3D)
- Per-frame jitter cycling in temporal accumulation (Phase 9 probe jitter)

**None of these is soft α's problem.** Phase 2.6 as originally framed is filed dead — pursue the actual concern via a different name if it ever surfaces.

---

## What "Phase 2.6" might productively be (if revisited)

If someone later finds visible artifacts and labels them "Phase 2.6":

| Observed artifact | Probable cause | Recommended fix |
|---|---|---|
| Hit/miss flicker at probe-cell boundaries | Trilinear interpolation across 8 corners with sharply-different α | Add the **ShaderToy cone-correction** to our existing per-bin α derivation (at bake time, not render). Equivalent to the deferred "Path A cone correction" from the unified plan. |
| Banding aligned with octahedral bin boundaries | Octahedral non-uniformity | Per-bin solid-angle weighting in the bake (currently uniform); see Phase 2 critic chain H2 |
| Banding aligned with cascade interval boundaries | The cascade-handoff smoothstep `l` itself | Already as smooth as ShaderToy's; not fixable without architectural change |
| Cross-wall light bleed | The actual bake-side leak (Phase 3 territory) | Phase 3 (`WeightedSample` at bake time) |

**None of the above is "soft α via smoothstep on SDF proximity / hit angle / cell-boundary distance / hit-distance fraction."** That entire design space is filed dead per ShaderToy ground truth.

---

## Trigger conditions for actually planning Phase 2.6

Don't draft a Phase 2.6 plan unless:
1. A user reports a SPECIFIC visible artifact, AND
2. The artifact's mechanism is identified (e.g., "octahedral banding visible here"), AND
3. The mechanism maps to one of the rows in the table above (NOT to "soft α via smoothstep on X")

Otherwise: Phase 2 is the production ceiling for the current architecture. Acknowledge and move on.

---

## What was filed in the impl docs (for cross-reference)

- [Phase 2.5b's failure analysis](visibility_phase2.5_impl.md#L139) — the SDF-proximity smoothstep that didn't work
- [Phase 2.5d M1 histogram](visibility_phase2.5d_impl.md) — the bimodal data confirming SDF proximity is a poor metric
- [Critic 10 W8](critic/10_visibility_phase2.5d_impl_review.md) — the SDF-scalar-vs-gradient distinction
- This doc — **closure on the entire soft-α direction**

If someone restarts soft α work, they should read all four AND the ShaderToy `WeightedSample` reference before writing one line of code.
