# Jargon Index

This is the plain-language dictionary for the branch.

## Core scene terms

`Cornell Box`
A simple test room. Left wall is red, right wall is green, other big surfaces are white. It is useful because wrong global illumination is easy to notice.

`SDF`
Signed Distance Field. A 3D grid where each cell stores how far the nearest surface is.

`Analytic SDF`
The scene is described by math formulas for boxes and other shapes, not by triangle meshes.

`Albedo`
The base color of a surface before lighting is applied.

`World space`
The real 3D coordinate system used by the scene. In this branch the main room lives inside a `4 x 4 x 4` meter volume.

## Ray terms

`Ray`
A starting point plus a direction.

`Raymarch`
Follow a ray through an SDF by repeatedly jumping forward by the stored distance-to-surface value.

`Sphere march`
Same idea as raymarch in this codebase.

`t`
Distance traveled along a ray.

`tMin`
Where a cascade starts caring about hits along a ray.

`tMax`
Where a cascade stops caring about hits along a ray.

`Miss`
A ray that did not hit anything inside that cascade's distance band.

`Hit`
A ray that touched a surface inside that cascade's distance band.

`Shadow ray`
A second ray from a hit point toward the light to check whether something blocks direct light.

`Soft shadow`
An approximation that returns a value between fully lit and fully shadowed instead of only 0 or 1.

## Probe terms

`Probe`
A tiny light sensor placed in 3D space. It asks, "what light is arriving here from each direction?"

`Probe grid`
The 3D lattice of probes.

`Probe cell size`
The distance between adjacent probes.

`probeToWorld()`
Function that converts a probe's integer grid index into a world-space position.

`Probe bake`
Running the compute shader to fill probe textures with lighting data.

## Cascade terms

`Cascade`
One level of the hierarchy. Each level is responsible for a different distance range.

`C0`
The nearest cascade. This is the one the final renderer samples after all merges are done.

`C1`, `C2`, `C3`
Progressively farther cascades.

`Base interval`
The basic distance unit. In this project it starts at `0.125 m`.

`Cascade hierarchy`
The full stack of levels working together.

`Merge`
When a lower cascade misses, it asks the next higher cascade for the far-field answer.

`Upper cascade`
The next coarser level above the current one.

`Directional merge`
Use the upper cascade's answer for the same direction, not just its average color.

`Isotropic merge`
Use one averaged value from the upper cascade for every direction.

## Texture/storage terms

`probeGridTexture`
The 3D texture holding one averaged lighting value per probe. The final raymarch shader reads this for the isotropic GI path.

`Atlas`
The 3D texture that holds per-direction values for every probe. Each probe owns a `D x D` tile.

`Tile`
The block of atlas texels belonging to one probe.

`Reduction pass`
Average all direction bins in a probe's tile back into one isotropic value in `probeGridTexture`.

`Directional GI`
Final-render path that reads the directional atlas directly instead of only the isotropic reduced probe grid.

`probeGridHistory`
Temporal-history version of `probeGridTexture`. When temporal accumulation is on, the display path reads the accumulated history.

`probeAtlasHistory`
Temporal-history version of `probeAtlasTexture`. Phase 10 can write the EMA-blended atlas in the bake path and then swap handles.

`RGBA16F`
Half-float texture format used for probe storage.

`texelFetch`
Read a texture at an exact integer texel coordinate with no filtering.

`GL_NEAREST`
Read one exact texel.

`GL_LINEAR`
Blend neighboring texels together.

## Direction terms

`Directional bin`
A bucket on the sphere. Rays that fall into the same bucket share one stored direction slot.

`D`
Direction resolution per axis of the atlas tile. `D=4` means `4 x 4 = 16` direction bins. The current constructor default is `dirRes=8`.

`Octahedral encoding`
A way to map directions on a sphere into coordinates on a square, so directions can be stored in a 2D bin grid.

`dirToOct()`
Convert a 3D direction to 2D octahedral coordinates.

`octToDir()`
Convert octahedral coordinates back to a 3D direction.

`dirToBin()`
Convert a direction to an integer atlas-bin coordinate.

`binToDir()`
Convert an integer atlas-bin coordinate back to a representative direction.

`Nearest-bin`
Snap to one single direction bin.

`Directional bilinear`
Blend the 4 nearby direction bins instead of snapping to one.

`Trilinear spatial blend`
Blend the 8 neighboring probes in 3D space instead of snapping to one probe.

## Phase 5 layout terms

`Co-located cascades`
All cascades use the same probe positions.

`Non-co-located cascades`
Higher cascades use fewer probes and larger spacing.

`D scaling`
Higher cascades may use more direction bins than lower cascades.

`Scaled D formula`
When D scaling is on, cascade `i` uses `min(16, dirRes << i)`. With the current `dirRes=8` default, that means C0=`D8`, C1=`D16`, C2=`D16`, C3=`D16`.

`Visibility weighting`
A check meant to suppress upper-cascade light if the upper probe cannot see the lower probe's location.

`C0 atlas`
The directional atlas texture for the nearest cascade. C0 is the default final-render cascade, but the current UI can select another cascade for atlas-based GI inspection.

## Temporal and quality terms

`Temporal accumulation`
Blend each fresh probe bake into history over time instead of replacing the previous bake immediately.

`EMA`
Exponential moving average. In this code it means `history = mix(history, current, alpha)`.

`Temporal alpha`
How much the newest bake contributes to history. Lower alpha is smoother but slower to react.

`History clamp`
TAA-style guard that clamps stale history into the current neighborhood range before blending, reducing color bleeding from jittered samples.

`Probe jitter`
Small sub-cell offset applied to probe positions before a bake. The current code uses a Halton sequence so repeated bakes sample different nearby positions.

`Halton sequence`
A low-discrepancy pattern used for deterministic probe jitter.

`Jitter dwell`
Number of frames to hold one jitter position before moving to the next position.

`Staggered cascade updates`
Update lower cascades more often than upper cascades. The current code updates cascade `i` every `min(2^i, staggerMaxInterval)` frames.

`Fused atlas EMA`
Phase 10 path where `radiance_3d.comp` blends the fresh atlas into atlas history during the bake, avoiding a separate atlas `temporal_blend.comp` dispatch in the common path. There is no user-facing toggle for this path; C++ selects it through `doFusedEMA` and the shader gate is `uTemporalActive` with `uAtlasHistory` bound. `temporal_blend.comp` remains loaded as the non-fused fallback.

`GI blur`
Depth/normal/luminance-aware bilateral blur applied to the indirect term, not to direct lighting.

`GBuffer`
Small screen-space buffer containing surface normal and depth for the GI blur pass.

`C0 min range`
Minimum ray reach for cascade 0. The current default is `1.0` world unit, overriding the legacy C0 `tMax = cellSize` behavior.

`C1 min range`
Minimum ray reach for cascade 1. The current default is `1.0` world unit, overriding the legacy C1 `tMax = 0.5` behavior.

`surfPct`
Debug stat: percentage of probes in a cascade with at least one direct surface hit in their interval.

## Capture and analysis terms

`Screenshot analysis`
Press `P` or use the UI to capture a clean frame into `tools/` and optionally run `tools/analyze_screenshot.py`.

`Burst capture`
Three-frame capture of render modes 0, 3, and 6 for comparing final image, indirect x5, and GI-only output.

`Sequence capture`
Multi-frame capture of the current render mode for temporal stability analysis.

`RenderDoc capture`
Press `G`, or launch with `--auto-rdoc`, to capture a full GPU frame through RenderDoc.

`RenderDoc pipeline analysis`
The current two-script flow: `qrenderdoc.exe --py tools/rdoc_extract.py` extracts timing and texture PNGs, then regular Python runs `tools/analyze_renderdoc.py` for Claude analysis.

## Debug terms

`Radiance debug view`
The overlay that shows slices, atlas data, hit types, and bin viewers.

`Overlay mode 0`
Slice view in `radiance_debug.frag` `uVisualizeMode`.

`Overlay mode 1`
Max-projection view in `radiance_debug.frag` `uVisualizeMode`.

`Overlay mode 2`
Average-projection view in `radiance_debug.frag` `uVisualizeMode`.

`Overlay mode 3`
Atlas raw view in `radiance_debug.frag`. Shows the packed `D x D` tile layout for every probe.

`Overlay mode 4`
Hit-type heatmap in `radiance_debug.frag`.

`Overlay mode 5`
Nearest-bin viewer in `radiance_debug.frag`.

`Overlay mode 6`
Bilinear bin viewer in `radiance_debug.frag`.

`Render mode 0`
The normal final image in `raymarch.frag`: direct light plus indirect GI.

`Render mode 1`
Surface normals view in `raymarch.frag`.

`Render mode 2`
Depth view in `raymarch.frag`.

`Render mode 3`
Indirect-times-5 view in `raymarch.frag`. Useful for seeing the isotropic reduced GI signal. If GI blur is enabled, this view goes through the same indirect blur/composite path as modes 0 and 6.

`Render mode 4`
Direct-only view in `raymarch.frag`. Useful for validating shadow-ray behavior without indirect GI.

`Render mode 5`
Step-count heatmap in `raymarch.frag`. Useful for seeing where raymarching is expensive.

`Render mode 6`
GI-only view in `raymarch.frag`. In the current code it respects the directional-GI toggle. If GI blur is enabled, this view is postfiltered too.

`Render mode 7`
Continuous ray-distance heatmap in `raymarch.frag`. Compare with mode 5 to separate real distance structure from integer step-count quantization.

`Render mode 8`
Probe-cell boundary visualization in `raymarch.frag`. Compare with mode 6 to see whether GI banding lines up with probe-cell boundaries.

`A/B`
Compare two versions by toggling a setting and looking for visible differences.
