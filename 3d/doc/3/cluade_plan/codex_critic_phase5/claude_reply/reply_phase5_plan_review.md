# Reply — Phase 5 Plan Review

**Reviewed:** `codex_critic_phase5/01_phase5_plan_review.md`  
**Date:** 2026-04-24  
**Status:** All six findings accepted. Plan revised accordingly.

---

## Finding 1 (High): Final image integration path not specified

**Accept. The display path needs an explicit ownership model.**

`raymarch.frag` reads `texture(uRadiance, uvw).rgb` from the isotropic `probeGridTexture`. The Phase 5 atlas changes the bake path but leaves the display path dangling. Two options exist:

1. Add a reduction pass after the atlas bake: average all D² directional bins per probe → write averaged radiance back into `probeGridTexture`. The display path is then unchanged.
2. Rewrite `raymarch.frag` to sample the atlas directly using the ray direction at each point.

**Decision: Option 1 — atlas reduction pass.** It isolates Phase 5 changes to the compute/bake path and keeps `raymarch.frag` stable. The reduction is cheap: one compute dispatch, D² texture reads per probe.

**Plan update:** Added "Phase 5b-1 — Atlas reduction pass" as an explicit sub-step between 5b (atlas write) and 5c (directional merge). The reduction dispatch runs after the atlas bake for each cascade and writes the averaged `probeGridTexture`.

---

## Finding 2 (High): Linear filtering bleeds across direction bins

**Accept. `texture()` with normalized UVs and GL_LINEAR will smear adjacent probe tiles and direction bins.**

The atlas is a packed 3D texture where each D×D tile belongs to a single probe. A bilinear tap near a tile boundary will interpolate with the neighboring probe's first bin — a different probe, different world position. This directly corrupts the directional lookup and defeats the atlas.

**Fix: all directional atlas reads use `texelFetch()` with integer texel coordinates.**

```glsl
// Old (incorrect for atlas):
texture(uUpperCascadeAtlas, vec3(atlasUV, atlasW)).rgb

// New (correct):
texelFetch(uUpperCascadeAtlas,
           ivec3(probePos.x * uDirRes + dx,
                 probePos.y * uDirRes + dy,
                 probePos.z), 0).rgb
```

The atlas texture can keep GL_LINEAR for the reduction-pass averaged readback, but the directional merge path uses `texelFetch` exclusively. The atlas sampler binding is changed to `usampler3D` or integer-fetch-capable uniform.

**Plan update:** All atlas merge lookups replaced with `texelFetch()`. The plan's pseudo-code updated to use integer coordinates throughout 5c.

---

## Finding 3 (High): Fixed D=4 (16 bins) is an unjustified quality regression for upper cascades

**Accept. C3 drops from 64 rays to 16 — a 4× angular budget cut.**

The ShaderToy scales `probeSize = 2^(ci+1)` giving C0:4, C1:16, C2:64, C3:256 directional bins. That scaling exists for a reason: upper cascades cover more of the scene and need more directional resolution to avoid low-frequency angular aliasing.

Cutting C3 from 64 to 16 is a pre-committed quality regression without runtime evidence that 16 is sufficient. That evidence should come first.

**Decision: implement D=4 (16 bins) for all cascades as the starting point; add an explicit A/B step (5e) to compare D=4 vs D=8 for upper cascades before locking in the budget.**

If the A/B shows visible banding or color-bleed regression vs Phase 4, adopt per-cascade D scaling:
- D_ci = 2^(ci+1): C0=2, C1=4, C2=8, C3=16 (matching ShaderToy ratios)

Memory cost at per-cascade scaling (32³ grid, RGBA16F):
- C0: 64×64×32 = 0.25 MB
- C1: 128×128×32 = 1 MB
- C2: 256×256×32 = 4 MB
- C3: 512×512×32 = 16 MB
- Total: ~21 MB — acceptable on RTX 2080 SUPER (8 GB VRAM)

**Plan update:** D=4 is the starting point; per-cascade scaling is the upgrade path documented in Phase 5e.

---

## Finding 4 (Medium): Equal solid-angle stop condition is false for octahedral bins

**Accept. Octahedral equal-area UV bins are NOT equal solid-angle on the sphere.**

The incorrect stop condition was: "each bin covers 1/16 of the sphere solid angle."

Octahedral parameterization maps the sphere uniformly in L1 distance, not in solid angle. Near-polar bins (small |z|) subtend slightly different solid angles than near-equator bins. This is an intrinsic property of the octahedral map and does not prevent its use as a direction encoding — it just means the stop condition was wrong.

**Corrected stop conditions for 5a:**
- Bin indexing is stable and deterministic: `dirToBin(binToDir(b, D), D) == b` for all valid b
- Full sphere coverage: no direction produces an out-of-range bin index
- Visual quality is acceptable at D=4: GI image shows directional color separation, no obvious pole artifacts

**Plan update:** Stop condition table in 5a corrected.

---

## Finding 5 (Medium): Phase 5d is irrelevant for co-located probes

**Accept. Phase 5d is a no-op by the plan's own analysis and should not be in the implementation path.**

The plan itself concludes: all cascades share the same 32³ grid at the same origin → `probeToWorld(probePos) == worldPos` → `probeDist == 0` → visibility check trivially passes. Implementing a check that always returns `true` adds code complexity with zero visual benefit.

**Decision: Phase 5d is removed from the numbered sub-phases.** It is preserved as an architecture note at the end of the plan: "visibility weighting (ShaderToy `WeightedSample()`) becomes relevant only if cascades adopt different spatial resolutions."

**Plan update:** 5d section removed from implementation table. Content moved to architecture notes.

---

## Finding 6 (Medium): "No spatial interpolation" should be framed as a 3D adaptation, not ShaderToy parity

**Accept. The framing was misleading.**

"No spatial interpolation needed" reads as "we've achieved the same result as ShaderToy's 4-neighbor merge, just more simply." That is not correct. It means the current architecture avoids the problem by construction, not that it solves it the same way.

**Corrected framing (added to plan):**

> This is a deliberate 3D volumetric architecture choice. The ShaderToy's 4-neighbor spatial interpolation exists because its probe grid is surface-attached with different spatial densities per cascade. The current implementation uses co-located 32³ grids across all cascades, which eliminates spatial probe-position misalignment by construction. This is a 3D adaptation, not a simplification of the ShaderToy merge.

**Plan update:** Architecture note rewritten to make the adaptation explicit.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Final image integration not specified | High | **Fixed** — atlas reduction pass (5b-1) writes averaged `probeGridTexture`; display path unchanged |
| Linear filtering bleeds across bins | High | **Fixed** — all atlas merge reads switched to `texelFetch()` with integer coordinates |
| Fixed D=4 regresses upper cascade angular budget | High | **Fixed** — D=4 is starting point; 5e A/B decides whether per-cascade scaling is needed |
| Equal solid-angle stop condition false | Medium | **Fixed** — replaced with stable-indexing + full-sphere-coverage + visual quality checks |
| Phase 5d is a no-op for co-located probes | Medium | **Fixed** — 5d removed from implementation plan; preserved as architecture note |
| No-spatial-interpolation framing misleading | Medium | **Fixed** — reworded to explicit 3D adaptation language |

All six findings accepted. `phase5_plan.md` updated.
