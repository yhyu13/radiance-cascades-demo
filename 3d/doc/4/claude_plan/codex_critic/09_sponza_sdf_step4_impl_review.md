# Review: sponza_sdf_step4_impl.md

Reviewed: 2026-05-07T19:19:02+08:00

Target: `doc/4/claude_plan/sponza_sdf_step4_impl.md`

Verdict: useful implementation, not as fully verified as the note says. The per-OBJ scale change is a real improvement and the current build succeeds. Sponza visibility is better than Step 3. But the implementation introduced a light-state lifecycle bug, the camera validation does not mean what the note says for outside-volume cameras, the build-warning claim is wrong, and the final visual evidence is too weak to support several strong verification claims.

## Evidence Checked

- Current source diff in `src/obj_loader.h`, `src/demo3d.cpp`, and `src/demo3d.h`.
- Step 4 logs: `tools/app_run_step4a_*.log`, `tools/app_run_step4b_*.log`, `tools/app_run_step4c_cornell.log`, and `tools/app_run_step4_final_cornell.log`.
- Step 4 screenshots: `tools/step4*.png`, including `step4d_sponza_mode0.png`, `step4d_sponza_mode1.png`, `step4d_sponza_mode4.png`, and `step4_final_cornell_mode0.png`.
- Current Debug MSBuild of `build/RadianceCascades3D.vcxproj`.
- Pixel comparison between `tools/step3_cornell_mode0.png` and `tools/step4_final_cornell_mode0.png`.

## What Is Solid

- `OBJLoader::normalize(float halfExtent)` exists and preserves `normalize()` as the `1.0` default.
- `loadOBJMesh()` now keeps Cornell at `halfExtent=1.0` and scales Sponza to `1.9`, matching the main recommendation from the Step 4 plan review.
- Runtime logs support the measured seed-count claims: Sponza goes from 37,757 to 147,593 seeds, and Cornell remains 40,878.
- Boundary-slice counting exists and logs zero boundary seeds for the captured Sponza/Cornell runs.
- The current source compiles: MSBuild of `RadianceCascades3D.vcxproj` completed with 0 errors.

## Findings

### F1 - `lightPosition` leaks from OBJ scenes into later analytic scenes

Severity: high for interactive scene switching.

The implementation adds `lightPosition` as a member and updates it from `loadOBJMesh()` (`src/demo3d.cpp:4493`). Both the radiance bake and final raymarch now read this member (`src/demo3d.cpp:1631`, `src/demo3d.cpp:1886-1888`).

But `setScene()` clears OBJ state without resetting `lightPosition` (`src/demo3d.cpp:2409-2425`). That means this workflow leaves analytic scenes using the Sponza light:

1. Load Sponza OBJ. `lightPosition = (0, 0.5, 0)`.
2. Click analytic Cornell Box, Empty Room, or Simplified Sponza.
3. `setScene()` disables OBJ and rebuilds history, but the light remains `(0, 0.5, 0)`.

This directly contradicts the implementation note's claim that analytic scenes "keep the Cornell light" (lines 204-208). The existing history reset in `setScene()` is good; it just needs a light reset too.

Recommendation: set `lightPosition` inside `setScene()` for every analytic scene, at least defaulting to `(0, 0.8, 0)` for the current Cornell-style analytic scenes. Keep future per-scene light presets next to the analytic geometry setup, not only in `loadOBJMesh()`.

### F2 - Camera alpha validation is misleading for outside-volume cameras

Severity: medium.

The validation code computes:

```cpp
glm::vec3 uvw = (camPosCandidate - volumeOrigin) / volumeSize;
glm::ivec3 voxel = glm::ivec3(uvw * float(volumeResolution));
voxel = glm::clamp(voxel, glm::ivec3(0), glm::ivec3(volumeResolution - 1));
```

See `src/demo3d.cpp:4472-4478`.

For the final Sponza camera `(3.5, 0.5, 0)`, `uvw.x = 1.375`, so the check clamps to the boundary voxel at `x=127`. For the Cornell camera `(0,0,4)`, `uvw.z = 1.5`, so it also clamps to the boundary. In both cases, `alpha=0` proves only that the nearest clamped boundary voxel is empty. It does not prove the camera position itself is in free space.

That distinction matters because the implementation note uses the alpha result as a camera-free-space validation (lines 57-62, 157-167, 191-196). For outside-volume cameras, the more relevant validation is: ray-box entry exists, the first entry point is not immediately in a zero band, and a representative center ray can hit.

Recommendation: if `uvw` is outside `[0,1]^3`, log "camera outside SDF volume; alpha check skipped" instead of clamping silently. For outside cameras, validate the ray-box entry point or capture mode 1/2/4 from a clean screenshot.

### F3 - Build warning count is not unchanged

Severity: medium.

The note says "warning count unchanged from Step 3 baseline (37 in `3d/src/`)" (lines 24 and 153-155). I rebuilt the current project:

- Command: MSBuild `build/RadianceCascades3D.vcxproj`, Debug
- Result: 0 errors, 39 warnings

So the "unchanged 37" claim is false in this worktree. Several warnings are C4819 code-page warnings, and the Step 4 source edits added more non-ASCII comments and string text around the modified areas. This also conflicts with the repo editing guideline to default to ASCII when editing source.

Recommendation: either reduce the source comments/log strings to ASCII or save the affected files in an encoding MSVC accepts cleanly. Then record the actual warning baseline from the exact build command used.

### F4 - Final Sponza verification is under-documented and UI-obscured

Severity: medium.

The note lists final Sponza captures for modes 0/1/4, but there is no preserved `app_run_step4d_sponza*.log`. The preserved final log is Cornell-only: `tools/app_run_step4_final_cornell.log`.

The final Sponza screenshots also include the full ImGui overlay because `main3d.cpp` takes the CLI `--screenshot` after UI rendering (`src/main3d.cpp:241-263`). This makes the visual claims hard to audit. The images do show something beyond Step 3 darkness, but the note's stronger statements, such as "magenta back wall, green ceiling band, purple columns" and "recognizable atrium architecture", are partly subjective from these captures.

There is also no final `step4d_sponza_mode3.png`, even though the verification table discusses final mode 3 as partial (lines 169-170).

Recommendation: preserve final Sponza run logs for each listed capture and add clean pre-ImGui screenshots or crops. Include final mode 3 if it remains in the verification table.

### F5 - Cornell is visually stable, but not byte-identical

Severity: low.

The note says Cornell remains "byte-identical to Step 3" (lines 7-8 and 139). File hashes disagree. A pixel comparison is much more favorable: `step3_cornell_mode0.png` vs `step4_final_cornell_mode0.png` differs in only 166 of 2,764,800 RGB channel samples, with max absolute difference 1.

That is excellent regression evidence, but it is not byte identity.

Recommendation: replace "byte-identical" with "visually unchanged / pixel-equivalent within 1 LSB except 166 channel samples." That is both more precise and stronger as engineering evidence.

### F6 - Mode 5 is still described like hit coverage

Severity: low to medium.

The note says the 4a mode 5 capture "showed full-screen hit coverage" and "validated the SDF density gain" (lines 144-147 and 162-164). But mode 5 is a step-count heatmap, not a hit mask. The shader emits it after the march loop for non-surface-hit paths too (`res/shaders/raymarch.frag:571-582`).

The Step 4 mode 5 image is useful because the visible rectangle changed compared with Step 3, but "full-screen hit coverage" is not what that buffer means.

Recommendation: call mode 5 "step-count coverage" or "raymarch activity", and use modes 1/2/4 plus final hit logs/screenshots for primary-hit proof.

### F7 - The future light-slider note omits invalidation

Severity: low.

The implementation note says a future UI can drag the light at runtime without code changes (lines 186-189). That is only true for the direct-light uniform in the final raymarch pass. The radiance cascade bake also consumes `lightPosition` (`src/demo3d.cpp:1631`), so changing the light at runtime needs cascade invalidation and likely temporal-history reseeding. Otherwise direct lighting updates while indirect lighting remains stale.

Recommendation: document `lightPosition` as scene-load-owned for now. If a UI slider is added, route it through a setter that marks cascades/history dirty.

## Bottom Line

Step 4 is a real improvement: the Sponza scale fix is correctly scoped, the seed-count result validates the surface-area expectation, and the current build succeeds. Before calling it fully verified, fix the `lightPosition` reset in `setScene()`, make the camera validation honest for outside-volume cameras, update the warning count, and preserve clean final Sponza logs/screenshots.

