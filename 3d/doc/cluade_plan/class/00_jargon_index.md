# Jargon Index

Every term used in the phase docs, defined in plain language.
Terms are grouped by the concept they belong to. Cross-references point to the numbered class docs.

---

## The World / Scene

**SDF (Signed Distance Field)**
A 3D texture where every cell stores "how far is the nearest surface from this point?"
Positive = outside, negative = inside, zero = on the surface.
We use a 64×64×64 grid of floats. → see `02_scene_and_sdf`

**Analytic SDF**
Each scene object (box, sphere) is described by a math formula rather than mesh triangles.
The GPU evaluates the formula instead of tracing rays against polygons.

**Albedo**
The base color of a surface before lighting. Stored in a 64³ RGBA8 texture parallel to the SDF.

**Cornell Box**
Our test scene: a closed room with a red left wall, green right wall, white ceiling/floor/back, and two boxes inside. Standard GI benchmark.

**World Units (m)**
Our scene is 4 m × 4 m × 4 m (centered at origin, running from −2 m to +2 m on each axis).

---

## Rays and Marching

**Ray**
A half-line starting at a point, going in a direction, used to ask "what does this probe see in this direction?"

**Sphere-march / Raymarch**
A method for following a ray through an SDF scene without knowing intersection geometry in advance.
At each step, jump forward by (distance to nearest surface). Guaranteed not to overshoot.
Stops when distance < threshold (surface hit) or when we exceed the ray interval.

**tMin / tMax**
The start and end distance of the ray segment a probe is responsible for.
A probe only cares about surfaces between tMin and tMax meters along each ray.

**Hit sentinel**
The way we encode the ray outcome in a single float stored in the alpha channel:
- `alpha > 0` → surface hit at distance `alpha` meters
- `alpha < 0` → ray exited the volume without hitting anything (sky)
- `alpha = 0` → ray missed everything within [tMin, tMax]

**Shadow ray**
A secondary ray from a surface hit point toward the light source.
If it hits something before reaching the light, the surface is in shadow.
We run 32 sphere-march steps per shadow test.

---

## Probes

**Probe**
A single point in 3D space that fires rays in all directions to sample radiance (incoming light).
Think of it as a tiny omnidirectional light sensor.

**Probe grid (32³)**
We place 32×32×32 = 32,768 probes evenly throughout the 4 m scene.
Default spacing: 4 m / 32 = 0.125 m between adjacent probes.

**Probe cell size**
The spacing between adjacent probes for a given cascade level.
Also equals tMax for that level (the probe is responsible for light up to one cell-width away).

**probeToWorld()**
The function that converts a probe's integer grid index (e.g. probe #5,3,7) into a 3D world position (e.g. −1.375, −1.625, −1.875 m).

**Co-located**
All cascade levels share the exact same 32³ probe positions.
Upper and lower cascade probes at the same grid index occupy the same world point.

**Non-co-located**
Each cascade level uses a different probe count and spacing:
C0=32³, C1=16³, C2=8³, C3=4³. Upper probes are physically displaced from lower probes.
→ see `06_phase5_spatial_options`

---

## Cascades

**Cascade**
One level of the 4-level probe hierarchy. Each level covers a different distance band.
C0 = near field, C3 = far field.

**Cascade index (i)**
The level number: 0, 1, 2, 3. C0 is closest; C3 is farthest.

**Base interval (d)**
The fundamental unit = 0.125 m = 4 m / 32 probes.
Each cascade's tMax = d × 4^i.

**Ray interval**
The [tMin, tMax] range for one cascade level:
```
C0: [0.02,  0.125] m
C1: [0.125, 0.500] m
C2: [0.500, 2.000] m
C3: [2.000, 8.000] m
```
A probe only raymarchs within its interval. Hits outside that range belong to another level.

**Merge**
The process of passing far-field radiance from a coarser cascade into a finer one.
When C0's ray misses (no surface within 0.125 m), it pulls the answer from C1.
C3→C2→C1→C0 in order; higher = farther = coarser.

**Cascade hierarchy**
The full stack of 4 cascades working together so every probe sees light from near to far.

---

## Radiance and GI

**Radiance**
The amount of light traveling through a point in a specific direction. (Units: W/m²/sr.)
Here we use it loosely to mean "the color/brightness seen when looking in direction D."

**GI (Global Illumination)**
Lighting that includes light that bounced off surfaces, not just direct light from the source.
Color bleeding (green wall tinting a white box) is a GI effect.

**Isotropic probe**
A probe that stores a single average radiance value — no direction information.
All directions see the same value. Cheap but causes banding.

**Directional probe / atlas**
A probe that stores one radiance value per direction bin.
Enables correct direction-dependent merge. → see `05_phase5_atlas`

---

## Direction Encoding

**Directional bin**
A small patch of the sphere assigned an integer index.
All rays within that patch share the same bin.

**D (direction resolution)**
The side length of the bin grid. D=4 means a 4×4=16-bin grid covers the full sphere.
Each bin covers roughly 36° of solid angle at D=4.

**Octahedral encoding**
A way to map the full sphere (3D) onto a flat square (2D) using "fold the sphere like origami."
Analytically invertible: we can go sphere→square (dirToOct) and square→sphere (octToDir) exactly.
This makes atlas addressing precise. → see `04_phase5a_direction_encoding`

**dirToOct / octToDir**
The conversion functions between 3D direction vectors and 2D [0,1]² octahedral coordinates.

**dirToBin / binToDir**
The conversion between a direction vector and its integer bin index (0..D−1 in each axis).
`dirToBin(dir, D)` = `floor(dirToOct(dir) * D)`.

---

## Atlas

**Atlas (probe atlas)**
A single 3D texture that packs all probe direction bins for all probes.
Probe (px, py, pz) occupies a tile at x:[px·D, px·D+D), y:[py·D, py·D+D), z:pz.

**Atlas dimensions**
`(probeRes × D) × (probeRes × D) × probeRes` RGBA16F.
At 32³ probes and D=4: 128×128×32. At D=8: 256×256×32.

**Tile**
The D×D block of texels that belongs to one probe within the atlas.
Each texel = one direction bin of that probe.

**GL_NEAREST (on the atlas)**
The texture filtering mode that reads exactly the texel you address — no blending with neighbors.
Required on the atlas to prevent one probe's tile from bleeding into an adjacent probe's tile.

**GL_LINEAR / trilinear**
Texture filtering that blends neighboring texels. Used on the isotropic `probeGridTexture`.
Cannot be used on the atlas (would cross tile boundaries between probes).

**texelFetch**
A GLSL function that reads one texel at an exact integer coordinate, bypassing all filtering.
Used for atlas reads in Phase 5c onward.

**probeGridTexture**
The isotropic 32³ RGBA16F texture that stores the direction-averaged radiance per probe.
Written by the reduction pass (5b-1). Read by `raymarch.frag` for final GI lookup.

**Reduction pass**
A second compute shader (`reduction_3d.comp`) that averages the D² atlas bins per probe
and writes the result to `probeGridTexture`. Runs after each atlas bake.

---

## Merge Details

**Upper cascade**
The next coarser level above the current one. C1 is the upper cascade for C0.
The upper cascade provides far-field radiance when the current cascade's ray misses.

**Miss**
A ray that traveled from tMin to tMax without hitting any surface. Alpha = 0.

**Isotropic merge (Phase 4)**
When a ray misses, pull one averaged value from the upper probe's `probeGridTexture`.
Same value for all ray directions — no directional discrimination.

**Directional merge (Phase 5c)**
When a ray misses, look up the upper cascade's atlas at the exact same direction bin.
The miss in direction D gets the upper level's answer for direction D specifically.

**Directional bilinear (Phase 5f)**
Instead of snapping to the nearest bin, blend the 4 surrounding bin centers.
Smooths hard color steps at bin boundaries. → see `06_phase5_bilinear`

**Blend zone (Phase 4c)**
A distance region near tMax where a surface hit is smoothly blended toward the upper cascade.
Prevents a hard cut when a surface appears just inside the interval boundary.

---

## Compute Shader Terms

**Dispatch**
Launching the compute shader. One dispatch per cascade per frame (when baking).

**gl_GlobalInvocationID**
Built-in GLSL variable giving each thread its unique (x, y, z) index.
We map this directly to `probePos` (the probe's grid index).

**local_size_x/y/z = 4**
Each workgroup processes a 4×4×4 block of probes.

**imageStore / image3D**
Writing to a texture from a compute shader using integer coordinates, bypassing the sampler.
Used to write the atlas.

**barrier()**
A sync point that ensures all threads in a pass finish before the next pass reads the results.
Required between the atlas bake and the reduction pass.

**uniform**
A GPU variable set from CPU code, shared across all shader threads in one dispatch.

---

## Debug / Visualization Terms

**radianceVisualizeMode**
Integer (0–6) controlling what the top-right debug panel shows.
0=slice, 1=max proj, 2=avg, 3=atlas, 4=hit type, 5=bin (nearest), 6=bin (bilinear).

**Mode 4 (HitType heatmap)**
Colors each probe by fraction of rays that hit surface (green), hit sky (blue), or missed (red).

**Mode 5 (Bin nearest)**
Shows a single selected direction bin's radiance for all probes, using nearest-bin lookup.

**Mode 6 (Bin bilinear)**
Shows the bilinearly interpolated radiance at the midpoint between the selected bin and its (+1,+1) neighbor.
Exposes whether bilinear smoothing is working.

**cascadeReady**
A bool flag on the CPU. When false, the next `render()` call re-bakes all cascades.
Every toggle that changes bake output must set this to false.

**`[F]` key**
Keyboard shortcut cycling through `radianceVisualizeMode` 0→6→0.
