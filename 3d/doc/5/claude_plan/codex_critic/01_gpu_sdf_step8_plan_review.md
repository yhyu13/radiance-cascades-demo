# Critic Review 01 - gpu_sdf_step8_plan.md

Reviewed: 2026-05-09T10:20:14+08:00

Target: `doc/5/claude_plan/gpu_sdf_step8_plan.md`

## Verdict

The plan points in the right direction: keep the CPU EDT path as the baseline, fix the dormant `sdf_3d.comp` path, and use a small dynamic primitive overlay before attempting GPU triangle voxelization. That is a good decomposition.

As written, though, the plan is not implementation-ready. The biggest problem is lifecycle: the current render loop is gated by static local `sdfReady` and `cascadeReady` flags in `Demo3D::render()`, while the plan only toggles `meshSDFReady`. That will not reliably trigger a GPU rebake or cascade rebuild after the first static bake. The dynamic sphere also needs tighter integration with cascade scheduling and temporal history, or it will either not update or will ghost/stale across cascades. There are also correctness issues in the proposed sphere voxelization, shader binding/barriers, A/B expectations, and GPU timing method.

## Evidence Checked

- Current Step 7/cleanup code in `src/demo3d.cpp` and `src/demo3d.h`.
- Current dormant shaders: `res/shaders/sdf_3d.comp` and `res/shaders/voxelize.comp`.
- Live CPU mesh SDF path: `Demo3D::generateMeshSDF()` and `Demo3D::sdfGenerationPass()`.
- Live render dirty-state and cascade scheduling in `Demo3D::render()` / `updateRadianceCascades()`.
- Live texture creation/destruction and shader loading paths.
- Live raymarch/radiance shader SDF/albedo contracts.

## What Looks Good

- Keeping CPU EDT as the default baseline is the right safety choice.
- Deferring `voxelize.comp` is a good scope boundary; fixing JFA and GPU triangle voxelization at the same time would make failures hard to isolate.
- Rewriting `sdf_3d.comp` instead of patching the old shader is appropriate. The current file is dead code and still contains the image-parameter `safeLoad(image3D img, ...)` pattern that caused the driver compile failure.
- Using ping-pong Voronoi textures is better than in-place JFA.
- The plan correctly preserves the existing raymarch/radiance hit thresholds as a contract that the generated SDF must satisfy.

## Findings

### 1. `meshSDFReady = false` is not enough to trigger a new SDF pass

Severity: High

The plan says the GPU toggle and dynamic sphere update should force a rebake by setting `meshSDFReady = false`. In the current renderer, that flag is only checked inside `sdfGenerationPass()`. But `sdfGenerationPass()` is only called when the render-local static `sdfReady` is false:

- `Demo3D::render()` owns a static local `sdfReady`.
- `sdfReady` is reset when `sceneDirty` is true.
- The OBJ branch inside `sdfGenerationPass()` checks `meshSDFReady`.

After the first successful static bake, `sdfReady` remains true. Flipping `meshSDFReady` from UI or `update()` will not cause `sdfGenerationPass()` to run again unless something also resets the render-local `sdfReady`.

Impact: the GPU/CPU toggle can appear to do nothing after the first bake, and the dynamic sphere can modify `voxelGridTexture` without producing a new SDF.

Suggested fix: make SDF/cascade readiness explicit members rather than static locals, or add a render condition such as `if (!sdfReady || (useOBJMesh && !meshSDFReady))`. Then all Step 8 toggles and dynamic updates must invalidate the same single source of truth.

### 2. Dynamic SDF changes also need cascade and temporal-history policy

Severity: High

The plan says that once the SDF rebakes, radiance cascades pick up the moved sphere automatically. That is too optimistic for the current scheduler.

Current behavior:

- `cascadeReady` is another static local in `Demo3D::render()`.
- `updateRadianceCascades()` staggers cascades by `renderFrameIndex % min(1 << i, staggerMaxInterval)`.
- `useTemporalAccum` defaults on and uses EMA history with low alpha.
- `historyNeedsSeed` is cleared after cascade update.

For a moving sphere, every frame is a scene change. The plan needs to explicitly mark cascades dirty when the dynamic SDF changes, decide whether all cascades must rebuild on that frame, and decide whether temporal history should be reset, clamped, or allowed to trail. Otherwise the sphere can produce stale upper-cascade lighting and EMA ghosting.

Suggested fix: dynamic-object mode should either force all cascades for the demo frame (`renderFrameIndex = 0` / force rebuild) and seed or validate history, or it should document intentional latency/staggering. Do not describe the cascade update as automatic until this policy is specified.

### 3. `addVoxelSphere()` should not mirror the current `addVoxelBox()` coordinate math

Severity: High

The plan says to mirror `addVoxelBox()`. That helper is not a good template for OBJ-space dynamic overlays:

- It uses `voxelSize = 1.0f / volumeResolution`.
- It assumes `uGridOrigin(0.0f)`.
- The actual SDF/radiance volume uses `volumeOrigin = (-2,-2,-2)` and `volumeSize = (4,4,4)`.

For sphere centers derived from `currentObjBmin/currentObjBmax`, negative coordinates are normal. Mirroring `addVoxelBox()` will map those incorrectly and clamp large parts of the sphere to voxel 0.

Suggested fix: implement `addVoxelSphere()` using the same world-to-voxel convention as the OBJ voxelizer: `(worldPos - volumeOrigin) / volumeSize * resolution`, with clamped bounds.

### 4. Per-voxel `glTexSubImage3D` uploads are not a dynamic path

Severity: Medium

`addVoxelBox()` uploads one voxel at a time. The plan estimates the sphere update as about 16 KB per frame, but if implemented like `addVoxelBox()` the real issue is thousands of tiny GL calls per frame, not the byte count.

Suggested fix: build a compact CPU subvolume for the sphere's voxel bbox and upload it with one `glTexSubImage3D()` call. If the sphere is surface-only, the subvolume can still be sparse in alpha while uploaded as one contiguous block.

### 5. Static CPU-vs-GPU pixel parity is overclaimed

Severity: Medium

The plan expects near-zero pixel difference between CPU EDT and GPU JFA when dynamic objects are off. That is not guaranteed:

- The CPU path is an exact separable EDT over occupied voxel centers.
- Standard JFA is approximate and can miss the exact nearest seed in edge cases.
- The proposed GPU albedo path assigns nearest-seed color broadly, while the CPU path does three iterations of 6-neighbor albedo dilation from surface voxels.
- Both paths are voxel-center UDFs, not triangle-surface SDFs.

Suggested fix: make A/B acceptance numeric and tolerant: SDF max/mean error on a sampled volume, hit mask difference, final image difference with a defined threshold, and visual diff artifacts. Do not require or imply near-zero pixels until measured.

### 6. The final compute barrier is missing the texture-fetch bit

Severity: Medium

The proposed GPU path writes `sdfTexture` and `albedoTexture` as images, then later samples them as `sampler3D` in `radiance_3d.comp` and `raymarch.frag`. The plan only shows `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT`.

The live analytic path uses:

`GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT`

Suggested fix: after finalize, use at least the same barrier bits as the analytic SDF path before any texture sampling.

### 7. The shader-binding plan does not match the current shader loader model

Severity: Medium

The pseudocode refers to `sdfJFAProgram`, but the current code loads shaders through `loadShader()` into the `shaders` map. It also does not show explicit `layout(binding = N)` declarations or `glUniform1i()` calls for the image uniforms.

Suggested fix: either follow the existing `sdf_analytic.comp` pattern with explicit image bindings in GLSL, or explicitly set image uniform units in C++. `generateMeshSDFGPU()` should fetch `shaders["sdf_3d.comp"]`, handle missing/failed shader load cleanly, and fall back or return false with a clear error.

### 8. The GPU timing method will measure submission, not GPU execution

Severity: Medium

The proposed `std::chrono` timing around dispatches will mostly measure CPU submission time. GL compute dispatch is asynchronous; `glMemoryBarrier()` orders GPU memory visibility but does not make the CPU wait for completion.

Impact: a log like `GPU JFA SDF: <5ms` would not prove the GPU work took less than 5 ms.

Suggested fix: use OpenGL timer queries for the JFA debug group, or explicitly label the value as CPU submission time. Use `glFinish()` only as a diagnostic, not in the shipping path.

### 9. Resource lifecycle is underspecified

Severity: Low

The plan adds `voronoiTextureA`, `voronoiTextureB`, and `meshVoxelBaseTexture`, but only discusses allocation. The implementation also needs:

- deletion in the volume-buffer cleanup path,
- RenderDoc labels,
- safe behavior on failed allocation,
- reset/clear behavior when leaving OBJ mode,
- handling if volume resolution changes later.

This is not the core algorithm risk, but it should be in the implementation checklist.

### 10. Verification needs an exact way to control dynamic capture frames

Severity: Low

The plan asks for captures at sphere times `0, 1.5, 3.0, 4.5`, but the current CLI only has frame-count style controls. Without a way to set sphere time, orbit speed, or capture-at-frame deterministically, those captures will be approximate and may not be reproducible.

Suggested fix: add a CLI test hook for fixed sphere time or fixed frame index for the dynamic verification run.

## Bottom Line

Revise the plan before implementation. The shader rewrite is worthwhile, but Step 8 should first define a real dirty-state contract for dynamic SDF updates, cascade invalidation, and temporal history. Then implement sphere voxelization with correct world-to-voxel math and batched uploads, use texture-fetch barriers after image writes, and replace the near-zero/pure-timing verification claims with measured tolerances and GPU timer queries.
