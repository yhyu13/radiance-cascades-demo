# Critic Review 13 — `visibility_phase3_plan.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-15
**Verdict:** Plan is structured well (3a/3b/3c sub-phasing matches Phase 2's pattern; tiered decision gate is the right calibration). **Three HIGH-severity issues, four MEDIUM, two LOW.** Most importantly: the algorithm spec in §3 has at least three subtle errors that — if shipped as written — would either break or repeat Phase 2's debug cycles. The plan also makes promises about cone correction, cascade ordering, and `uUpperGridOrigin` existence that haven't been verified against current code.

---

## HIGH severity

### H1 — The `relVec` direction sign is ambiguous and likely wrong

The plan writes:

```glsl
vec3 relVec = lowerProbeWorld - upperProbeWorld;
vec3 dirToLower = -normalize(relVec);
```

Then claims `dirToLower` is "the direction the upper probe would have looked TOWARD the lower probe."

**That's wrong.** If `relVec = lowerProbeWorld - upperProbeWorld`, then `relVec` already points from the upper probe TO the lower probe. The direction "from upper toward lower" is `+normalize(relVec)`, NOT `-normalize(relVec)`. The negation flips it to "from lower toward upper" — which would pick the upper probe's bin facing AWAY from the lower probe.

ShaderToy's [WeightedSample line 27](../../shader_toy/CubeA.glsl#L27):

```glsl
vec3 relVec = probePos - lastProbePos;  // probePos = current/lower; lastProbePos = upper
float phi = atan(-dot(relVec, gTan), -dot(relVec, gBit));
```

ShaderToy's `relVec` is also "lower minus upper" (matches our convention) but then uses `-dot(relVec, ...)` for the phi calculation. The negation in ShaderToy is part of the phi atan2 convention (which axis is "positive" for the wall-attached coordinate system) — NOT a "direction toward upper" semantic. Translating ShaderToy's negation to our 3D-octahedral world-space case via blanket negation is wrong; the right translation requires understanding what the ShaderToy negation actually does in the wall-attached parameterization.

**Concrete fix path**: derive the correct `dirToLower` from first principles in our 3D world-space:
- Upper probe at world position U.
- Lower probe at world position L.
- The upper probe's bin that "looks at" L is the bin whose direction-from-U is `normalize(L - U)`.
- So `dirToLower = normalize(lowerProbeWorld - upperProbeWorld) = +normalize(relVec)`. **Drop the negation.**

If shipped as written, the look-back bin would be the bin pointing AWAY from the lower probe — its stored hit-distance is for a completely different geometric direction. The visibility test would be using nonsense data. C0 leak might decrease (if it gets lucky and reads "sky" or "miss" bins) but would not be principled.

### H2 — `uUpperGridOrigin` doesn't exist as an upper-cascade-specific uniform

The plan writes:

```glsl
vec3 upperGridOrigin = uUpperGridOrigin;       // existing uniform, may need to add
vec3 upperCellSize   = uUpperGridSize / vec3(uUpperVolumeSize);  // existing uniforms
```

The "may need to add" hedge is doing too much work. **Searching the codebase confirms there is no `uUpperGridOrigin` uniform.** There IS `uGridOrigin` (this cascade) and `uUpperVolumeSize` (upper probe-grid size), but `uUpperGridOrigin` and `uUpperGridSize` would be new uniforms.

If those uniforms don't exist, my `sampleUpperDirWeighted` can't compute `upperProbeWorld`. The plan assumes the data is "already passed to the bake" but it isn't — adding them requires:
1. Computing per-upper-cascade `gridOrigin` / `gridSize` on the C++ side (the gridOrigin equals `volumeOrigin` for all cascades since they share the SDF volume; gridSize equals `volumeSize`; **but `cellSize` differs per cascade**)
2. Wiring two new uniforms through the `updateSingleCascade` dispatch
3. Updating the bake to read them

This is a non-trivial sub-task that the plan glossed as "existing uniforms." Probably half a day of plumbing work the §2.5 doesn't budget.

**Fix**: explicitly add the uniform-plumbing as a Phase 3a sub-task. Or — better — derive `upperProbeWorld` in C++ where the per-cascade state already lives, and pass it as a vec3 array uniform `uUpperProbeWorld[8]` (computed once per dispatch per probe — there are only 8 corners). This avoids exposing per-cascade-grid-origin/size everywhere.

### H3 — The "look-back bin vs forward bin" logic conflicts with ShaderToy's actual algorithm

The plan claims:

> Visibility test uses the dirToLower bin; the merge uses the rayDir bin. This is correct because the upper probe's "what radiance comes from the rayDir direction" is what the lower probe wants to merge in.

But ShaderToy's `WeightedSample` uses the SAME bin for both: it picks a bin based on `phi` (computed from relVec), reads `lProbeRayDist` from THAT bin's stored ray distance, then if visible, returns the **sum of 4 forward-direction bins** centered around `luvd` (which is `lPUVDirs` = the rayDir bin index). So:

- Visibility test: bin `phi` (look-back direction)
- Merge: 4 bins centered at `luvd` (forward direction, derived from current cascade's rayDir mapped into upper cascade's coordinate system)

The plan's "1 forward bin" simplification loses ShaderToy's 4-bin sum. ShaderToy's 4-bin sum is a 2x2 directional bilinear over neighboring forward-bin texels — equivalent to our existing `sampleUpperDir` with `uUseDirBilinear == 1` (Phase 5f). My plan's simplification effectively forces `uUseDirBilinear == 0` (nearest-bin) for the WeightedSample path, regressing Phase 5f's directional-bilinear improvement.

**Fix**: the merge inside `sampleUpperDirWeighted` should use the existing `sampleUpperDir(...)` helper (which respects `uUseDirBilinear`) rather than a single `texelFetch` of the forward bin. This restores Phase 5f compatibility and matches ShaderToy more faithfully. The visibility test's look-back bin is separate — it's a single fetch for the .a channel only.

If shipped as written, Phase 3b would silently regress Phase 5f users (anyone with bilinear directional merge ON; default is ON per the existing combo). Tier 3 fail likely.

---

## MEDIUM severity

### M1 — Cost estimate corrected in §6 but not propagated to §1 TL;DR

The plan's TL;DR says "~+0.5 ms bake (~1.3% of current 38 ms bake)." Section §6 corrects this to ~1.2 ms (~3%). The TL;DR is wrong if §6 is right. **Fix**: update TL;DR to match the corrected estimate, OR add a note that cost is "predicted, verified in 3b."

### M2 — Cone-correction formula uses `cos(PI/2 - theta) = sin(theta)` but doesn't pin which `theta` formula

The plan says "start with the octahedral average-area formula" and references "critic 7 H2 caveat." But it doesn't write the actual formula. The reader has to dig back through critic 7 to find:

> θ_half = acos(1 − 2/D²)
> tan(θ_half) = sqrt(1 − (1 − 2/D²)²) / (1 − 2/D²)

So `sin(θ_half) = sqrt(1 - (1 - 2/D²)²)`. For D=8 (typical upper cascade): `sin(θ_half) ≈ 0.248`. **Fix**: write the formula in the plan; pre-compute the value for each D the project uses (D=4, D=8, D=16); either pass as uniform array or compute CPU-side per cascade.

### M3 — Cascade ordering ("top-down vs bottom-up") is mentioned but never confirmed

The plan §6 notes:

> Cascade ordering matters... Need to verify which order the existing dispatch uses.

If C0 is dispatched first with `uUpperCascade` pointing at C1, then C1 is dispatched, the C0 reads stale C1 from previous frame. Only acceptable for steady state; visible flicker on first frame post-load. **Fix**: actually verify before §3a. A 5-minute grep of `updateRadianceCascades` resolves this.

### M4 — Phase 3b iteration backlog (v1-v4) is too aspirational; budget says "5 days within 3b"

The plan §3 lists 4 likely iteration variants (v1 textbook, v2 per-bin LUT, v3 epsilon tuning, v4 cascade re-ordering). Each is a meaningful change requiring rebuild + re-measurement. At a realistic 1 day per iteration cycle (build, measure, analyze, decide next iteration), 4 variants = 4 days. Plus the initial v1 implementation = 5+ days. **The "3b = 3 days" estimate doesn't survive the iteration backlog.** Either the iteration budget needs to be 5+ days, or the plan needs to commit to "v1 only; revert if it doesn't work."

**Fix**: realistic budget. If the team is willing to iterate, allow 5-7 days for 3b. If not, scope to v1-only with explicit revert-on-failure.

---

## LOW severity

### L1 — `triF` interpretation differs between Phase 2 and Phase 3

In Phase 2's `sampleUpperDirTrilinear`, `triF` is the fractional position of the **lower probe** within the upper-cascade cell — used for trilinear weighting. The Phase 3 spec's per-corner loop uses the same `triF` for `wx, wy, wz` calculation. **But the formula `wx = (corners[i].x == 0) ? (1 - triF.x) : triF.x` is correct ONLY for axis-aligned corner offsets `corners[i]` ∈ {0, 1}**, which the spec uses — so the formula is technically right. Just worth confirming `triF` semantics haven't drifted in Phase 5d/5f changes.

### L2 — The plan's "wTotalSpatial" accumulator is dead code

The spec accumulates `wTotalSpatial += wSpatial` in the loop but only uses it in the return: `sumWeighted.a / max(0.01, wTotalSpatial)`. The result is "fraction of corners that voted visible." But the bake's existing merge formula (`rad = hit.rgb * l + upperDir.rgb * (1 - l)`) uses only `upperDir.rgb`, not `upperDir.a`. So the .a accumulator is computed but never consumed downstream. **Either** the bake's merge needs to honor the visibility fraction (similar to Phase 2's α gating), **or** the accumulator should be dropped to save a few ops.

If kept, the natural use is: `rad = hit.rgb * l + upperDir.rgb * (1 - l) * upperDir.a` — gates upper contribution by "fraction of visible corners." This matches the spirit of WeightedSample. Worth confirming the merge formula actually uses `.a`.

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | `dirToLower = -normalize(relVec)` has wrong sign; should be `+normalize(relVec)` |
| H2 | HIGH | `uUpperGridOrigin` and `uUpperGridSize` uniforms don't exist; plumbing budget missing |
| H3 | HIGH | "Single forward bin" merge regresses Phase 5f directional bilinear; should use existing `sampleUpperDir` helper |
| M1 | MEDIUM | TL;DR cost (0.5 ms) contradicts §6 corrected cost (1.2 ms) |
| M2 | MEDIUM | Cone-correction formula referenced but not written into the plan |
| M3 | MEDIUM | Cascade ordering claim never verified against existing dispatch |
| M4 | MEDIUM | "3b = 3 days" doesn't survive the v1→v4 iteration backlog (realistic: 5-7 days) |
| L1 | LOW | `triF` semantics technically correct but worth confirming hasn't drifted |
| L2 | LOW | `wTotalSpatial` / `sumWeighted.a` accumulator computed but downstream merge formula doesn't use it |

---

## Top actions for plan revision

1. **Fix H1**: drop the negation. `dirToLower = normalize(lowerProbeWorld - upperProbeWorld)`. Add a comment explaining why (to forestall someone "fixing" it back to ShaderToy's negation, which works in 2D wall-attached but not 3D world-space).
2. **Fix H2**: spec the C++ uniform-plumbing in 3a. Add the half-day to the 3a budget. Or better: pass per-corner `upperProbeWorld[8]` as a vec3 array uniform computed in C++.
3. **Fix H3**: `sampleUpperDirWeighted` should call `sampleUpperDir(cornerPos, rayDir, Du)` for the forward sample (preserves Phase 5f bilinear). The look-back is still a separate single `texelFetch` of `.a`.
4. **Fix M2**: write the cone-correction formula with concrete numbers per D.
5. **Fix M3**: verify cascade ordering (5-min grep) and pin in the plan.
6. **Fix M4**: realistic 3b budget (5-7 days OR commit to v1-only with hard abort).
7. **Fix L2**: decide whether `.a` (visibility fraction) feeds into the bake's merge formula. If yes, update the merge expression. If no, drop the accumulator.
