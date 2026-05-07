# Review: sponza_sdf_step4_plan.md

Reviewed: 2026-05-07T16:27:49+08:00

Target: `doc/4/claude_plan/sponza_sdf_step4_plan.md`

Verdict: directionally good, but revise before implementation. Scaling Sponza to use more of the existing SDF volume and adding an OBJ-specific camera preset are the right next tests. The current plan overstates the density math, changes Cornell as a side effect, and treats a plausible Sponza camera as if it were already verified.

## Evidence Checked

- `src/obj_loader.h:155-172` still normalizes every OBJ to `[-1, 1]`.
- `src/demo3d.cpp:169-172` still uses volume origin `(-2,-2,-2)`, volume size `(4,4,4)`, and `baseInterval=0.125`.
- `src/demo3d.cpp:4275-4290` still resets the camera to `(0,0,4)` looking at the origin.
- `src/demo3d.cpp:4320-4386` still calls `objLoader.normalize()` unconditionally in `loadOBJMesh()`.
- Current post-Step-3 code has the bake-failure propagation fix: `render()` only marks `sdfReady=true` when `sdfGenerationPass()` returns true (`src/demo3d.cpp:508-518`), and `sdfGenerationPass()` returns false on mesh-bake failure (`src/demo3d.cpp:1461-1486`).
- `src/main3d.cpp:179-182` supports the plan's `--render-mode=` capture commands.
- `res/shaders/raymarch.frag:407-414` intersects the SDF volume before marching, so an outside camera is expected to work if the view ray enters the volume.
- Step 3 logs show Sponza at 37,757 seeds and Cornell at 40,878 seeds (`tools/app_run_step3_sponza*.log`, `tools/app_run_step3_cornell*.log`).
- Local OBJ bounds: Sponza has 145,185 vertices, min `(-1920.946,-126.443,-1182.807)`, max `(1799.908,1429.433,1105.426)`, extent `(3720.854,1555.876,2288.233)`. This supports the plan's "X long axis, Y up" assumption.

## Findings

### F1 - The 8x / 150K-300K seed-count claim uses volume math for a surface voxelizer

Plan refs: lines 33-36, 67-85, 179, 195.

The plan says filling `[-2,2]^3` instead of `[-1,1]^3` gives "8x the texel coverage" and implies a Sponza seed jump from about 38K to 150K-300K. That is not the right default expectation for this voxelizer. The filled set is a conservative surface band, not a solid volume. With the proposed 5 percent margin, linear scale changes from `1.0` to `1.9`; a surface-band count should usually scale closer to area, about `1.9^2 = 3.61x`, before topology and threshold effects. From 37,757 seeds, that rough estimate is about 136K, not 300K.

The full-volume `2.0` scale would be `2^3 = 8x` only for volume occupancy. The actual proposed `1.9` scale is `1.9^3 = 6.86x` even for a solid fill.

Recommendation: keep the `>100K` verification gate, but soften the expected range to "measure it; likely around 100K-160K if surface-area scaling dominates." Also do not explain EDT time as mostly seed-count driven. The CPU EDT and albedo dilation passes are dominated by fixed-size 128^3 sweeps; seed count mainly changes initialization, voxelization work, and surface density.

### F2 - The normalization change is global and may regress Cornell OBJ

Plan refs: lines 65-72, 101-117, 175-185.

`loadOBJMesh()` has one normalization call for every OBJ (`src/demo3d.cpp:4353`). Replacing that with `normalize(volumeSize.x * 0.5f * 0.95f)` scales Cornell as well as Sponza. The plan then uses Cornell as the regression check while also changing Cornell's mesh scale, surface density, ray entry depth, and light/probe relationship.

That makes the regression ambiguous: if Cornell changes visually, we will not know whether Step 4 broke the pipeline or simply changed the test asset's scale. Cornell currently already bakes with 40,878 seeds in Step 3, so it is not the asset motivating the volume-utilization change.

Recommendation: make the scale explicit per OBJ for Step 4. For example, keep Cornell at `normalize(1.0f)` and apply `normalize(1.9f)` only to Sponza, or introduce a local `targetHalfExtentForOBJ(currentOBJPath)` helper. Once Sponza is proven, decide separately whether Cornell should also fill the full volume.

### F3 - The Sponza camera axis assumption is supported, but the hardcoded camera point is unproven

Plan refs: lines 101-133, 193.

The actual local Sponza bounds do support the plan's broad orientation assumption: X is the longest axis and Y appears to be up. That is a useful correction to the plan, which currently frames this only as a distribution-level assumption.

The exact preset is still a guess:

```cpp
camera.position = glm::vec3( 1.6f, 0.1f, 0.0f);
camera.target   = glm::vec3(-1.0f, 0.1f, 0.0f);
```

After `halfExtent=1.9`, the Sponza X range will be near `[-1.9,1.9]`, with normalized Y extent about `1.59` and Z extent about `2.34`. A camera at `x=1.6` is near the positive-X end, but the plan has not checked whether that point is actually in free atrium space rather than a wall, column, or conservative zero band.

Recommendation: log normalized bounds and camera placement, then validate the first Sponza run with modes 1, 2, 4, 5, and 7. If implementation effort is still small, sample the baked SDF at the proposed camera position or add a temporary "camera SDF" log before calling the preset verified.

### F4 - The root-cause statement is stronger than the evidence

Plan refs: lines 11-19, 217-224.

The plan says mode 0 is dark because the camera is wrong and the mesh uses only the central eighth of the SDF volume. Those are plausible causes, but the Step 3 evidence does not isolate them. Mode 5 is a post-loop step-count heatmap (`res/shaders/raymarch.frag:571-582`), not a pure hit mask. It suggests the SDF/raymarch path is doing work, but it does not by itself prove final shading, normals, albedo, direct light, or cascade sampling are correct for Sponza.

This matters because Step 4 could make mode 5 denser and still leave mode 0 dark if the real blocker is albedo sampling, normal estimation near a conservative band, direct-light placement, shadowing, GI, or exposure. The plan already includes mode 3 and mode 5 checks; it should add mode 4 direct-only and mode 7 ray-distance checks before declaring the issue solved.

Recommendation: reword the goal as "test the two most likely visibility blockers" rather than "mode 0 is dark because..." Keep the two-step implementation order, but add a diagnostic checkpoint after 4a and before 4b: compare Sponza modes 1, 2, 4, 5, and 7 with the old camera.

### F5 - "No cascade changes" is true structurally, but not behaviorally neutral

Plan refs: lines 87-89, 168-169, 208-210.

The plan is correct that FBO sizes, volume textures, and `baseInterval` do not need to change when the OBJ mapping changes. But changing the mesh's world-space scale relative to the fixed volume also changes scene semantics: probe spacing relative to object size, light position relative to walls, final ray distances, shadow rays, conservative hit bands, and effective GI scale.

That is not a reason to reject the plan. It is a reason to stop calling the change "only at the mesh-to-volume mapping" when judging rendered output.

Recommendation: state this as "no resource reallocation or cascade reinit required" rather than "same density goal without touching cascades." Then verify mode 3/6 GI separately from primary visibility.

### F6 - The build-verification wording still needs the warning baseline

Plan ref: line 175.

The plan says "Build clean, no new warnings." The recent local Debug MSBuild succeeded with 0 errors but 36 warnings. That means "no warnings" is not the current baseline.

Recommendation: use "build succeeds; warning count does not increase from the current baseline" unless the implementation also cleans the baseline warnings.

### F7 - The 5 percent margin rationale is plausible but should be measured

Plan refs: lines 74-79, 196.

The margin is a reasonable guard. With voxel size `4/128 = 0.03125`, a 0.1 world-unit margin is about 3.2 voxels, and the current surface radius from the logs is about 0.027. That should usually keep max-axis surface voxels away from the boundary slices.

The plan's explanation is still too absolute: "the boundary slice stays empty" should be verified, not assumed. A quick count of occupied alpha voxels on the six boundary slices after voxelization would turn this from a rationale into evidence.

Recommendation: add a boundary-slice occupancy log for Step 4 verification. If any boundary slice is nonzero, bump the margin to 10 percent or make the margin depend on `surfaceRadius`.

## What I Agree With

- The Step 4 problem selection is right: Sponza visibility should be addressed before material loading or multi-mesh work.
- Adding `normalize(float halfExtent)` while preserving `normalize()` as `normalize(1.0f)` is a clean local API change.
- Keeping volume bounds fixed is preferable to expanding the volume for this step because it avoids resource and cascade reallocation.
- The current `--render-mode=` CLI exists, so the proposed capture commands are usable in the current codebase.
- The post-07 bake-failure path is now fixed in current code, so Step 4 can build on a better render-loop lifecycle than the Step 3 implementation note originally had.

## Recommended Revision Before Coding

1. Make normalization scale per OBJ: Sponza gets `1.9`, Cornell stays at `1.0` for regression unless there is a separate Cornell-rescale experiment.
2. Replace the 8x/300K language with surface-band math and measured acceptance gates.
3. Add actual Sponza bounds to the plan and keep X-long/Y-up as "confirmed by this asset."
4. Treat the camera preset as a first candidate, not a proven view. Validate with debug modes before declaring final visibility fixed.
5. Update build wording to account for the current warning baseline.
6. Add direct-only mode 4 and ray-distance mode 7 captures to the Step 4 verification set.
