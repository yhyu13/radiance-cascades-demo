# Phase 3 — Standby Notes (NOT a Plan)

**Date:** 2026-05-15
**Status:** **Not a plan. Standby brief informed by ShaderToy ground-truth review.** The bake-side leak fix Phase 2 didn't deliver IS implementable on our current architecture if anyone wants to attempt it — ShaderToy's `WeightedSample` is the algorithm. This doc captures what's known so a future attempt doesn't redo the investigation.

---

## The problem

Per [visibility_phase2_impl.md "What Phase 2 actually does"](visibility_phase2_impl.md):

> Render-time leaks: FIXED. Bake-time leaks: NOT FIXED. The bake's `rad = hit.rgb * l + upperDir.rgb * (1 - l)` formula is unchanged. When the smoothstep `l < 1` (within `blendWidth` of the interval far edge), upper-cascade radiance — which can include "what's beyond the wall" — is mixed into this bin's stored `rad`. The render-side α=0 then HIDES this leaked value from the rendered output, but the leaked value still lives in the atlas.

Per [visibility_phase2.5_impl.md §2.5a.1 baseline (corrected v2)](visibility_phase2.5_impl.md):

| Cascade | Probes in alcove | Bins counted | **Leak sum** |
|---|---:|---:|---:|
| C0 | 6144 | 44925 | **4373.5** |
| C1 | 768 | 34954 | 2864.9 |
| C2 | 64 | 3439 | 309.7 |
| C3 | 16 | 299 | 86.4 |

**The 4373.5 in C0 is the success criterion** for Phase 3: drive this number toward zero.

---

## Why Phase 2's earlier attempts failed

[Phase 2 impl doc §"Why v1 over-darkened"](visibility_phase2_impl.md) recorded:

> The textbook RC interval merge (`rad = thisRad + thisAlpha × upperDir.rgb`; binary `thisAlpha`) over-darkened Sponza by 23% because the chained α-multiply across C3→C2→C1→C0 terminates radiance at the first opaque cascade. Most C0 bins in Sponza hit something (closed architecture), so most surface-hit bins lose all far-field contribution.
>
> **The geometric mismatch**: the textbook formula assumes probes along the ray are stacked at increasing distances, looking in the same direction. With non-co-located probes (Phase 5d), the upper cascade's probe is OFFSET — its `bdir` ray from a different position can legitimately see past the local wall.

[Critic-10 H1 on the rev-1 plan](critic/10_visibility_phase1.5_and_phase2_plan_review.md):

> The rev-1 "geometry-aware merge formula" was wrong in three ways: coordinate systems mixed up between cascade grids, lateral wall topology not addressed, didn't account for what upper cascade actually contains.

So the failed direction was: try to derive a formula that compensates for non-co-located probes. **My derivation was wrong; finding the right one is research-level work.**

---

## What ShaderToy does (the answer was here all along)

Reference: [shader_toy/CubeA.glsl:21-42 `WeightedSample`](../../shader_toy/CubeA.glsl) and [CubeA.glsl:194-219 cascade merge](../../shader_toy/CubeA.glsl#L194).

ShaderToy uses **per-corner geometric visibility** at bake time. For each of the 4 upper-cascade probes that the lower probe trilinear-interpolates over (2D bilinear in ShaderToy; we'd be 3D trilinear with 8 corners):

```glsl
vec4 WeightedSample(...) {
    // 1. Compute upper probe's world position
    vec3 lastProbePos = gPos + gTan*offsetX + gBit*offsetY;

    // 2. Compute relative vector from upper probe to lower probe
    vec3 relVec = probePos - lastProbePos;

    // 3. Pick the upper probe's direction bin closest to (-relVec) — i.e., the
    //    direction the upper probe would have looked AT this lower probe
    float phi = atan(-dot(relVec, gTan), -dot(relVec, gBit));
    // ... bin index calc ...

    // 4. Read the upper probe's stored ray distance for that bin
    float lProbeRayDist = TextureCube(luvo + floor(phiUV)*uvo + luvp).w;

    // 5. Geometric visibility check: is the lower probe within the wall the
    //    upper probe's ray hit?
    float theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*PI*0.5;
    if (lProbeRayDist < -0.5                                  // sky exit (always visible)
        || length(relVec) < lProbeRayDist*cos(PI*0.5 - theta) + 0.01) {
        // VISIBLE — contribute upper probe radiance
        return vec4(SUM_OF_UPPER_BINS_RGB, 1.0);
    }
    // OCCLUDED — return zero
    return vec4(0.);
}
```

The merge then uses the bilinear-interpolated visibility weights as the divisor:

```glsl
vec3 lastOutput = mix(mix(S0.xyz, S1.xyz, fx), mix(S2.xyz, S3.xyz, fx), fy)
                  / max(0.01, mix(mix(S0.w, S1.w, fx), mix(S2.w, S3.w, fx), fy));
```

**This is renormalization-by-visible-corner-count.** Same pattern as our Phase 2 v5 (which I picked empirically — turns out it matches ShaderToy by accident).

---

## Why this is THE correct algorithm for our current architecture

It's the algorithm Phase 2 should have used. It works on volumetric probes too (just becomes 8-corner trilinear instead of 4-corner bilinear). It addresses every concern:

- **Bake-side leak fix**: the visibility check at bake time prevents lower probes from inheriting upper probes' radiance through walls. Atlas `rad` no longer carries leaked values.
- **Non-co-located probes (Phase 5d)**: explicitly handled by `relVec = probePos - lastProbePos`. The cone-angle correction `cos(PI*0.5 - theta)` accounts for the upper probe's own bin acceptance angle.
- **No over-darkening like the textbook merge**: occluded corners return `vec4(0.)` and don't contribute to the divisor; visible corners' RGB is used unmodified. The final mix is over visible corners only.

**This is what the rev-1 plan's hand-waved "geometry-aware merge" was groping toward, expressed correctly.**

---

## What an actual Phase 3 implementation would look like

### 3D adaptation of `WeightedSample` for our atlas

Pseudo-code in `radiance_3d.comp`:

```glsl
vec4 weightedSample3D(ivec3 lowerProbePos, ivec3 upperProbePos, vec3 lowerProbeWorld, vec3 rayDir) {
    // 1. Upper probe world position (use existing per-cascade cellSize derivation)
    vec3 upperProbeWorld = uUpperGridOrigin + (vec3(upperProbePos) + 0.5) * uUpperCellSize;

    // 2. Relative vector
    vec3 relVec = lowerProbeWorld - upperProbeWorld;

    // 3. Pick upper probe's bin in -relVec direction (octahedral encoding)
    ivec2 upperBin = dirToBin(normalize(-relVec), uUpperDirRes);

    // 4. Read upper probe's atlas value (.rgb = radiance, .a = stored hit-distance from Phase 1 Mode 4)
    vec4 upper = texelFetch(uUpperCascadeAtlas,
                            ivec3(upperProbePos.x * uUpperDirRes + upperBin.x,
                                  upperProbePos.y * uUpperDirRes + upperBin.y,
                                  upperProbePos.z), 0);

    // 5. Cone-angle (for D=8: ~22.5° half-angle since each bin covers ~PI/D rad)
    float theta = PI / float(uUpperDirRes);
    float cosTerm = cos(PI * 0.5 - theta);

    // 6. Visibility check
    bool visible = (upper.a < 0.0)                              // sky exit
                || (length(relVec) < upper.a * cosTerm + 0.01); // within cone

    return visible ? vec4(upper.rgb, 1.0) : vec4(0.0);
}
```

Then in the cascade-merge per direction loop:

```glsl
// Replace the existing `vec3 upperDir = ...` block with 8-corner WeightedSample
vec4 corners[8];
for (int i = 0; i < 8; ++i) {
    ivec3 upperPC = upperP000 + offsets[i];
    corners[i] = weightedSample3D(probePos, upperPC, probeWorld, rayDir);
}
// Trilinear interpolation
vec4 upperDir = trilinearMix(corners, triF);
// Renormalize over visible corners
vec3 upperRad = upperDir.rgb / max(0.01, upperDir.a);
```

The rest of the bake's existing radiance formula (`rad = hit.rgb * l + upperRad * (1 - l)`) stays unchanged.

### Cost estimate (rough)

Per direction bin in the bake:
- Current Phase 2 bake: 1 fetch (upper cascade probe) → 8 fetches if non-co-located trilinear
- WeightedSample bake: same 8 fetches BUT each goes through `weightedSample3D` adding ~10 ALU ops (length, cos, compare) per fetch = +80 ops/bin × D² bins/probe × probe count

For C0 at 32³ probes, D=8: 32³ × 64 × 80 = ~165M extra ops per bake. At 0.47 TFLOP-effective per Phase 1's measurement: **~0.35 ms added per bake**. Bake currently takes ~38 ms total (per Phase 1 RenderDoc); WeightedSample-style merge would push it to ~38.5 ms. **Cost is negligible.**

Render-side: unchanged.

### Verification

1. Build clean.
2. Run `--bake-leak-test=tools/phase3_bake_leak.json --load-obj=cornell-orig-alcove --exit-frames=400`. **Pass criterion: C0 leak_sum drops from 4373.5 to < 500** (90%+ reduction).
3. Run quality A/B: Sponza/Cornell at cam.md. **Pass criterion: RMSE vs Phase 2 baseline ≤ 0.05** (don't introduce new artifacts; ideally improve).
4. RenderDoc bake timing: confirm < 5% bake cost increase.

### Risks

- **The 3D adaptation of `WeightedSample` may have failure modes the 2D version doesn't.** Specifically, the cone angle `theta = PI/D` is a 2D wall-attached probe assumption; 3D octahedral bins have different solid-angle geometry per critic 7 H2.
- **Our probes are volumetric, not wall-attached.** `WeightedSample`'s `gTan`/`gBit`/`gNor` don't exist for our probes. The pseudo-code above uses world-space `relVec` directly, which is correct for volumetric probes BUT the cone correction may be over- or under-applying without the wall normal as reference.
- **Even if the visibility test is right, the merge formula might still over-darken in edge cases** (e.g., probes near walls where multiple corners are occluded). May need iteration similar to Phase 2 v1→v5.

---

## Trigger conditions for actually planning Phase 3

Don't draft a Phase 3 plan unless one of these is true:

1. **A user reports cross-wall light bleed in Mode 0 at a viewpoint where it visually breaks the scene** (currently nothing reported; render-side α-gate hides the bake leak from output).
2. **A future feature requires the atlas to be leak-free** (e.g., specular GI that reads atlas RGB directly without honoring α; or atlas-based GI in a different renderer).
3. **The atlas debug viewer becomes a primary debugging tool** and the leak-visible-in-viewer is operationally annoying.

The Phase 3 implementation outlined above is **plausibly correct based on ShaderToy** but has not been derived from first principles or tested. Before drafting a real plan, do the literature follow-up:

- Sannikov's RC Shadertoy series (the WeightedSample author's other implementations)
- Burgess' RC paper (the original)
- Any followup work on RC + non-co-located probes specifically
- Whether anyone has published the 3D adaptation already

---

## What was filed in the impl docs (for cross-reference)

- [Phase 2.5_plan rev 2 §"What was scope-cut"](visibility_phase2.5_plan.md): "filed as Phase 3 (research-level)"
- [Phase 2.5_impl §"Bake-leak quantitative test deferred"](visibility_phase2.5_impl.md): the 4373.5 baseline + the metric tooling
- [Phase 2.5d_impl M1](visibility_phase2.5d_impl.md): the bimodal histogram (incidentally relevant to Phase 3 — confirms most surface bins have small but nonzero distance to wall, validating the WeightedSample geometric test)
- [Critic 10 H1 (plan rev 1)](critic/10_visibility_phase1.5_and_phase2_plan_review.md): why my hand-waved geometry-aware formula was wrong; ShaderToy's WeightedSample is what I was trying to derive
- This doc — Phase 3's correct algorithm + cost estimate + risks

If someone implements Phase 3, they should read all five AND the ShaderToy `WeightedSample` reference before writing one line of code.

---

## Bottom line

**Phase 3 is more tractable than the rev-1 plan made it sound.** ShaderToy `WeightedSample` is the algorithm; the 3D adaptation is mechanical (with risks called out above); the cost is negligible (~0.5 ms bake increase); the success criterion is defined (drive 4373.5 → <500).

**But it's still ~1 week of careful implementation + verification on a problem that has no user impact today.** Not urgent. Phase 2's render-side fix is the ceiling for "what users actually see"; Phase 3 is for "what the atlas actually contains" — only matters if a downstream feature reads the atlas without α-gating.
