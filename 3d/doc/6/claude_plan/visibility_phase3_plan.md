# Plan: Phase 3 — Bake-Side Leak Fix via 3D `WeightedSample`

**Date:** 2026-05-15
**Predecessors:**
- [visibility_phase3_standby.md](visibility_phase3_standby.md) — algorithm identified (ShaderToy `WeightedSample` adapted to 3D); this plan formalizes execution
- [visibility_phase2_impl.md](visibility_phase2_impl.md) — Phase 2 shipped: render-side α-gate works; bake-side leaks NOT delivered (acknowledged as W2 in critic chain)
- [visibility_phase2.5_impl.md §2.5a.1](visibility_phase2.5_impl.md) — baseline metric: C0 leak = **4373.5** in cornell-orig-alcove (the success criterion)
- [shader_toy/CubeA.glsl:21-42 `WeightedSample`](../../shader_toy/CubeA.glsl) — the ground-truth algorithm
- [.wolf/cerebrum.md 2026-05-15 entries](../../.wolf/cerebrum.md) — captures the architectural lessons

**Critic chain extends:** [critic 13](critic/13_visibility_phase3_plan_review.md) → revision 2 (this version). 3 HIGH (algorithm sign error, missing uniforms, single-bin merge regresses Phase 5f) + 4 MEDIUM (cost contradiction, missing formula, cascade ordering unverified, iteration budget unrealistic) + 2 LOW. All applied.

**TL;DR (rev 2):** Implement the 3D adaptation of ShaderToy's `WeightedSample` in `radiance_3d.comp`. **Per-upper-corner geometric visibility check at bake time** replaces Phase 2's "trust the upper cascade unconditionally" smoothstep. Atlas no longer carries leaked radiance; render-side α-gate stays as defense-in-depth. Estimated **~7–8 days** total (reflecting realistic iteration budget per critic 13 M4).

**Cost prediction (revised per critic 13 M1)**: ~+1.2 ms bake (~3% of current 38 ms bake), unchanged render. **Quality target**: drive C0 leak 4373.5 → <500 (≥88% reduction); Sponza/Cornell render RMSE within ±0.05 of Phase 2 (don't introduce new artifacts).

**Sequencing (three sub-phases):**

- **Phase 3a — Pre-flight + per-corner refactor + uniform plumbing (~1.5 days, no behavior change).** The existing bake's `upperDir` is sampled via 4 paths. Refactor so per-corner visibility can intercept. **Add per-corner `upperProbeWorld[8]` uniform array computed CPU-side** (per critic 13 H2 — `uUpperGridOrigin`/`uUpperGridSize` don't exist; cleaner to push the 8 world positions directly than expose per-cascade grid state). Bit-exact regression confirms.
- **Phase 3b — `WeightedSample` translation + iteration (~5–7 days, the substantive change).** Per-upper-corner geometric visibility check. Iteration backlog (v1–v4 per §3.6) realistically takes 5–7 days; abort to 3a if Tier 3 fails after v4.
- **Phase 3c — Cleanup (~0.5 day).** Update impl doc + cerebrum.

**Total: ~7–8 days** (was 5 in rev 1; corrected per critic 13 M4). Same hard-checkpoint structure as Phase 2 (3a is bit-exact reversible; 3b decision-gate fails → revert to 3a).

**Cascade ordering** (per critic 13 M3, verified [demo3d.cpp:2257](../../src/demo3d.cpp#L2257)): cascades dispatch **top-down** (`for (int i = cascadeCount - 1; i >= 0; --i)`). C3 → C2 → C1 → C0. Each lower cascade reads a freshly-baked upper. **No stale-data issue.** v4 iteration fallback (cascade re-ordering) is unnecessary; removed from §3.6.

---

## 1. Context

### What Phase 2 left undone (recap)

Phase 2's bake-side `rad = hit.rgb * l + upperDir.rgb * (1 - l)` formula trusts the upper cascade's contribution unconditionally. When `l < 1` (within `blendWidth` of the cascade interval boundary), upper-cascade radiance — which can include "what's beyond a wall the lower probe also sees" — gets baked into the lower probe's atlas RGB. Render-side α-gate hides this from output (α=0 zeros the leaked bin's contribution) but the leaked value still lives in the atlas. The 4373.5 baseline measurement quantifies this for cornell-orig-alcove.

### Why ShaderToy doesn't have this problem

Reference: [shader_toy/CubeA.glsl:21-42](../../shader_toy/CubeA.glsl). For each of the 4 upper-cascade probes the lower probe interpolates over (2D bilinear in ShaderToy's wall-attached layout; would be 3D trilinear with 8 corners in ours), `WeightedSample`:

1. Computes `relVec = lowerProbePos - upperProbePos` (world-space offset).
2. Picks the upper probe's direction bin facing back toward the lower probe (the "would-look-at-me" direction).
3. Reads the upper probe's stored ray distance for that bin (`.w` channel of the atlas — same as our Phase 1 Mode 4's `hit.a`).
4. **Geometric visibility check**: is `length(relVec)` small enough that the upper probe's "blocking" hit is past where the lower probe is? Mathematically: `length(relVec) < lProbeRayDist * cos(PI/2 - theta) + 0.01` where theta is the upper probe's bin half-angle.
5. Returns either `vec4(SUM_OF_UPPER_BINS_RGB, 1.0)` (visible) or `vec4(0.)` (occluded).

The merge then bilinear-interpolates over 4 corners (or trilinear over 8 in our case) and divides by the sum of visibility weights — same renormalize-over-visible pattern Phase 2 v5 already uses.

### Why this works for our volumetric probes

The standby doc identified this is a **mechanical translation**, not a research problem:
- Wall-attached → volumetric: replace `gPos + gTan*offset + gBit*offset` (ShaderToy's wall-parameterized upper position) with `uUpperGridOrigin + (upperProbeIdx + 0.5) * uUpperCellSize` (our world-space probe position; already exists in cascade state).
- 2D bilinear → 3D trilinear: 8 corners instead of 4; existing `sampleUpperDirTrilinear` already does the spatial blend.
- ShaderToy's `gNor` (wall normal) reference: NOT needed for our volumetric case because we use world-space `relVec` directly. The cone-angle correction `cos(PI/2 - theta)` is in upper-probe-local terms.

---

## 2. Phase 3a — Pre-flight + Per-Corner Refactor + Uniform Plumbing (~1.5 days)

### Goal

Restructure the existing `radiance_3d.comp` bake so the per-corner upper-cascade visibility check in Phase 3b can drop in cleanly, AND plumb the per-corner `upperProbeWorld[8]` uniform array CPU-side (per critic 13 H2 — `uUpperGridOrigin`/`uUpperGridSize` don't exist as uniforms). **No behavior change**; bit-exact regression confirms.

### CPU-side uniform plumbing (critic 13 H2)

The §3 algorithm needs the world-space position of each of the 8 upper-cascade corners surrounding the lower probe. Two options:

- **Option A (rejected as initially-considered "uUpperProbeWorld[8]"):** Pass per-thread 8 corner world positions as a vec3 uniform array. Doesn't work — there is no fixed set of 8 corners; each lower-probe thread has a different `triP000`, so corner indices vary per thread. A uniform array can't carry per-thread data.
- **Option B (chosen):** Pass `(uUpperGridOrigin, uUpperCellSize)` as 2 new vec3 uniforms (named to disambiguate from this cascade's existing `uGridOrigin`/`uGridSize`). Shader computes the per-thread 8 corner world positions inline from `triP000 + corners[i]` plus the base/cell uniforms. Trivially correct; 2 new uniforms; small uniform footprint.

**Implementation in `demo3d.cpp`'s `updateSingleCascade(i)` for `i < cascadeCount - 1`** (when there is an upper cascade to read):

```cpp
// Inside updateSingleCascade, when an upper cascade exists
const Cascade& upper = cascades[i + 1];
glm::vec3 upperGridOrigin = upper.volumeOrigin;
glm::vec3 upperCellSize   = upper.volumeSize / glm::vec3(upper.volumeSize_voxels);
glProgramUniform3fv(prog, locUpperGridOrigin, 1, &upperGridOrigin[0]);
glProgramUniform3fv(prog, locUpperCellSize,   1, &upperCellSize[0]);
```

Plumbing cost: 2 new uniforms, ~10 lines of C++ in `updateSingleCascade`, ~30 minutes. Add to Phase 3a budget (was 1d, now 1.5d to accommodate this + the helper refactor).

### Today's structure ([radiance_3d.comp:367-379](../../res/shaders/radiance_3d.comp#L367))

```glsl
vec4 upperDir = vec4(0.0, 0.0, 0.0, 1.0);
if (uHasUpperCascade != 0) {
    if (uUseDirectionalMerge != 0) {
        if (uUpperToCurrentScale == 2 && uUseSpatialTrilinear != 0)
            upperDir = sampleUpperDirTrilinear(triP000, triF, rayDir, uUpperDirRes);
        else
            upperDir = sampleUpperDir(upperProbePos, rayDir, uUpperDirRes);
    } else if (uUseDirBilinear != 0) {
        upperDir = vec4(texture(uUpperCascade, uvwProbe).rgb, 1.0);
    } else {
        upperDir = vec4(texelFetch(uUpperCascade, upperProbePos, 0).rgb, 1.0);
    }
}
```

The 4 sampling paths (trilinear / single / texture-bilinear / texelFetch) feed `upperDir` as a single `vec4`. Phase 3b needs to gate this `vec4` per the visibility test, but the inner `sampleUpperDirTrilinear` is the only path that exposes the 8-corner structure needed for per-corner gating.

### Refactor plan

Add a new helper `sampleUpperDirWeighted` that:
1. Wraps the existing trilinear path
2. Takes additional `relVec` info
3. Returns the 8-corner array OR a pre-blended `vec4` depending on a "gating mode" parameter
4. Defaults to "no gating" → bit-exact same as `sampleUpperDirTrilinear` today

Then in Phase 3b, switch the helper's gating mode on (per-corner WeightedSample). If 3b decision-gate fails, switch the gating off → bit-exact reverts to 3a state.

```glsl
// Phase 3a refactor target (no gating yet):
vec4 sampleUpperDirWeighted(ivec3 triP000, vec3 triF, vec3 rayDir,
                            int Du, vec3 lowerProbeWorld, int gatingMode) {
    if (gatingMode == 0) {
        return sampleUpperDirTrilinear(triP000, triF, rayDir, Du);  // bit-exact passthrough
    }
    // Phase 3b path: per-corner WeightedSample (filled in 3b)
    return vec4(0.0);
}
```

The bake's `vec4 upperDir = ...` block changes to call the new helper. Other 3 sampling paths (single, texture-bilinear, texelFetch) stay unchanged — they don't need WeightedSample because they don't use the 8-corner upper-cascade trilinear (they're already-averaged or single-probe samples; no leak amplification mechanism from them).

### Verification (3a)

1. Build clean.
2. Default render: bit-exact RMSE 0.000000 vs Phase 2 baseline (`phase2v5_post_sponza_cammd_m0.png`).
3. Bake-leak metric re-run on cornell-orig-alcove: numbers identical to current 4373.5 baseline.

If either fails, the refactor changed behavior somewhere — fix or back out.

---

## 3. Phase 3b — `WeightedSample` Translation (~5–7 days)

### Goal

Implement the 3D per-corner geometric visibility check inside `sampleUpperDirWeighted`. Drive the bake-leak baseline from 4373.5 toward <500 in C0 (cornell-orig-alcove). Don't regress Sponza/Cornell render quality.

### Algorithm spec (rev 2 per critic 13 H1/H3/M2/L2)

Inside `sampleUpperDirWeighted(triP000, triF, rayDir, Du, lowerProbeWorld, 1)` for the 3b gating mode:

```glsl
// Upper-cascade origin/cellSize (per §2 plumbing — 2 new vec3 uniforms)
vec3 upperGridOrigin = uUpperGridOrigin;
vec3 upperCellSize   = uUpperCellSize;

// 8-corner offsets (same as Phase 2 trilinear)
ivec3 hi = uUpperVolumeSize - ivec3(1);
ivec3 corners[8] = ivec3[8](
    ivec3(0,0,0), ivec3(1,0,0), ivec3(0,1,0), ivec3(1,1,0),
    ivec3(0,0,1), ivec3(1,0,1), ivec3(0,1,1), ivec3(1,1,1));

// Cone half-angle for the upper probe's directional bin (critic 13 M2 — formula written explicit).
// Octahedral D×D bins → average solid-angle cap: cos(theta_half) = 1 - 2/D².
// We need sin(theta_half) = sqrt(1 - cos²) = sqrt(1 - (1 - 2/D²)²).
// Concrete values (precomputed CPU-side, passed as `uUpperBinConeSin`):
//   D=4  → sin(theta_half) ≈ 0.484
//   D=8  → sin(theta_half) ≈ 0.248
//   D=16 → sin(theta_half) ≈ 0.124
// Per critic 7 H2: octahedral non-uniformity means per-bin theta varies; the
// average-area value above is v1; v2 fallback in §3.6 swaps for a per-bin LUT.
float cosCorrection = uUpperBinConeSin;

vec4 sumWeighted = vec4(0.0);
float wTotalSpatial = 0.0;

for (int i = 0; i < 8; ++i) {
    ivec3 cornerPos = clamp(triP000 + corners[i], ivec3(0), hi);

    // 3D trilinear weight (axis-aligned corner offsets ∈ {0,1}; formula correct)
    float wx = (corners[i].x == 0) ? (1.0 - triF.x) : triF.x;
    float wy = (corners[i].y == 0) ? (1.0 - triF.y) : triF.y;
    float wz = (corners[i].z == 0) ? (1.0 - triF.z) : triF.z;
    float wSpatial = wx * wy * wz;
    wTotalSpatial += wSpatial;

    // Upper probe's world position
    vec3 upperProbeWorld = upperGridOrigin + (vec3(cornerPos) + 0.5) * upperCellSize;

    // Vector from upper probe to lower probe
    vec3 relVec = lowerProbeWorld - upperProbeWorld;

    // CRITIC 13 H1 FIX: Direction the upper probe would have looked TOWARD the lower probe.
    // In 3D world-space, this is +normalize(L - U). DO NOT NEGATE.
    // ShaderToy's CubeA.glsl WeightedSample uses `-dot(relVec, gTan)` etc., but that
    // negation is part of the wall-attached phi atan2 axis convention (which axis is
    // "positive" in the wall's local frame), NOT a "direction toward upper" semantic.
    // Translating ShaderToy's negation to our 3D world-space case via blanket negation
    // would flip the bin to point AWAY from the lower probe, reading nonsense data.
    vec3 dirToLower = normalize(relVec);  // NOT -normalize(relVec).

    // Pick the upper probe's bin in dirToLower (octahedral)
    ivec2 upperBin = dirToBin(dirToLower, Du);

    // Read ONLY the .a channel (look-back bin's stored hit-distance from Phase 1 Mode 4).
    // The .rgb of this bin is NOT consumed — visibility test only.
    float lProbeRayDist = texelFetch(uUpperCascadeAtlas,
        ivec3(cornerPos.x * Du + upperBin.x,
              cornerPos.y * Du + upperBin.y,
              cornerPos.z), 0).a;

    // Geometric visibility check (translated from ShaderToy WeightedSample)
    bool visible = (lProbeRayDist < 0.0)                                  // sky exit (always visible)
                || (length(relVec) < lProbeRayDist * cosCorrection + 0.01); // cone test

    if (visible) {
        // CRITIC 13 H3 FIX: For the FORWARD sample (radiance the lower probe merges in),
        // call the existing `sampleUpperDir` helper rather than a single texelFetch of
        // the rayDir bin. `sampleUpperDir` honors `uUseDirBilinear` (Phase 5f's 4-bin
        // directional-bilinear sum). A single texelFetch here would silently regress
        // anyone running with `uUseDirBilinear == 1` (the default).
        // Note: `sampleUpperDir` returns vec4 with .rgb = radiance, .a = 1.0 (no
        // visibility info from this helper — visibility came from our look-back test above).
        vec4 forward = sampleUpperDir(cornerPos, rayDir, Du);
        sumWeighted.rgb += forward.rgb * wSpatial;
        sumWeighted.a   += wSpatial;  // accumulator for visible-corner weight
    }
}

// Normalize over visible corners (matches Phase 2 v5 / ShaderToy renormalize-over-visible).
// CRITIC 13 L2 FIX: .a of return is "visibility fraction"; the bake's merge formula
// is updated to GATE the upper contribution by it (see "Bake merge formula change" below).
return vec4(sumWeighted.rgb / max(0.01, sumWeighted.a),
            sumWeighted.a / max(0.01, wTotalSpatial));
```

### Bake merge formula change (critic 13 L2)

Today's bake (after Phase 2's render-side α-gate ship): `rad = hit.rgb * l + upperDir.rgb * (1 - l)`. The `upperDir` previously had no per-corner visibility info, so `.a` was always 1.0 (just a placeholder).

After Phase 3b, `upperDir.a` carries "fraction of upper-cascade corners that passed visibility." If the bake formula ignores it, the visibility check has no effect on what gets baked into `rad` — making the whole exercise a no-op. Update the merge:

```glsl
// Before (Phase 2): unconditional upper trust
rad = hit.rgb * l + upperDir.rgb * (1.0 - l);

// After (Phase 3b): gate upper contribution by visible-fraction
rad = hit.rgb * l + upperDir.rgb * (1.0 - l) * upperDir.a;
```

When all 8 corners are visible (`upperDir.a == 1.0`), behavior matches Phase 2 exactly. When some are occluded (`upperDir.a < 1.0`), upper contribution is proportionally attenuated — consistent with the renormalize-over-visible spirit of ShaderToy's `WeightedSample`. When all 8 are occluded (`upperDir.a == 0.0`), the bake stores only the local hit (no leak from upper).

**Key design decisions:**

1. **`relVec` for visibility (look-back bin), `rayDir` for sampling (forward via `sampleUpperDir`).** ShaderToy's `WeightedSample` does the same split: pick a look-back bin for the visibility test, then sum 4 forward bins for radiance. Our 3D version: visibility test uses the `dirToLower` bin's `.a`; the merge uses `sampleUpperDir(cornerPos, rayDir, Du)` for the forward radiance — preserving Phase 5f's `uUseDirBilinear` 4-bin sum.
2. **Cone correction `cosCorrection = sin(theta_half) = sqrt(1 - (1 - 2/D²)²)`** computed CPU-side per cascade and passed as `uUpperBinConeSin`. v1 uses the octahedral average-area value; v2 fallback (per-bin LUT) for non-uniformity (critic 7 H2).
3. **Two-component return.** `sumWeighted.a` = "fraction of corners that voted visible." The updated merge formula `rad = hit.rgb * l + upperDir.rgb * (1 - l) * upperDir.a` consumes it — without this consumer, the visibility check has no effect on baked `rad`.
4. **`dirToLower` is `+normalize(relVec)`, not `-normalize(relVec)`** (critic 13 H1). This is the most failure-prone line in the algorithm — the ShaderToy reference code's negation is misleading when ported naively to 3D world-space. The shader-side comment must be present and explanatory to forestall a future reader from "fixing" it back.

### Verification (3b — tiered)

Per the Phase 2 critic chain pattern (binary "ship vs revert" gates with explicit thresholds):

**Tier 1 — Ship 3b unconditionally:**
- Bake-leak metric: C0 leak ≤ 500 (≥88% reduction from 4373.5 baseline)
- Sponza render RMSE vs Phase 2 baseline ≤ 0.02
- Cornell scenes RMSE vs Phase 2 baseline ≤ 0.02
- Bake cost increase ≤ 5% (~38ms → ~40ms)

**Tier 2 — Ship 3b with documented partial improvement:**
- Bake-leak metric: C0 leak between 500 and 2000 (50-90% reduction)
- Render RMSE between 0.02 and 0.05
- Bake cost ≤ 10% increase

**Tier 3 — Don't ship; revert to 3a:**
- Bake-leak metric: < 50% reduction
- Render RMSE > 0.05 (visual regression)
- Bake cost > 10% increase
- Any new visible artifacts (new banding, energy loss, color shifts)

**Iteration budget**: up to 5–7 days within Phase 3b (per critic 13 M4 — the v1→v3 backlog below realistically takes ~1 day per iteration cycle including build/measure/analyze/decide). If iterating beyond that, abort and document what was tried (similar to Phase 2.5b's revert pattern).

### Likely iteration mechanics

Phase 2's experience suggests v1 won't be the final form. Pre-allocated iteration backlog:

- **v1 (textbook from spec above)**: cone correction = octahedral average-area sin(theta_half) (D=8 → 0.248).
- **v2 if C0 leak doesn't drop enough**: per-bin cone correction (compute per-bin solid angle from octahedral Jacobian; pass as a small uniform LUT, e.g. `uniform float uUpperBinConeSinLUT[64]` for D=8).
- **v3 if Sponza render dims**: tune the cone correction additive epsilon (`+0.01` in ShaderToy) — too small under-occludes, too large over-occludes. Likely sweep `{0.001, 0.005, 0.01, 0.02, 0.05}`.

**(Removed: v4 cascade re-ordering.)** Per critic 13 M3, verified [demo3d.cpp:2257](../../src/demo3d.cpp#L2257) — cascades dispatch top-down (`for (int i = cascadeCount - 1; i >= 0; --i)`), so each lower cascade always reads a freshly-baked upper. The "chicken-and-egg" stale-data concern doesn't apply.

---

## 4. Phase 3c — Cleanup (~0.5 day)

### Decisions to pin

1. **Keep render-side α-gate or remove it?** Phase 2's `sampleProbeDir` does `w = wcos × a.a`. After Phase 3, the bake no longer leaks → α=0 bins are correctly "no contribution" by construction. The render-side multiply becomes defense-in-depth (free perf). **Recommendation: keep it.** Multiply-by-zero is essentially free; removing it adds risk if a future bake change reintroduces a small leak.

2. **Phase 2.5d's bake-leak diagnostic (`--diag-alpha-mode=1`) — keep?** Yes; needed for any future Phase 3 iteration or regression.

3. **Cerebrum entries.** Update the 2026-05-15 "ShaderToy WeightedSample IS the algorithm" entry to mark Phase 3 as IMPLEMENTED, not just "concrete algorithm identified."

### Scope

- Update [visibility_phase3_standby.md](visibility_phase3_standby.md) → impl doc.
- Update [.wolf/cerebrum.md](../../.wolf/cerebrum.md) Phase 3 entry.
- File any deferred follow-ups (e.g., per-bin cone LUT if v1 was sufficient and v2 wasn't needed).

---

## 5. Recommended commit shape

| # | Commit | Time | Description |
|---|---|---:|---|
| 1 | `[Claude] Phase 3a: per-corner upper-cascade refactor + uUpperGridOrigin/CellSize plumbing (no behavior change)` | 1.5d | Add `sampleUpperDirWeighted` helper; add 2 vec3 uniforms; bit-exact match to Phase 2 |
| 2 | `[Claude] Phase 3b: WeightedSample-style bake-time visibility (drives leak from 4373.5 → ~XXX)` | 5–7d | Fill in the gating-mode-1 path; iterate v1→v3 per §3.6; verify per Tier 1/2/3 gate; update bake merge formula to consume `upperDir.a` |
| 3 | `[Claude] Phase 3c cleanup + impl doc + cerebrum` | 0.5d | Per §4 above |

If 3b decision-gate falls into Tier 3, an additional commit reverts the gating mode (1 → 0) and ships only 3a's harmless refactor. The 3a commit always survives.

---

## 6. Honest risks (rev 2 per critic 13)

- **My algorithm spec MAY STILL have errors**, even after critic 13 caught H1/H2/H3. Critic 10 H1 demonstrated my "geometry-aware merge" was wrong in three ways. ShaderToy's reference is right but my translation may have residual errors not caught by self-critique. Mitigation: line-by-line reference to ShaderToy CubeA.glsl during 3b implementation; explicit shader-side comments at the H1/H3 fix sites to forestall regression.
- **`dirToLower = +normalize(relVec)` is the most fragile line in the algorithm.** Critic 13 H1 already caught one wrong-sign mistake (the original `-normalize`). A future reader looking at ShaderToy's `-dot(relVec, gTan)` may "fix" it back. The mandatory shader-side comment block (per §3 algorithm spec) is part of the contract, not optional.
- **Cone correction (`sin(theta_half)` from average-area) is approximate.** Octahedral non-uniformity (critic 7 H2) means the actual per-bin half-angle varies. v2 fallback (per-bin LUT) addresses this if v1 fails. Concrete v1 values per critic 13 M2: D=4→0.484, D=8→0.248, D=16→0.124.
- **The "look-back bin" assumption** (`dirToBin(normalize(relVec), Du)`) picks ONE bin; the upper probe's ray for that bin may not be the geometrically-relevant one (e.g., if the upper probe's bin was wide and missed the actual occluder). ShaderToy's flatland geometry guarantees correctness in 2D; in 3D with octahedral, this is a heuristic. Tier 1/2/3 gate catches this if it matters.
- **Cascade ordering: NO LONGER A RISK** (per critic 13 M3, verified [demo3d.cpp:2257](../../src/demo3d.cpp#L2257)). Existing dispatch is top-down (`for (int i = cascadeCount - 1; i >= 0; --i)`); each lower cascade reads a freshly-baked upper. v4 iteration fallback removed.
- **Cost estimate (~+1.2 ms, critic 13 M1 reconciled).** ShaderToy's WeightedSample does 1 atlas fetch (the look-back bin) per corner. Our 3D version does 2 fetches per corner: 1 single-bin look-back (`.a` only) + 1 `sampleUpperDir` call (which may itself do 4 bin fetches if `uUseDirBilinear == 1`). Worst case: 5 fetches per corner. 8 corners × D²=64 bins × ~5 fetches × 32³ probes for C0 = ~2.7B fetches/bake — but most will hit cache. Realistic: ~1.2 ms added (~3% of 38 ms bake). The original "0.5 ms" estimate in the standby doc was 2× optimistic.
- **Iteration budget (~5–7 days for 3b).** Per critic 13 M4, the v1→v3 backlog at 1 day per cycle (build + measure + analyze + decide next iteration) realistically takes 3–5 days. Plus initial v1 implementation (~1.5 days). Total 3b: 5–7 days. If team prefers tighter scope, commit to v1-only with hard-revert on Tier 3 fail.
- **Atlas memory unchanged** (still RGBA16F per Phase 5g; .a still carries hit.a); no GPU resource changes. Two new uniforms (`uUpperGridOrigin`, `uUpperCellSize`) — negligible footprint.
- **Render-side behavior unchanged** (still α-gate per Phase 2; α now has different content in atlas but the consumer doesn't care). This is the key safety property — Phase 3 changes the atlas content AND the bake's merge formula (per L2 fix: `* upperDir.a`), but not the render math.
- **Bake merge formula change is load-bearing** (critic 13 L2). The new `* upperDir.a` factor is what makes the visibility check actually affect baked `rad`. If a future change drops the `.a` factor (thinking it's a no-op since `upperDir.a` was always 1.0 pre-Phase-3), the entire Phase 3 work becomes a slower no-op. This is worth a comment in the bake shader at the merge site.

---

## 7. Decision gates summary

```
START
  │
  ▼
Phase 3a refactor lands → bit-exact verify
  │
  ├── PASS → continue to 3b
  └── FAIL → fix the refactor; can't proceed without bit-exact 3a
       │
       ▼
Phase 3b WeightedSample lands → re-run bake-leak metric + render A/B
  │
  ├── Tier 1 (C0 ≤ 500, RMSE ≤ 0.02)  → ship 3b; continue to 3c
  ├── Tier 2 (C0 500-2000, RMSE ≤ 0.05) → ship with documented partial improvement
  └── Tier 3 (C0 > 2000 OR RMSE > 0.05) → revert to 3a; file lessons for future attempt
       │
       ▼
Phase 3c cleanup
  │
  └── DONE — Phase 2.5/3 chain closed.
```

---

## 8. What's explicitly OUT of scope

- **Wall-attached probes** (the more thorough architectural fix per ShaderToy). That's a multi-week refactor; Phase 3 keeps the volumetric-probe architecture and just gates the bake-side merge.
- **Soft α** (Phase 2.6, filed dead per [its standby](visibility_phase2.6_standby.md)). ShaderToy doesn't use soft α; we shouldn't either.
- **Multiple-bounce specular GI / glossy reflections** (separate work stream).
- **Removing the render-side α-gate** (Phase 3c §1 decision: keep it as defense-in-depth).
- **Per-cascade tuning of cone-correction `cosCorrection`** (start with single global value; only diversify if v2 fallback is triggered).
- **Sentinel-α encoding (Option A)** — Phase 2's binary α=0 for sky stays. The 3D `WeightedSample` handles sky via the `lProbeRayDist < 0.0` short-circuit, same as ShaderToy.

---

## 9. Pre-conditions before starting

- Phase 2.5d landed (M1 diagnostic + critic-10 fixes, bake-leak baseline measurement infrastructure).
- Cerebrum 2026-05-15 entries documented.
- ShaderToy ground-truth review captured in [visibility_phase3_standby.md](visibility_phase3_standby.md).
- This plan reviewed (self-critic + improvements applied).

**Trigger to actually start (per the standby doc)**: at least one of (a) user-visible cross-wall bleed reported; (b) future feature wants leak-free atlas; (c) atlas debug viewer is operationally annoying.

If none of (a)-(c) yet, this plan stays drafted-but-unstarted. Phase 2 remains production.
