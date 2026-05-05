# Jargon Index

Every term used in the phase docs, defined in plain language.
Terms are grouped by the concept they belong to. Cross-references point to the numbered class docs.

---

## The World / Scene

**SDF (Signed Distance Field)**
A 3D texture where every cell stores "how far is the nearest surface from this point?"
Positive = outside, negative = inside, zero = on the surface.
We use a 64×64×64 grid of floats. → see `01_scene_and_pipeline`

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
→ see `07_phase5d_probe_layout`

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
The [tMin, tMax] range for one cascade level. With default `c0MinRange=1.0` and `c1MinRange=1.0`:
```
C0: [0.020, 1.000] m   ← tMax extended by c0MinRange
C1: [0.125, 1.000] m   ← tMax extended by c1MinRange; tMin unchanged
C2: [0.500, 2.000] m
C3: [2.000, 8.000] m
```
Legacy (c0MinRange=0, c1MinRange=0): C0=[0.02,0.125], C1=[0.125,0.500].
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
Enables correct direction-dependent merge. → see `05_phase5b_atlas`

---

## Direction Encoding

**Directional bin**
A small patch of the sphere assigned an integer index.
All rays within that patch share the same bin.

**D (direction resolution)**
The side length of the bin grid. D=8 (default at C0) means an 8×8=64-bin grid covers the full sphere.
Each bin covers roughly 22° at D=8 (36° at D=4). With D-scaling ON, upper cascades use D=16.

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
Written by the reduction pass (5b-1). Read by `raymarch.frag` for the isotropic GI path.
The directional GI path (Phase 5g) bypasses this texture and reads `probeAtlasTexture` directly.

**probeAtlasTexture**
The directional atlas texture — 256×256×32 RGBA16F at C0 (D=8) — storing one radiance value per
direction bin per probe. Written by the cascade bake compute shader. The Phase 5g directional
GI path reads the **selected cascade** atlas (`cascades[selC].probeAtlasTexture`) from the final renderer.
→ see `12_phase5g_directional_gi`

**Directional GI path (Phase 5g)**
The optional final-renderer code path that reads the **selected cascade atlas** (`selC`) instead of the
isotropic `probeGridTexture`. Performs cosine-weighted hemisphere integration over D² bins per
probe and manual 8-probe trilinear spatial blending. Excludes back-facing bins, giving more
directionally correct indirect light than the isotropic average. → see `12_phase5g_directional_gi`

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
Smooths hard color steps at bin boundaries. → see `09_phase5f_bilinear`

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

**Overlay mode 4 (HitType heatmap)** — `radiance_debug.frag` `uVisualizeMode=4`
Colors each probe by fraction of rays that hit surface (green), hit sky (blue), or missed (red).

**Overlay mode 5 (Bin nearest)** — `radiance_debug.frag` `uVisualizeMode=5`
Shows a single selected direction bin's radiance for all probes, using nearest-bin lookup.

**Overlay mode 6 (Bin bilinear)** — `radiance_debug.frag` `uVisualizeMode=6`
Shows the bilinearly interpolated radiance at the midpoint between the selected bin and its (+1,+1) neighbor.
Exposes whether bilinear smoothing is working. Note: different shader than `uRenderMode=6`.

**uRenderMode**
The final renderer's display mode selector (`raymarch.frag`). Key values:
- 0 = final image (direct + indirect + tone map)
- 3 = indirect × 5 (isotropic probe grid only, magnified; always isotropic regardless of directional GI toggle)
- 4 = direct light only (no indirect; shadow ray applies)
- 6 = GI-only (indirect without direct; respects the directional GI toggle — the primary A/B diagnostic for Phase 5g)

**radianceVisualizeMode**
Integer (0–6) controlling what the top-right **atlas debug overlay** shows (`radiance_debug.frag`).
Different shader from `raymarch.frag` — mode numbers coincide but mean different things:
0=slice, 1=max proj, 2=avg, 3=atlas raw, 4=hit type, 5=bin (nearest), 6=bin (bilinear).

**cascadeReady**
A bool flag on the CPU. When false, the next `render()` call re-bakes all cascades.
Every toggle that changes bake output must set this to false.

**`[F]` key**
Keyboard shortcut cycling through `radianceVisualizeMode` 0→6→0.

---

## Temporal Accumulation (Phase 9)

**EMA (Exponential Moving Average)**
Blending formula: `history = mix(history, bake, alpha)`.
With `alpha=0.05`, each new bake contributes 5% weight; history carries 95%.
After ~60 frames the history has converged to the steady-state answer.

**probeGridHistory / probeAtlasHistory**
Companion textures (same dims, RGBA16F) that hold the accumulated history for
`probeGridTexture` and `probeAtlasTexture` respectively. Each cascade level has both.

**temporal_blend.comp**
Fallback EMA blend shader. Runs only when history textures are not yet allocated
(first frame after a cascade resize). The default path is **fused EMA**: the blend is
embedded inside `radiance_3d.comp`, so no separate `temporal_blend.comp` dispatch is needed.

**temporalAlpha**
EMA blend weight. Default 0.05. Lower = smoother but slower convergence.
Must balance jitter pattern size and hold time.

**Probe jitter**
Shifting each probe's world position by a sub-cell offset before each bake.
With temporal accumulation this sub-pixel samples the field, trading noise for
smooth convergence over 16 frames instead of 1.

**probeJitterScale**
Jitter amplitude in probe-cell units. Default 0.06 (≈ ±6% of cell width).
Smaller than old ±0.25 to keep each sample close to the true probe center.

**Halton sequence**
Low-discrepancy sequence that distributes samples evenly.
We use Halton(2,3,5) for the three jitter axes so 8 samples fill the cell
uniformly rather than clustering.

**jitterPatternSize**
Wrap index for the Halton sequence. Default 8 → 8 distinct positions per cycle.
After index 7, the sequence repeats from 0.

**jitterHoldFrames**
How many frames to hold each jitter position before advancing.
Default 2 → 8 positions × 2 frames = 16-frame cycle.

**History clamp (useHistoryClamp)**
TAA-style ghost rejection: before the EMA blend, clamp `history` to the AABB
of current-bake neighbor values. Prevents outdated radiance from persisting
after a light or geometry change. Default ON.

**historyNeedsSeed**
When true, the next temporal dispatch seeds history = current bake (bypasses EMA).
Set automatically when temporal is re-enabled to skip the dark warm-up ramp.

**temporalRebuildCount**
Total EMA dispatches since temporal was last enabled. Used for diagnostics.

---

## Staggered Cascade Updates (Phase 10)

**renderFrameIndex**
Monotonic frame counter, incremented every `render()` call.

**staggerMaxInterval**
Maximum allowed cascade update interval. Default 8.
Cascade `i` updates when `renderFrameIndex % min(1<<i, staggerMaxInterval) == 0`.
With `jitterHoldFrames=2` the bake trigger fires every 2 render frames:
- C0: every bake trigger (≈ every 2 render frames)
- C1: every 2 bake triggers (≈ every 4 render frames)
- C2: every 4 bake triggers (≈ every 8 render frames)
- C3: every 8 bake triggers (≈ every 16 render frames)

**staggerMaxInterval=1**
Disables staggering — all cascades update every frame (useful for debugging).

---

## GI Blur (Phase 9c/13b)

**giFBO**
Framebuffer with 3 linear-space color attachments:
- [0] `giDirectTex` — direct lighting from `raymarch.frag` location=0
- [1] `giGBufferTex` — packed normal (×0.5+0.5) + linearDepth, location=1
- [2] `giIndirectTex` — indirect/GI from `raymarch.frag` location=2

**gi_blur.frag**
Bilateral blur shader applied to `giIndirectTex` using `giGBufferTex` for edge weights.
Output composites blurred indirect onto unblurred direct.

**Edge stops**
Gaussian weights that suppress blur across boundaries:
- Depth: `exp(-|Δd| / depthSigma)` — stops at depth breaks
- Normal: `exp(-|1 − dot(N₁,N₂)| / normalSigma)` — stops at normal breaks
- Luminance (Phase 13b): `exp(-|ΔY| / lumSigma)` — stops within-surface tonal leakage

**giBlurLumSigma**
Luminance edge-stop sigma. Default 0.4. Set to 0 to disable luminance stopping.
Phase 13b addition to prevent dark indirect from bleeding into bright regions
and vice versa on co-planar surfaces.

---

## Range Scaling (Phase 14b/c)

**c0MinRange**
Minimum C0 tMax in world units. Default 1.0. 0 = legacy (tMax = cellSize = 0.125m).
With the default, C0 probes see light up to 1.0m away instead of 0.125m.

**c1MinRange**
Minimum C1 tMax in world units. Default 1.0. 0 = legacy (0.5wu).
Extends C1 reach so it blends smoothly with C0's extended range.

**Legacy interval**
The original tMax formula: `tMax_C0 = cellSize = volumeSize / cascadeC0Res`.
At 32³ this is 4.0 / 32 = 0.125m — very short for most scenes.

---

## Capture Pipeline (Phase 6/12)

**pendingScreenshot**
Flag set when the user requests a screenshot. Cleared by `takeScreenshot()` once written.

**BurstState { Idle, CapM0, CapM3, CapM6, Analyze }**
State machine for burst capture: cycles renderMode through 0→3→6, saving one PNG per mode,
then launches multi-image AI analysis (`analyze_screenshot.py`).

**SeqCapState { Idle, Capturing }**
State machine for sequence capture: captures `seqFrameCount` (default 8) frames,
one per jitter position, then launches temporal-jitter analysis.

**--auto-analyze**
CLI flag: runs burst capture + AI analysis then exits (`setAutoCloseMode(true)`).

**--auto-sequence**
CLI flag: runs sequence capture + AI analysis then exits (`setAutoSequenceMode(true)`).

**--auto-rdoc**
CLI flag: triggers RenderDoc GPU frame capture 8 seconds after launch (`setAutoRdocMode(8.0f)`).

**RenderDoc in-process API**
Accessed via `RENDERDOC_API_1_6_0*`. Must be loaded before `InitWindow()` (before the
GL context is created) so RenderDoc can hook into OpenGL at context creation time.
