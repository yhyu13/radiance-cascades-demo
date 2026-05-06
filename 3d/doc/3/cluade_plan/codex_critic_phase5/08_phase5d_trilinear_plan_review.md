# Phase 5d Trilinear Spatial Merge Plan Review

## Verdict

The plan is moving in the right direction. Replacing the current `probePos / 2` nearest-parent lookup with an 8-neighbor spatial blend is the right next step if the goal is to make non-co-located Phase 5d behave more like a real 3D upper-cascade interpolation path.

But the plan still has one important math bug and two smaller framing/validation problems. The biggest issue is that its proposed `triP000` / `triF` setup does not clamp correctly at the spatial borders, so the first and last upper probes would still blend with interior neighbors when they should clamp to edge.

## Findings

### 1. High: the proposed trilinear coordinate math is wrong at the grid borders

The plan proposes:

- `upperGrid = (worldPos - uGridOrigin) / uUpperProbeCellSize - 0.5`
- `triP000 = clamp(floor(upperGrid), 0, uUpperVolumeSize - 2)`
- `triF = fract(upperGrid)`

Evidence:

- `doc/cluade_plan/phase5d_trilinear_plan.md:87-103`

That is not a correct clamp-to-edge spatial interpolation rule. It clamps the integer base corner, but it leaves the fractional weights derived from the **unclamped** coordinate.

Concrete 1D example in the current 32 -> 16 layout:

- lower first-center position: `x = 0.0625`
- upper cell size: `0.25`
- `upperGrid = 0.0625 / 0.25 - 0.5 = -0.25`
- `floor(upperGrid) = -1`, then clamped to `0`
- `fract(-0.25) = 0.75`

So the plan would blend 75% toward upper probe 1 even though this sample lies **outside** the first interval between upper probe centers and should clamp to upper probe 0.

The same problem appears at the high edge. This is the same class of issue that Phase 5f already had to reason about in directional space: you cannot clamp only the integer indices and leave the interpolation weights based on an unclamped coordinate.

Evidence:

- `doc/cluade_plan/phase5d_trilinear_plan.md:196-198`
- `res/shaders/radiance_3d.comp:122-136`

The fix is to clamp the continuous coordinate first, then derive both `p000` and `f` from that clamped value, or equivalently recompute `f` from the clamped base interval.

### 2. Medium: the plan overstates ShaderToy equivalence because it removes visibility instead of replacing it per neighbor

The problem statement says:

- the "correct implementation" is 8-neighbor trilinear interpolation
- this is the 3D analogue of ShaderToy's `WeightedSample()`

Evidence:

- `doc/cluade_plan/phase5d_trilinear_plan.md:19-27`

But ShaderToy's merge path is not only spatial interpolation. `WeightedSample()` also computes an approximate per-sample visibility weighting before the 4-neighbor blend.

Evidence:

- `shader_toy/Image.glsl:21-34`
- `shader_toy/Image.glsl:207-214`

This plan explicitly removes the current `upperOccluded` logic and does not replace it with any per-corner weighting. That is a defensible simplification, especially because the current visibility test is analytically inert, but it means the plan is only implementing the **spatial interpolation half** of the analogue.

So the plan should describe itself as the correct next spatial fix, not as full ShaderToy-style weighted merge parity.

### 3. Medium: the proposed stop condition uses the wrong debug view for proving spatial smoothness

One stop condition says:

- "Atlas mode 3 debug: C1 probe boundaries visually smoother with trilinear ON"

Evidence:

- `doc/cluade_plan/phase5d_trilinear_plan.md:243-251`

But mode 3 is the raw atlas viewer:

- it displays the `D x D` tile layout of the current cascade atlas
- it is useful for per-bin inspection, not for a clean read of world-space interpolation boundaries

Evidence:

- `res/shaders/radiance_debug.frag:140-146`

Since the whole point of this change is spatial blending between neighboring upper probes, the stronger validation would be:

- final GI comparison in non-co-located mode, or
- a dedicated debug view that visualizes the selected `triP000`/neighbor set or the interpolation weights

Mode 3 may still show a difference, but it is a weak acceptance test for this particular change.

### 4. Low: the prerequisite line is self-referential and the file still has encoding damage

The header currently lists:

- `phase5d_impl_learnings.md`
- `phase5d_trilinear_plan.md`

as its own prerequisite docs.

Evidence:

- `doc/cluade_plan/phase5d_trilinear_plan.md:6`

That should point at the actual antecedent analysis note instead of the file itself. The same file also still contains mojibake in multiple places (`鈥?`, `脳`, `鲁`), which makes the math sections harder to trust on reread.

## Where the plan is strong

- It is targeting the real structural weakness in current non-co-located Phase 5d: `upperProbePos = probePos / 2` still picks one parent probe for an entire `2 x 2 x 2` lower block.
- Hoisting the trilinear coordinates outside the direction loop is the right performance shape.
- Removing the current global `upperOccluded` bool is better than pretending that a single probe-pair visibility decision is equivalent to per-corner weighting.

## Bottom line

This plan is worth keeping, but not as written. I would revise it by:

1. fixing the border-weight math for `triF`,
2. downgrading the ShaderToy claim to "spatial interpolation parity, not full weighted parity",
3. replacing the atlas-mode stop condition with a validation path that actually exposes spatial blending, and
4. cleaning up the prerequisite line and encoding damage.
