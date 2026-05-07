# Review: Sponza SDF Step 1 Implementation Note

Review timestamp: 2026-05-06T18:12:45+08:00

Target: `doc/4/claude_plan/sponza_sdf_step1_impl.md`

Verdict: mostly accepted, with documentation corrections needed before treating the note as canonical. The current `src/obj_loader.h` implementation does replace the old projected `pointInTriangle()` test with closest-point distance, expands the triangle bbox by the marking threshold, and guards `voxelsFilled` against double-counting. The remaining issues are mostly about overclaimed explanations and missing runtime validation.

## What Matches Current Source

- `src/obj_loader.h:286-303` computes `threshold`, expands `minPt/maxPt`, converts that expanded bbox to voxels, computes `closestPointOnTriangle()`, and tests `length(worldPos - closest) <= threshold`.
- `src/obj_loader.h:305-310` uses `if (grid[idx + 3] == 0)` before writing and incrementing `voxelsFilled`.
- `src/obj_loader.h:340-374` defines `closestPointOnTriangle()`.
- `pointInTriangle()` is no longer present in `src/obj_loader.h`.
- `src/obj_loader.h:49-53` now clears loader arrays at the start of `OBJLoader::load()`, which also resolves the Step 0 switching bug, although the Step 1 note does not mention it.

## Findings

### 1. Medium - The old-bug explanation is technically wrong

The note says the old dominant-plane projection makes every point in 3D space look like it is inside an axis-aligned triangle, then says the barycentric denominator collapsed.

Affected note lines:

- `sponza_sdf_step1_impl.md:12-15`
- `sponza_sdf_step1_impl.md:131-134`

That is not how the old code behaved. The old `pointInTriangle()` dropped the dominant normal axis and ran a 2D barycentric test in the triangle's plane. For an axis-aligned wall/floor triangle, the projected 2D triangle generally has good area; the denominator does not collapse unless the triangle itself is degenerate in that projected plane.

The real issue was that this test ignored distance to the triangle plane after the caller restricted candidates to the raw, unexpanded triangle bbox. For flat surfaces, the bbox normal axis can map to too few voxel-center layers, and adjacent intersecting voxels were never tested. Within the tested layer, the old projection could also fill points by projected footprint rather than actual center-to-triangle distance.

Recommended correction:

Replace the "denominator collapsed" explanation with:

```text
The old test only checked whether a voxel center's projection landed inside the triangle footprint. It did not test 3D distance to the triangle, and the caller only considered voxels in the unexpanded triangle bbox. Flat Sponza surfaces therefore missed many intersecting voxels adjacent to the surface.
```

### 2. Medium - Degenerate triangle coverage is overclaimed

The note says the Ericson helper is "correct for all degenerate cases" and says no special-casing is needed.

Affected note lines:

- `sponza_sdf_step1_impl.md:48`
- `sponza_sdf_step1_impl.md:150-153`

The implementation is the standard closest-point-on-triangle routine for normal triangles, and it handles many vertex/edge regions. It does not include an explicit guard for a fully degenerate final region where `va + vb + vc` is zero. If malformed OBJ data includes zero-area or nearly-zero-area triangles that fall through the earlier region tests, `denom = 1.0f / (va + vb + vc)` can produce infinities or NaNs.

Recommended correction:

Soften the note to "handles normal triangles and common vertex/edge nearest-feature cases." For robustness, add a denominator guard before the final barycentric return:

```cpp
float sum = va + vb + vc;
if (std::abs(sum) < 1e-12f)
    return a; // or closest point on the longest edge
float denom = 1.0f / sum;
```

A better fallback is to return the closest point on the longest nonzero edge.

### 3. Medium - Runtime acceptance is still unproven

The note status is honest that runtime voxel-count verification is pending, but the surrounding writeup still reads like the Sponza voxelization problem is solved.

Affected note lines:

- `sponza_sdf_step1_impl.md:5`
- `sponza_sdf_step1_impl.md:157-168`

This step's risk is not compile-time correctness; it is whether Sponza voxelization at the current code path completes in reasonable time and produces a plausible count. Current `loadOBJMesh()` still calls:

- `src/demo3d.cpp:4090`: `objLoader.voxelize(volumeResolution, ...)`
- `src/demo3d.h:51`: `DEFAULT_VOLUME_RESOLUTION = 128`

So the pending runtime test is a 128^3 voxelization of 262267 Sponza triangles unless Step 2 changes the path to `meshSDFResolution=64`. The note should not call Step 1 functionally complete until the runtime checklist is run.

Recommended correction:

Keep the status as:

```text
Build clean; runtime voxel count and timing verification pending.
```

Then record actual Sponza and Cornell counts and elapsed time after running from the UI.

### 4. Low - Loader clearing is implemented but missing from the note

The current source now clears:

- `vertices`
- `normals`
- `texcoords`
- `faces`
- `faceMaterials`

at `src/obj_loader.h:49-53`.

That fixes the most important Step 0 issue: repeated Cornell/Sponza loads no longer append mesh data. However, `sponza_sdf_step1_impl.md` does not mention this change.

Recommended correction:

Add a short "extra Step 0 cleanup included" note. Also consider whether clearing should occur after the file opens successfully, because the current implementation clears the previous parsed OBJ even if the new file fails to open. In the current UI with known paths this is low risk, but it matters for future arbitrary OBJ loading.

### 5. Low - The first-writer guard is correct for counts but not a final albedo policy

The first-writer guard is the right fix for unique occupancy counts and avoids repeated writes along shared triangle edges. The note says it prevents double-counting and color overwrite, which is true.

The limitation is that future Step 2 albedo propagation will inherit arbitrary first-face colors in overlapping/coplanar regions. For Sponza's current default-gray path this is fine; for MTL/textured albedo it will need a deterministic material policy.

Recommended correction:

Add a caveat that first-writer color is acceptable for occupancy and gray Sponza validation, but not the final material policy.

## Recommended Fix Order

1. Correct the root-cause explanation: no barycentric denominator collapse for normal axis-aligned triangles.
2. Soften or guard the degenerate-triangle claim.
3. Add the implemented loader-clearing change to the Step 1 record.
4. Run the runtime voxel-count/timing checklist and record actual numbers.
5. Keep the first-writer color rule for now, but mark material policy as future work.

## Bottom Line

The Step 1 code is broadly the right fix for conservative surface voxel marking. The note needs a more precise explanation and runtime evidence before it should be treated as the final implementation record.
