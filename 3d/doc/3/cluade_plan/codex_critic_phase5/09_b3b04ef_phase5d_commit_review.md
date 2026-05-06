# Commit `b3b04ef39ec445e5c831043a3ff17b885f218c7d` Review

## Verdict

Yes, this commit solves the **main** Phase 5d problem.

The core issue was that non-co-located Phase 5d still read exactly one upper probe via `probePos / 2`, so every lower `2 x 2 x 2` block inherited the same upper sample and produced blocky spatial stepping. This commit replaces that with a real 8-neighbor spatial blend in the directional merge path, and it also fixes the border-weight bug from the earlier trilinear plan review by clamping the continuous upper-grid coordinate before `floor`/`fract`.

So if the question is "does this commit fix the parent-probe simplification that made non-co-located Phase 5d spatially blocky?", the answer is **yes**.

But it still does **not** achieve full ShaderToy-style weighted merge parity, and one piece of UI help text is now stale.

## Findings

### 1. High: the commit fixes the actual Phase 5d spatial-merge problem

Before this commit, the non-co-located directional path still did:

```glsl
upperProbePos = probePos / 2;
upperDir = sampleUpperDir(upperProbePos, rayDir, uUpperDirRes);
```

That meant all 8 lower probes in one `2 x 2 x 2` block read the same parent upper probe.

This commit changes that in the shader by:

- adding `uUpperVolumeSize` and `uUseSpatialTrilinear`
- adding `sampleUpperDirTrilinear(...)`
- precomputing `triP000` / `triF`
- routing non-co-located + directional + trilinear-ON through the 8-neighbor blend

Evidence:

- `res/shaders/radiance_3d.comp:142-171`
- `res/shaders/radiance_3d.comp:264-286`
- `res/shaders/radiance_3d.comp:304-312`

That is the right structural fix for the known Phase 5d simplification.

### 2. High: the earlier border bug is fixed correctly

The earlier trilinear plan still had the same class of mistake that Phase 5f had at the directional borders: it clamped the integer corner but left the interpolation weights derived from an unclamped coordinate.

This commit fixes that by doing:

```glsl
vec3 upperGridClamped = clamp(upperGrid, vec3(0.0), vec3(uUpperVolumeSize - ivec3(1)));
triP000 = ivec3(floor(upperGridClamped));
triF    = fract(upperGridClamped);
```

Evidence:

- `res/shaders/radiance_3d.comp:275-285`

That is the correct fix for the low-edge/high-edge weight problem that was called out in the plan review.

### 3. Medium: this is still not full ShaderToy-style weighted merge parity

The commit removes the old global `upperOccluded` logic, which is good because that logic was both analytically inert and structurally wrong as a single probe-pair decision.

Evidence:

- `res/shaders/radiance_3d.comp:268-270`
- `rg` confirms `upperOccluded` is gone from the live shader path

But ShaderToy's merge analogue is not just spatial interpolation. It also includes per-sample weighting in `WeightedSample(...)`.

So this commit achieves:

- 8-neighbor spatial interpolation parity in the 3D sense

It does **not** yet achieve:

- per-corner visibility-weighted upper sampling parity

That is fine if the project goal is "fix the blocky nearest-parent Phase 5d merge". It is not fine if the goal is "full ShaderToy weighted merge equivalence".

### 4. Medium: one Phase 5d help-text block is now stale and contradicts the new feature

The live UI help under the co-located checkbox still says non-co-located mode is:

- "probe-resolution halving only; no spatial interpolation merge"

Evidence:

- `src/demo3d.cpp:2153-2163`

That was true before this commit. It is no longer true once `useSpatialTrilinear` exists and defaults to `true`.

The separate Phase 5d trilinear checkbox below it is correct and explains the new behavior well:

- `src/demo3d.cpp:2191-2206`

But the older help block should now be revised, otherwise the panel tells two different stories about what non-co-located mode does.

### 5. Low: compile verification is real

I verified the current HEAD build with MSBuild after inspecting the commit.

Result:

- `0 warnings`
- `0 errors`

So unlike some earlier phase docs, this one is not relying only on diff inspection for the compile claim.

## Bottom line

This commit is a real Phase 5d fix, not just plan churn.

It solves the main problem we were criticizing:

- non-co-located merge no longer collapses each lower `2 x 2 x 2` block to one parent upper probe
- the spatial border math is fixed correctly

What remains is smaller:

1. update the stale co-located/non-co-located help text,
2. be explicit that this is spatial-trilinear parity, not full ShaderToy weighted parity, and
3. do runtime validation if you want to prove the stepping reduction visually rather than only structurally.
