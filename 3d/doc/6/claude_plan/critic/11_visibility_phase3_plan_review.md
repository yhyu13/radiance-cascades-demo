# Critique: Phase 3 — Bake-Side Leak Fix via 3D `WeightedSample`

**Document reviewed:** `visibility_phase3_plan.md` (rev 2, post-critic-13 integration)
**Date:** 2026-05-15

---

## Strengths

1. **Mechanical translation, not research — honest framing.** The plan correctly identifies that ShaderToy's `WeightedSample` is a proven algorithm, and the 3D volumetric adaptation is a "mechanical translation" with known differences (2D→3D, wall-attached→volumetric, 4→8 corners). This avoids the research-risk trap that Phase 2.5b fell into (inventing a novel SDF-proximity soft-α metric).

2. **Critic 13 fixes landed substantively.** H1 (`dirToLower` sign), H2 (missing uniforms → now `uUpperGridOrigin`/`uUpperCellSize`), H3 (single-bin merge regress → `sampleUpperDir` preserves `uUseDirBilinear`) — all three are real algorithmic bugs that would have produced wrong results or silently regressed Phase 5f. The fixes are concrete and explained with reference to ShaderToy's actual conventions.

3. **Tiered verification gates.** Tier 1/2/3 with quantitative thresholds (C0 ≤ 500 for ship, 500-2000 for partial, >2000 for revert) is the right pattern. Each tier has a clear action. This avoids the "ambiguous pass" problem from Phase 1's decision gate.

4. **Cascade ordering verified empirically.** Checking `demo3d.cpp:2257` to confirm top-down dispatch, then removing v4 from the iteration backlog, is a good example of verifying assumptions before planning around them.

5. **Bake merge formula change (critic 13 L2) is load-bearing and acknowledged.** The `* upperDir.a` factor is what makes Phase 3 actually work — without it, the visibility check is a no-op. The plan identifies this as fragile (future reader might think `.a` was always 1.0 and drop it) and recommends a shader comment. This is a real maintenance risk that deserves the attention.

6. **Three sub-phases with reversible checkpoint.** 3a is bit-exact (safe to ship even if 3b fails), 3b has a revert path, 3c is cleanup. This matches the Phase 2 pattern and is structurally sound.

---

## Weaknesses / Concerns

### W1 (HIGH) — The visibility test reads `.a` from the upper-cascade atlas, but Phase 2 redefined `.a` as α-transparency (not hit-distance)

Phase 2 changed the atlas's α channel semantics:
- Surface hit → α = 0 (opaque)
- Sky exit → α = 0 (terminal)
- In-volume miss → α = 1 (transparent)

Phase 3's algorithm reads:
```glsl
float lProbeRayDist = texelFetch(uUpperCascadeAtlas, ...).a;
bool visible = (lProbeRayDist < 0.0) || (length(relVec) < lProbeRayDist * cosCorrection + 0.01);
```

This code expects `.a` to be **hit-distance** (the value Mode 4 stored in `.a`). But Phase 2 wrote α-transparency values (0 or 1) to `.a`. After Phase 2's 2C cleanup, `uVisibilityMode` and Mode 4 are deleted — the shader no longer stores hit-distance anywhere.

**Consequences:**
- `lProbeRayDist` will be 0 (surface hit/sky) or 1 (in-volume miss), not a distance in world units.
- `lProbeRayDist < 0.0` will NEVER be true — sky exit α=0, not the negative sentinel Mode 4 used.
- `length(relVec) < lProbeRayDist * cosCorrection + 0.01`: for in-volume miss bins, `lProbeRayDist = 1`, so `1 * 0.248 + 0.01 = 0.258`. This means ANY surface within 0.258 world units of the upper probe is "visible" — a fixed radius, not a geometric cone test. For surface-hit/sky bins, `lProbeRayDist = 0`, so `length(relVec) < 0.01` — only surfaces within 0.01 world units are "visible."

This is **not the WeightedSample algorithm** — it's a broken hybrid that uses α-transparency values as if they were hit-distances. The plan's entire algorithm is predicated on `.a` carrying hit-distance from Mode 4, which Phase 2's 2C cleanup deleted.

**Fix needed:** Phase 3 must restore hit-distance storage to the atlas. Either:
- (A) Write hit-distance to `.a` during the bake (reverting Phase 2's α=0/1 encoding to something that carries distance for surface hits, α=1 for misses, negative for sky). This would break Phase 2's render-side α-gate (`w = wcos × a.a`) unless the render shader distinguishes "distance" from "transparency" — which it can't from a single channel.
- (B) Store hit-distance in a **separate channel or texture**. E.g., a parallel `uUpperCascadeHitDist` texture (RGBA16F, only `.a` used per directional bin). Adds one texture fetch per look-back; ~2× the atlas memory for the upper cascade's hit-distance data.
- (C) Encode both hit-distance and α in `.a` using a convention (e.g., positive values = hit-distance, 0 = sky/miss, negative = transparent). Fragile; hard to maintain.

This is a **plan-breaking issue** — the algorithm spec as written cannot execute correctly against the current atlas format.

### W2 (HIGH) — The plan claims "render-side behavior unchanged" but Phase 3 changes the atlas's `.a` content

§6 "Honest risks" says: "Phase 3 changes the atlas content AND the bake's merge formula (per L2 fix: `* upperDir.a`), but not the render math." The render math is `w = wcos × a.a` (Phase 2's α-gate). If Phase 3 writes different `.a` values (e.g., restoring hit-distance per W1 above), the render-side α-gate gets different inputs. If `.a` is now hit-distance (e.g., 2.4 for a surface hit at 2.4 units), `w = wcos × 2.4` — the irradiance contribution would be massively over-weighted. This would catastrophically break the render.

The plan cannot simultaneously have: (a) `.a` carrying hit-distance for Phase 3's bake-side visibility test, and (b) `.a` carrying α-transparency for Phase 2's render-side α-gate. **These are conflicting channel semantics.**

The plan needs to resolve this before it can be executed. Options:
- **Separate textures** (W1 option B above) — `.a` in the main atlas stays as α-transparency (Phase 2 unchanged); a new `uHitDistAtlas` texture stores hit-distance for the bake-side look-back. Two `.a` channels serving two purposes. Render-side only reads the main atlas; bake-side reads both.
- **Dual encoding** — the bake writes `.a` = α-transparency for the render, and a separate store writes hit-distance to a different resource. The bake shader already has access to `hit.a` (the raw raymarch result); it just needs to store it somewhere the upper-cascade look-back can read.

### W3 (MEDIUM) — `uUpperCascadeAtlas` vs `uUpperCascade` — two different textures?

The algorithm spec reads from `uUpperCascadeAtlas` (line 201-204). The existing code reads from `uUpperCascade` (the current upper-cascade texture bound in `radiance_3d.comp`). Are these the same texture? The plan doesn't define `uUpperCascadeAtlas` as a new binding or clarify that it's the same as the existing `uUpperCascade`. If it's a new texture, that's an allocation change (not noted). If it's the same texture, the name mismatch will cause implementation confusion.

### W4 (MEDIUM) — The `sampleUpperDir` call inside the WeightedSample loop does 4-bin fetches if `uUseDirBilinear == 1`

Critic 13 H3 correctly identified that a single-bin `texelFetch` would regress `uUseDirBilinear`. The fix calls `sampleUpperDir(cornerPos, rayDir, Du)` per visible corner. But `sampleUpperDir` with `uUseDirBilinear == 1` does **4 directional-bin fetches** per call. In the WeightedSample loop, that's up to 8 corners × 4 bins = 32 atlas fetches per direction, on top of the 8 look-back fetches. Total per-direction: up to 40 fetches × D²=64 bins × 32³ probes = ~2.6B fetches for C0 alone. The plan's cost estimate says ~1.2 ms (3% of bake), but this was computed assuming 5 fetches per corner. With `uUseDirBilinear == 1` (the **default**), it's 5 fetches per corner × 8 corners = 40 per direction, not 5 per direction. The cost estimate needs recalibration for the default configuration.

### W5 (MEDIUM) — The "look-back bin" picks one bin per corner, but `dirToBin` is not defined in the plan

The algorithm spec calls `dirToBin(dirToLower, Du)` and `dirToBin(ivec2(dx, dy), D)` (in the render path). The plan doesn't specify what `dirToBin` does — is it the octahedral mapping's inverse? ShaderToy's `CubeA.glsl` has a specific bin-picking convention (phi/theta atan2 with wall-attached axes). Our volumetric case needs a different convention. The plan should either define `dirToBin` explicitly or cite the existing implementation (if it already exists in `radiance_3d.comp` or `raymarch.frag`).

This matters because if `dirToBin` maps `dirToLower` to the wrong bin, the look-back reads the wrong `.a` value, and the visibility test gives wrong results. Phase 2's `binToDir` (in the render path) is the forward mapping; the inverse must be consistent with it.

### W6 (MEDIUM) — Tier 2 threshold for C0 leak (500-2000) has no justification

Where do 500 and 2000 come from? The 4373.5 baseline is the measured value. An 88% reduction to 500 is the Tier 1 target — presumably derived from "most of the leak gone, remaining below perceptual threshold." But the Tier 2 range (500-2000 = 50-90% reduction) is a wide band. Is 2000 still visibly leaking? Is 500 perceptually acceptable? Without perceptual calibration, these thresholds are arbitrary. The plan should either: (a) reference the Phase 2.5a.1 visual A/B that established what leak magnitude looks visible, or (b) acknowledge the thresholds are provisional and will be calibrated from v1 results.

### W7 (LOW) — The plan references ShaderToy's `CubeA.glsl:21-42` as ground truth but doesn't include the relevant code inline

For a plan that's the source of truth, the algorithm spec should include the ShaderToy reference code alongside the 3D translation, so a reader can verify the correspondence without leaving the document. Currently, the link to `shader_toy/CubeA.glsl` is a relative path — functional in the repo but fragile if the file moves. An inline excerpt of the relevant 21-42 lines would be more robust.

### W8 (LOW) — Phase 3a's `sampleUpperDirWeighted` helper has a stub `return vec4(0.0)` for gating mode 1

The Phase 3a refactor defines:
```glsl
vec4 sampleUpperDirWeighted(..., int gatingMode) {
    if (gatingMode == 0) return sampleUpperDirTrilinear(...);
    // Phase 3b path: return vec4(0.0);
}
```

If gating mode 1 is accidentally set before 3b lands, the bake will produce `upperDir = vec4(0.0)` for all trilinear paths — zero upper-cascade contribution, which would produce massive over-darkening (similar to Phase 2 v1). The plan should add a defensive assertion or comment: "gating mode MUST be 0 until 3b lands; mode 1 produces zero output."

### W9 (LOW) — The iteration backlog doesn't include a v0 (dry run without the merge formula change)

The plan's v1 includes both the visibility test AND the merge formula change (`* upperDir.a`). But these are two independent changes — the visibility test changes `upperDir.a` content, and the merge change consumes it. If v1 over-darkens (like Phase 2's v1 did), it's unclear which change caused it. A v0 that implements the visibility test but **keeps the original merge formula** (`rad = hit.rgb * l + upperDir.rgb * (1 - l)`) would isolate whether the visibility test itself is correct, independent of the merge change. If v0 shows no regression, the merge change is safe to add on top. If v0 already shows regression, the visibility test itself needs fixing.

---

## Summary

A structurally sound plan with good tiered verification and honest risk acknowledgment. However, it has a **plan-breaking semantic conflict** (W1/W2):

- Phase 2 redefined `.a` as α-transparency (0/1). Phase 3's algorithm expects `.a` as hit-distance (real-valued, with negative sky sentinel). These are incompatible channel semantics in the same atlas texture.
- The plan claims "render-side behavior unchanged" but if `.a` carries hit-distance, the render-side α-gate (`w = wcos × a.a`) catastrophically breaks.

**This must be resolved before execution.** The cleanest solution: a separate `uHitDistAtlas` texture for the bake-side look-back, keeping `.a` in the main atlas as α-transparency for the render. This adds one texture fetch per look-back corner and ~2× memory for one additional atlas per cascade — but it preserves Phase 2's render-side contract and enables Phase 3's bake-side contract independently.

Secondary concerns: W4 (cost estimate under-calibrated for default `uUseDirBilinear == 1`), W5 (`dirToBin` undefined), W6 (Tier thresholds uncalibrated), W9 (iteration should start with v0 isolating visibility test from merge change).