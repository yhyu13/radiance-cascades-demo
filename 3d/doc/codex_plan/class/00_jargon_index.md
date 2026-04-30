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
The 3D texture holding one averaged lighting value per probe. The final raymarch shader reads this.

`Atlas`
The 3D texture that holds per-direction values for every probe. Each probe owns a `D x D` tile.

`Tile`
The block of atlas texels belonging to one probe.

`Reduction pass`
Average all direction bins in a probe's tile back into one isotropic value in `probeGridTexture`.

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
Direction resolution per axis of the atlas tile. `D=4` means `4 x 4 = 16` direction bins.

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

## Phase 5 layout terms

`Co-located cascades`
All cascades use the same probe positions.

`Non-co-located cascades`
Higher cascades use fewer probes and larger spacing.

`D scaling`
Higher cascades may use more direction bins than lower cascades.

`Visibility weighting`
A check meant to suppress upper-cascade light if the upper probe cannot see the lower probe's location.

## Debug terms

`Radiance debug view`
The overlay that shows slices, atlas data, hit types, and bin viewers.

`Mode 3`
Atlas raw view.

`Mode 4`
Hit-type heatmap.

`Mode 5`
Nearest-bin viewer.

`Mode 6`
Bilinear bin viewer.

`A/B`
Compare two versions by toggling a setting and looking for visible differences.
