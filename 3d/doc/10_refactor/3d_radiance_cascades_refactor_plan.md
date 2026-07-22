# 3D Radiance Cascades Status Judgment and Refactor Plan

**Date:** 2026-07-22
**Status:** Approved design
**Scope:** Rebuild the 3D application shell with a strangler migration, implement a Cornell-first ShaderToy parity kernel, freeze the legacy GI paths until parity gates pass, and retire broken paths only after evidence exists.
**Reference implementation:** `shader_toy/Common.glsl`, `shader_toy/CubeA.glsl`, and `shader_toy/Image.glsl`

---

## 1. Executive Judgment

The user's judgment is correct: the current project does not contain a coherent 3D implementation of the ShaderToy surface-attached radiance-cascade algorithm.

The repository currently contains two different GI experiments:

1. A volumetric probe-grid system in `radiance_3d.comp` with 3D probe grids, octahedral directional bins, SDF tracing, spatial trilinear merge, and temporal accumulation.
2. An experimental surface-attached path in `SurfaceRC` that has chart and ring-packed debug infrastructure, but whose production path is primarily a tuned C0 direct-light atlas.

Neither is semantically equivalent to the reference ShaderToy algorithm. The volumetric system is a separate design. The surface system reproduces some atlas indexing and chart mapping, but it does not implement the reference cascade intervals, angular mapping, hit-chart feedback, upper-cascade weighted merge, radiometric weighting, or final consumer contract.

The latest committed Sponza quality pass is not evidence of a correct radiance-cascade implementation. It is evidence that a scene-specific proxy C0 field can be calibrated to a selected path-tracing statistic. The pass depends on proxy visibility scaling and an irradiance floor in `surface_radiance_debug.comp:40-63`, and it does not establish general transport correctness.

The project does build in its current dirty working-tree state using:

```powershell
cmake --build . --config Release
```

from `3d/build`. This proves compile/link health only. It does not prove runtime, transport, or image correctness.

### Decision

Do not continue patching the existing surface path feature by feature. Rebuild the app shell through a strangler migration and introduce a new Cornell-first reference kernel with explicit contracts and executable semantic gates.

The current volumetric, hybrid, PT, and `SurfaceRC` paths remain buildable as frozen comparison paths. They are not dependencies of the new reference kernel. Deletion begins only after the replacement has passed the locked gates.

---

## 2. What Is Trustworthy Today

### 2.1 Trustworthy infrastructure

The following pieces have useful evidence and may be reused through adapters:

- OpenGL compute-shader loading and dispatch infrastructure.
- Cornell and Sponza scene loading, mesh normalization, SDF generation, and albedo volume generation.
- Camera, CLI, deterministic capture, EXR writing, RenderDoc integration, and PT reference infrastructure.
- Cornell room-plane chart round trips.
- Ring-packed atlas indexing and chart-local coordinate experiments.
- Build integration for `surface_rc.cpp` and `validation_helpers.cpp`.

### 2.2 Last trustworthy algorithmic milestone

The strongest uncompromised surface-path result is the Cornell room-plane chart round-trip and diagnostic tracing work. It proves that selected planar points can be mapped from chart space to world space and back. It does not prove recursive GI.

`doc/9_shadertoy2/critic/reply/05_reply_to_phase2b3_hit_classification_critique.md` records that room-plane UV mapping passed while unknown-hit classification and feedback readiness remained unresolved.

### 2.3 Latest committed integration milestone

Commit `37cb612` established that:

- surface C0 can be bound into the final raymarch shader;
- Cornell and Sponza chart-table round trips can pass;
- the Sponza proxy C0 path can produce finite, nonblack output;
- a calibrated Sponza result can pass the repository's N=512 EXR/PT statistic.

It did not establish:

- exact ShaderToy angular sampling;
- recursive hit-chart feedback;
- valid C1-C5 cascade transport;
- weighted upper-cascade merge;
- physically defined Sponza visibility;
- general mesh charting;
- or readiness to retire volumetric/hybrid rendering.

### 2.4 Current uncommitted work

The working tree modifies:

- `res/shaders/raymarch.frag`
- `res/shaders/surface_radiance_debug.comp`
- `src/demo3d.cpp`

These changes begin a hit-distance alpha contract and a Cornell C0 weighted sampler. They are not a completed hierarchy:

- `raymarch.frag:1041-1046` contains an upper-cascade hook that returns zero.
- C0 alpha is treated mainly as validity in the final sampler.
- downsample still converts alpha to a binary flag.
- no machine evidence proves the new consumer behavior.

The refactor plan must not silently depend on these uncommitted changes. They should be treated as diagnostic evidence and either ported deliberately or discarded when the replacement kernel is implemented.

---

## 3. Root-Cause Analysis

The failure is architectural rather than a single shader bug.

### 3.1 No authoritative algorithm contract

The code permits the same field to mean different things in different passes. The clearest example is alpha:

- the reference stores first-hit distance in `.w` and uses `-1` for sky;
- the volumetric path has used alpha as transparency, validity, visibility fraction, and diagnostic output;
- the surface path now writes hit distance, but later passes reduce it to a boolean.

Without a typed resource contract, a locally plausible shader can invalidate every downstream pass.

### 3.2 Debug milestones became architecture

The surface implementation accumulated debug modes, direct-light bridges, proxy charts, temporal smoothing, and generic image-pyramid passes. Several temporary steps became production inputs without satisfying their original replacement gates.

Examples:

- Phase 2D explicitly describes temporally smoothed direct lighting rather than full GI.
- No Phase 2E implementation report proves the hierarchy.
- the final consumer binds the direct atlas as cascade zero in `demo3d.cpp:3075-3087`.
- Sponza proxy compensation was added to pass a quality gate before the transport topology was complete.

### 3.3 `Demo3D` has no subsystem boundaries

`Demo3D` owns scene state, camera state, SDF generation, multiple GI algorithms, rendering, UI, CLI-facing setters, capture state machines, validation, shader ownership, resource invalidation, and diagnostics. This makes state transitions implicit and allows unrelated paths to share textures and flags.

The result is difficult to reason about:

- shader bindings can select a diagnostic texture instead of a hierarchy texture;
- feedback textures can be swapped independently of whether a feedback pass wrote them;
- legacy and experimental controls share invalidation state;
- stale build-tree shaders can differ from source shaders because CMake copies resources at configure time.

### 3.4 The reference was copied by appearance, not by invariant

The reference algorithm's defining invariants are:

- six ring-packed surface cascades;
- power-of-two coupling between probe spacing and angular resolution;
- a per-cascade ray reach;
- RGB radiance plus first-hit distance;
- chart-local upper-cascade interpolation;
- upper-probe look-back visibility;
- previous-frame hit-chart radiance feedback;
- solid-angle and cosine weighting.

The current implementation preserves only parts of the packing and chart concepts.

### 3.5 Validation checked outputs before transport semantics

Nonblack output, changed pixels, and aggregate PT ratios can pass even when the transport is wrong. Proxy scaling and floors demonstrate this directly. Semantic gates must precede image-quality gates.

---

## 4. Reference Semantics to Preserve

`shader_toy/CubeA.glsl` and `shader_toy/Image.glsl` are byte-identical in this snapshot. They define the cascade update, not two independent stages. `Common.glsl` defines the analytic scene, materials, chart UVs, and tracing helpers.

The first replacement kernel shall preserve the following semantics. The parity kernel is evaluated against a locked reference scene, not the existing OBJ Cornell scene.

### 4.0 Locked `ParitySceneSpec`

Parity mode ports the analytic scene and lighting contract from `shader_toy/Common.glsl`. It does not use the native point-light Cornell scene, mesh SDF, or OBJ proxy charts to satisfy parity gates.

Normative scene rules:

- use the eight charted planes defined by `TraceRay`: floor, ceiling, four room walls, and both sides of the interior wall;
- preserve the circular interior-wall openings, the mirror sphere, the mirror box, and the animated exclusion geometry in `Common.glsl`;
- preserve hit categories: diffuse charted surface, black/non-charted geometry, reflective, emissive, and sky miss;
- use `GetSkyLight`, `GetSunLight`, and `GetSunDirection` as the parity environment and directional-light model;
- use a fixed `referenceTime` recorded in every report so animated geometry and sun direction are deterministic;
- use directional shadow visibility with no inverse-square attenuation in parity mode;
- treat the native point-light Cornell scene as a named post-parity adaptation that cannot satisfy reference parity gates.

The parity tracer returns a typed result:

```cpp
struct ReferenceTraceHit {
    float distance;
    vec3 position;
    vec3 normal;
    ReferenceMaterialKind materialKind;
    vec3 reflectanceOrEmission;
    ChartId chartId;
    vec2 chartUv;
    bool chartValid;
    bool hit;
};
```

`ReferenceMaterialKind` distinguishes at least `Diffuse`, `BlackUncharted`, `Reflective`, `Emissive`, and `Sky`. No sentinel color is allowed outside the isolated reference-port implementation.

The checked-in parity chart table is:

| Chart | World origin | U axis / extent | V axis / extent | Normal | Resolution | Logical base offset |
|---|---|---|---|---|---:|---:|
| Floor | `(0,0,0)` | `+X / 1.0` | `+Z / 1.0` | `+Y` | `256x256` | `(0,0)` |
| Ceiling | `(0,0.5,0)` | `+X / 1.0` | `+Z / 1.0` | `-Y` | `256x256` | `(256,0)` |
| Wall X0 | `(0,0,0)` | `+Y / 0.5` | `+Z / 1.0` | `+X` | `128x256` | `(512,0)` |
| Wall X1 | `(1,0,0)` | `+Y / 0.5` | `+Z / 1.0` | `-X` | `128x256` | `(640,0)` |
| Wall Z0 | `(0,0,0)` | `+Y / 0.5` | `+X / 1.0` | `+Z` | `128x256` | `(768,0)` |
| Wall Z1 | `(0,0,1)` | `+Y / 0.5` | `+X / 1.0` | `-Z` | `128x256` | `(896,0)` |
| Interior front | `(0,0,0.47-1/256)` | `+Y / 0.5` | `+X / 1.0` | `-Z` | `128x256` | `(0,1536)` |
| Interior back | `(0,0,0.53-1/256)` | `+Y / 0.5` | `+X / 1.0` | `+Z` | `128x256` | `(128,1536)` |

The logical packed domain is `1024x3072`: six 256-texel cascade bands for the primary page and six for the interior-wall page. Unused regions are inactive. Edge inclusion, half-texel centers, chart handedness, and atlas clamping follow the expressions in `Common.glsl:147-179` and `CubeA.glsl:68-121` exactly and are covered by golden vectors.

### 4.1 Six cascades

The reference derives the cascade from a 256-texel band in a 1536-texel stack:

```glsl
probeCascade = floor(mod(UV.y, 1536.0) / 256.0);
probeSize = pow(2.0, probeCascade + 1.0);
```

Required replacement behavior:

- exactly C0-C5 for the parity kernel;
- `probeSize = 2^(cascade + 1)`;
- spatial probe density decreases as angular resolution increases;
- the sixth cascade is reachable and represents the far field.

### 4.2 Square-ring hemisphere mapping

The reference uses `probeThetai = max(abs(probeRel.x), abs(probeRel.y))` and a piecewise square-perimeter azimuth. The replacement must port this mapping directly before introducing any alternative parameterization.

The current clamped-square-to-disk mapping is not accepted as parity.

### 4.3 Distance reach

The reference uses:

```glsl
tInterval = (1.0 / 64.0) * probeSize * 2.0;
```

and treats C5 as effectively unbounded.

Every locked reference chart has isotropic world scale `1/256` per texel. The parity kernel therefore uses the reference equation directly, including C5's `10000.0` maximum. A generalized world-space derivation is post-parity work. Every cascade must have a distinct, testable maximum reach.

### 4.4 Atlas payload

The parity atlas contract is:

```text
RGB = weighted directional irradiance contribution arriving at this surface probe
A   = first-hit distance in reference-scene world units
A < 0 = sky or unbounded miss
```

Invalid chart texels are not represented by overloading alpha. Chart validity comes from layout metadata or a separate mask.

### 4.5 Surface-hit transport

Let `B(hit)` be the sum of the exact four previous-frame C0 directional values addressed by `CubeA.glsl:166-170`. Let `D(hit)` be `sunLight * max(dot(hit.normal, sunDir), 0)` when the directional shadow ray misses all blockers, otherwise zero. Let:

```text
solidAngleWeight =
    (cos(theta - PI/probeSize) - cos(theta + PI/probeSize))
    / (4 + 8*floor(probeThetaIndex))
lambertWeight = cos(theta)
W = solidAngleWeight * lambertWeight
```

The local RGB before upper merge is locked by hit category:

```text
Sky:              GetSkyLight(probeDir) * W, alpha = -1
Diffuse frontface: (B(hit) + D(hit)) * hit.reflectance * W, alpha = hit.distance
Diffuse backface:  zero, alpha = hit.distance
Black uncharted:   zero, alpha = hit.distance
Reflective:        zero in the parity kernel, alpha = hit.distance
Emissive:          hit.emission * W, alpha = hit.distance
```

This preserves the operation ordering in `CubeA.glsl:150-192`. Sky is not material-modulated. Previous-frame feedback reads only completed C0 at the traced hit chart and UV. Reading the current destination texel is temporal smoothing, not recursive transport.

### 4.6 Upper-cascade merge

For C0-C4, the pass must reproduce `WeightedSample` and the merge in `CubeA.glsl:21-41,195-218`:

- derive each candidate upper probe's world position from chart-local coordinates;
- derive the look-back direction from candidate upper probe to the current probe;
- quantize that look-back azimuth to `phiUV` using the reference square-ring perimeter rules;
- fetch first-hit distance from that distinct look-back direction address;
- accept sky or candidates whose look-back cone reaches the current probe, with the reference `+0.01` bias;
- fetch the separate four upper radiance-bin addresses corresponding to the lower ray's direction region;
- bilinearly interpolate and renormalize over visible candidate probes;
- clamp at chart edges using the reference half-texel convention;
- return finite zero when no candidate is visible;
- blend local RGB and upper RGB using the current local ray hit distance.

The transition is normative:

```text
base = (1/256) * probeSize * 1.5
C0: interpMinDist = 0,    interpMaxInterval = 2*base
C1-C4: interpMinDist = base, interpMaxInterval = base
l = 1 - clamp((rayHit.distance - interpMinDist) / interpMaxInterval, 0, 1)
result.rgb = local.rgb*l + upper.rgb*(1-l)
result.alpha = local.alpha
```

C5 performs no upper read. A miss retains the trace maximum in local `rayHit.distance` for transition math while its stored alpha remains the negative sky sentinel.

Generic image downsample/upsample is not an accepted substitute.

### 4.7 Final consumer

The captured ShaderToy snapshot does not include an independent final image compositor: `Image.glsl` is identical to `CubeA.glsl`. Therefore image-compositor equivalence is not part of semantic parity.

The native final surface shader shall consume the converged C0 representation through one documented adaptation function. It reconstructs the same four C0 directional values used by hit-chart feedback. The resulting sum is already directionally integrated by the stored weights. The renderer must explicitly declare whether it applies visible-surface albedo and a Lambert `1/pi` factor, and whether direct lighting is composited separately. Those choices are native display policy, recorded in the `LightingView` schema, and cannot alter G1-G8 parity results.

The consumer must never replace C0 with a direct-light texture or average bins whose values are already weighted contributions.

---

## 5. Considered Refactor Strategies

### 5.1 Patch the existing `SurfaceRC`

**Advantages**

- least file movement;
- reuses existing debug UI and chart data;
- may produce visible changes quickly.

**Rejected because**

- temporary modes and incompatible resource meanings remain intertwined;
- final binding, feedback, hierarchy, and consumer behavior are already contradictory;
- each repair depends on unproven assumptions from earlier phases;
- it encourages another sequence of local patches without a stable contract.

### 5.2 Build only an isolated parity kernel inside `Demo3D`

**Advantages**

- isolates algorithm work;
- allows quick Cornell parity experiments;
- retains existing app behavior.

**Rejected as the complete plan because**

- `Demo3D` remains the owner of all mutable state and pass scheduling;
- new code would still depend on implicit app-wide invalidation and texture binding;
- the project would retain the architecture that allowed the current failure.

### 5.3 Strangler app-shell rebuild with a parity kernel

**Selected.**

Build a new application coordinator beside `Demo3D`. Move capabilities behind explicit interfaces while keeping the legacy executable path available. Introduce the reference kernel only through the new shell. Migrate one vertical slice at a time and keep every intermediate state runnable.

**Advantages**

- corrects algorithm and orchestration boundaries together;
- retains PT, capture, scene, and legacy GI as comparison infrastructure;
- supports incremental verification;
- permits deletion based on evidence rather than hope.

**Costs**

- more up-front structure than a shader patch;
- temporary adapters exist during migration;
- both shells must build until cutover.

---

## 6. Target Architecture

### 6.1 High-level structure

```text
main3d
  |
  +-- App3D
       |
       +-- RuntimeConfig
       +-- SceneSystem
       +-- RenderResourceSystem
       +-- ReferenceSurfaceRC
       +-- LegacyGIBridge
       +-- FinalRenderer
       +-- ValidationSystem
       +-- CaptureSystem
       +-- DebugUI
```

`App3D` is a coordinator. It owns frame order and subsystem lifetimes, but not algorithm internals.

### 6.2 `RuntimeConfig`

Responsibilities:

- parse CLI once;
- store immutable startup configuration;
- store a small explicit set of runtime toggles;
- validate incompatible combinations before GPU initialization.

It replaces the large family of state-mutating public setters on `Demo3D`.

### 6.3 `SceneSystem`

Responsibilities:

- load analytic or OBJ scenes;
- own mesh normalization transforms;
- produce immutable `SceneSnapshot` values;
- build or request SDF and albedo resources;
- expose material and light data;
- expose the locked analytic `ParitySceneSpec` through a reference-scene provider.

It does not own GI textures or render-mode decisions.

### 6.4 `RenderResourceSystem`

Responsibilities:

- compile and reload shaders from one canonical source directory;
- own common GPU allocation helpers;
- assign resource labels;
- perform OpenGL error and completeness checks;
- record shader source hashes in validation output.

Resource copying must be build-time synchronized or eliminated. Runtime must not silently load stale configure-time copies.

### 6.5 `ReferenceSurfaceRC`

Responsibilities:

- own six persistent atlas pairs plus explicit history-valid state;
- own Cornell chart layout metadata;
- execute top-down cascade updates;
- implement square-ring direction construction;
- trace the locked interval for each cascade;
- perform hit-chart feedback, direct lighting, material response, weighting, and upper merge;
- expose only the converged C0 read view and diagnostic read-only views.

It receives immutable `SceneSnapshot` and frame parameters. It does not read UI state, global render mode, or legacy GI textures.

Suggested internal components:

```text
ReferenceSurfaceRC
  +-- SurfaceChartLayout
  +-- CascadeLayout
  +-- CascadeAtlasSet
  +-- ReferenceRayTracer
  +-- SurfaceTransportPass
  +-- SurfaceRCValidator
```

These may begin in one module and split only where ownership is real. The important boundary is between the algorithm and the app shell, not maximizing file count.

### 6.6 `LegacyGIBridge`

Responsibilities:

- adapt the existing volumetric, hybrid, PT, and old `SurfaceRC` paths to the new shell;
- expose each path as a named render backend;
- prevent legacy textures from entering `ReferenceSurfaceRC`;
- preserve baseline and diagnostic access until retirement.

No new transport feature is added to legacy backends during parity work. Only build fixes and baseline-preserving fixes are allowed.

### 6.7 `FinalRenderer`

Responsibilities:

- consume a typed `LightingView` selected by the coordinator;
- render the scene and optional comparison views;
- contain one reference surface-RC consumer;
- reject missing or mismatched resource contracts.

It must not know how reference cascades are built.

### 6.8 `ValidationSystem`

Responsibilities:

- run CPU and GPU invariant tests;
- read back selected texels and structured counters;
- write deterministic JSON reports;
- return nonzero process status when a required gate fails;
- record scene, shader hashes, configuration, GPU, and commit/working-tree metadata.

### 6.9 `CaptureSystem`

Responsibilities:

- deterministic PNG/EXR capture;
- PT accumulation control;
- RenderDoc trigger and artifact naming;
- no ownership of GI algorithm state.

### 6.10 `DebugUI`

Responsibilities:

- render subsystem-provided debug views;
- request typed commands through the coordinator;
- avoid directly mutating GPU resources or algorithm internals.

---

## 7. Locked Data Contracts

These contracts are parity invariants, not implementation suggestions.

### 7.1 `SceneSnapshot`

Conceptual fields:

```cpp
struct SceneSnapshot {
    SceneId id;
    Bounds worldBounds;
    GpuTextureView sdf;
    GpuTextureView albedo;
    LightSet lights;
    MaterialSet materials;
    optional<ParitySceneSpec> parityScene;
    uint64_t revision;
};
```

Impact:

- a GI pass sees one consistent scene revision;
- scene mutation cannot occur halfway through a cascade update;
- algorithms no longer reach into `Demo3D` members.

### 7.2 `SurfaceChart`

Conceptual fields:

```cpp
struct SurfaceChart {
    ChartId id;
    vec3 origin;
    vec3 tangent;
    vec3 bitangent;
    vec3 normal;
    vec2 worldExtent;
    vec2 worldUnitsPerTexel;
    uvec2 texelResolution;
    uvec2 logicalBaseOffset;
    ChartEdgeConvention edges;
    MaterialId material;
    bool active;
};
```

`tangent`, `bitangent`, and `normal` are unit axes. `worldExtent` supplies physical chart size. `worldUnitsPerTexel` is locked to `(1/256, 1/256)` for all parity charts. `logicalBaseOffset` is the chart's C0 reference-logical offset from the table in Section 4.0. `ChartEdgeConvention` records half-texel centers, inclusivity, UV origin, axis direction, and clamping behavior. Mesh-driven charts are explicitly outside the first parity milestone.

### 7.3 `CascadeDescriptor`

Conceptual fields:

```cpp
struct CascadeDescriptor {
    uint32_t index;
    uint32_t probeSize;
    float maxTraceDistance;
    bool unbounded;
    AtlasRect primaryGlobalRect;
    AtlasRect interiorGlobalRect;
    AtlasRect physicalStorageRect;
};
```

The parity kernel distinguishes the reference's global logical coordinates from physical per-cascade textures:

```text
primaryGlobalRect(c)  = (0, 256*c,        1024, 256)
interiorGlobalRect(c) = (0, 1536 + 256*c, 1024, 256)
physicalStorageRect   = (0, 0, 1024, 512)

globalToPhysical(c, p):
  if p is in primaryGlobalRect(c):
      return (p.x, p.y - 256*c)
  if p is in interiorGlobalRect(c):
      return (p.x, 256 + p.y - (1536 + 256*c))
  return inactive

physicalToGlobal(c, p):
  if p.y < 256:
      return (p.x, p.y + 256*c)
  return (p.x, p.y - 256 + 1536 + 256*c)
```

Reference formulas and golden vectors are authored in global logical coordinates. GPU storage may use one `1024x512` texture per cascade, but every address crosses the checked `globalToPhysical` mapping. G2, G6, and G7 verify both mapping directions and the final physical fetch address. `maxTraceDistance` is defined by the locked reference constants, logged, and tested. It is never inferred independently in multiple shaders.

### 7.4 `SurfaceCascadeTexel`

Logical contract:

```text
rgb: finite, nonnegative, weighted directional irradiance contribution
a >= 0: first-hit distance in world units
a < 0: sky/unbounded miss
```

Chart inactivity is represented separately. A miss is not zero alpha. A valid hit is not binary alpha. The payload schema is versioned as `ReferenceSurfaceTexelV1`.

### 7.5 `GpuTextureView` and completion contract

Every texture crossing a subsystem boundary carries:

```cpp
struct GpuTextureView {
    GLuint object;
    GLenum target;
    GLenum internalFormat;
    uvec3 dimensions;
    uint32_t mipLevel;
    FilterContract filter;
    PayloadSchema schema;
    uint64_t sceneRevision;
    uint64_t transportRevision;
    uint64_t historyGeneration;
    CompletionToken completion;
    ResourceLifetime lifetime;
};
```

Reference cascade textures require `RGBA32F` during parity validation unless a later precision gate approves another format. Fetches that inspect first-hit distance use integer texel fetches or nearest filtering; linear filtering of the alpha distance/sky sentinel is forbidden. The view also carries chart-layout identity and cascade index through its payload metadata.

`CompletionToken` proves that the entire producing pass generation completed and the required barrier was issued. A view without a valid token cannot be rendered, validated, swapped, or exposed as ready.

### 7.6 Temporal atlas contract

Each cascade owns two textures:

```text
read  = complete previous frame
write = current frame under construction
```

Rules:

- a pass never reads its own output target;
- cascade C may read completed current-generation `write[C+1]`;
- all temporal bounce feedback reads only completed previous-generation `read[C0]`;
- swap occurs only after all six current-frame cascades complete;
- a failed or skipped pass does not swap;
- first frame and revision changes set `historyValid=false`;
- cleared textures use RGB zero and alpha `-1`, but no feedback lookup occurs while history is invalid;
- a failed generation leaves the read set and history generation unchanged;
- debug readback names the exact read/write side.

The hit-chart feedback lookup reconstructs the exact four C0 directional values addressed by `CubeA.glsl:166-170`.

### 7.7 `LightingView`

The final renderer receives a tagged variant carrying backend-specific typed resources, not an enum alone:

```cpp
using LightingView = variant<
    DirectOnlyView,
    ReferenceSurfaceC0View,
    LegacyVolumetricView,
    LegacySurfaceView,
    PathTracedView,
    HybridView>;
```

`ReferenceSurfaceC0View` includes the typed C0 texture, chart-layout identity, payload schema, scene and transport revisions, completion token, history generation, and native display-policy version. The selected type and resource revision are logged. A direct atlas cannot masquerade as `ReferenceSurfaceC0`.

Reference backend state is explicit:

```text
Unavailable -> HistoryInvalid -> Updating -> Ready
```

Only `Ready` may produce a `ReferenceSurfaceC0View`.

---

## 8. Locked Frame Flow

For one reference-RC frame:

```text
1. App3D obtains immutable `SceneSnapshot` revision N containing the locked `ParitySceneSpec`.
2. ReferenceSurfaceRC validates resource revisions.
3. Update C5 into current write atlas, using previous completed read[C0] only if historyValid.
4. Issue the image-write/texture-fetch barrier; mark write[C5] complete.
5. Update C4, reading completed current-frame write[C5] and previous read[C0].
6. Barrier; mark write[C4] complete.
7. Repeat update then barrier for C3, C2, and C1.
8. Update C0, reading completed current-frame write[C1] and previous read[C0].
9. Issue the final barrier for rendering, validation, and readback.
10. Mark the six-atlas write generation complete.
11. Swap read and write sets atomically and increment history generation.
12. Set historyValid=true and backend state Ready.
13. FinalRenderer consumes the newly completed C0 read view.
14. Validation and capture consume read-only completed views.
```

The upper cascade is available from the same frame. Recursive bounce feedback comes only from the previous completed frame. This separates spatial hierarchy dependencies from temporal recurrence.

If allocation, validation, dispatch, or a barrier check fails at any step, the chain stops, no swap occurs, the write generation is incomplete, and the previous read generation remains unchanged.

---

## 9. Error Handling and Invalidation

### 9.1 Fail closed

If a required reference shader, texture, chart, or contract is invalid:

- mark the reference backend unavailable;
- render a diagnostic fallback;
- emit a structured error;
- return nonzero in validation/headless mode;
- do not reuse stale output while claiming a successful update.

Required failure-injection coverage includes shader compile failure, texture allocation failure, missing parity charts, scene-revision change, and a forced failure after an upper-cascade dispatch. Every case must prove: no swap, no new completion token, no `Ready` state, no stale-success log, and nonzero validation exit.

### 9.2 Revision-based invalidation

The following changes increment scene or transport revisions:

- scene geometry;
- normalization transform;
- SDF/albedo rebuild;
- chart layout;
- material or light changes;
- cascade layout or interval derivation;
- algorithm shader reload.

A revision mismatch clears temporal history before the next update.

Camera-only changes do not clear world-space surface radiance unless a later camera-dependent optimization explicitly requires it.

### 9.3 No hidden calibration in parity mode

Parity mode forbids:

- scene-specific radiance floors;
- proxy visibility constants;
- bypassed shadow tests;
- undocumented GI multipliers;
- post-hoc exposure selected to pass a metric.

Any deliberate divergence must have a named runtime mode and cannot satisfy parity gates.

---

## 10. Semantic Validation Gates

The user selected semantic invariants rather than direct ShaderToy image comparison as the Cornell parity bar. Native PT remains available as a later quality check, but it does not replace these gates.

Every gate writes JSON and returns failure when the locked predicate fails.

### Gate G0: Build and shader source integrity

Required evidence:

- Release build passes;
- all required reference shaders compile and link;
- runtime shader hashes match source-tree hashes;
- no OpenGL error during allocation or first dispatch;
- the selected backend and scene revision are logged.

### Gate G1: Chart contract

For every active Cornell chart:

- tangent, bitangent, and normal are orthonormal within epsilon;
- handedness matches the reference;
- chart corners map to expected world positions;
- deterministic `chartToWorld -> worldToChart` samples recover chart ID and UV;
- inactive texels never dispatch transport.

Round-trip tests must include independently authored expected points and the checked-in chart table from Section 4.0, not only a function tested against its inverse.

### Gate G2: Cascade layout

For C0-C5:

- cascade band is reachable;
- `probeSize` is exactly `2^(c+1)`;
- probe and direction index decoding matches a CPU oracle;
- atlas coordinates remain inside the owning chart and band;
- direction count and probe count obey the reference coupling.
- global-logical and physical per-cascade addresses round-trip through the locked mapping.

Golden fixtures derived directly from `CubeA.glsl` are authored in global-logical coordinates and must cover exact atlas coordinates, probe positions, boundary probes, band transitions, and inactive regions. Each fixture also records the expected physical cascade texture and texel. CPU-vs-GLSL agreement is secondary evidence.

### Gate G3: Direction mapping

For representative center, edge, corner, and all perimeter indices:

- CPU and GLSL square-ring directions match within epsilon;
- directions are finite and normalized;
- directions lie on the chart-normal hemisphere;
- ring azimuth order is continuous and has no duplicate or missing perimeter bins;
- solid-angle weights are finite and their hemisphere sum is within the derived tolerance.

Golden fixtures lock the reference's exact constants and branch boundaries, including its `3.14192653` theta literal, the `PI` used for azimuth and weighting, half-texel centers, precision, and tolerances. A cleaned-up constant is a deliberate post-parity divergence.

### Gate G4: Interval contract

For every cascade and chart:

- logged reach matches the CPU derivation;
- reaches are monotonic;
- C0-C4 use finite distinct reaches;
- C5 uses the locked reference maximum `10000.0` and reaches the entire parity scene;
- synthetic blockers immediately inside and outside each boundary classify correctly.

Parity fixtures lock reaches to the exact reference formula and C5 value. The C0 special merge transition is tested independently from ray reach.

### Gate G5: Payload contract

With deterministic synthetic rays:

- hit alpha equals first-hit world distance within tolerance;
- sky alpha is negative;
- inactive texels are identified by layout/mask, not alpha;
- RGB is finite and nonnegative;
- no pass converts distance alpha into a boolean;
- all consumers declare the same payload schema version.

### Gate G6: Upper-cascade weighted merge

Using synthetic atlas values with analytically known output:

- the correct four candidate upper probes are selected;
- each candidate's global-logical look-back `phiUV` and distance-fetch address, plus its physical fetch address, match golden fixtures;
- the distinct global-logical four-bin radiance addresses and physical fetch addresses match golden fixtures;
- bilinear weights match a CPU oracle;
- occluded upper samples contribute zero;
- visible weights renormalize correctly;
- no-visible-sample behavior is explicit and finite;
- C0-C4 blend local and upper results at the correct distance transition;
- C5 performs no upper read.

Fixtures cover chart edges, the reference `+0.01` visibility bias, sky-visible candidates, C0's special transition interval, and miss transition behavior. The alpha used for visibility must come from the look-back direction texel, not from one of the radiance-bin texels.

### Gate G7: Temporal hit-chart feedback

With direct lighting disabled and a seeded previous-frame chart:

- a traced hit reads the hit chart and hit UV;
- changing the source hit texel changes the destination result;
- changing the destination's previous texel alone does not emulate a bounce;
- feedback reads only previous-generation C0 and reconstructs the exact four reference bins through the locked global-to-physical mapping;
- frame N reads only completed frame N-1 feedback;
- no read/write image alias exists;
- reset and scene revision changes set history invalid and suppress lookup until a completed generation exists;
- initialized storage uses the locked negative miss sentinel rather than ambiguous zero alpha.

### Gate G8: Material and direct-light transport

For controlled Cornell points:

- direct lighting uses the locked ShaderToy directional sun and visibility at fixed reference time;
- no inverse-square attenuation is used in parity mode;
- material reflectance modulates hit transport;
- red/green wall identity appears in transport probes without a scene-specific multiplier;
- miss rays receive environment radiance;
- solid-angle and Lambert weighting match a CPU oracle.

Golden cases cover diffuse frontface, diffuse backface, black uncharted geometry, reflective zero contribution, emissive contribution, sky, and occluded sun. A separate native Cornell point-light adaptation must test inverse-square attenuation but cannot satisfy this parity gate.

### Gate G9: Final consumer

- final rendering binds the completed reference C0 view;
- direct and hierarchy textures have distinct resource identities;
- surface classification selects expected chart/UV samples;
- disabled reference RC is bitwise or tolerance-equivalent to the selected baseline path;
- no upper-cascade stub or zero-return hook remains on the selected path.

The native display policy declares four-bin reconstruction, visible-surface albedo policy, `1/pi` policy, and separate-direct-light policy. Changing that policy does not change the G1-G8 reference atlas.

### Gate G10: Determinism and stability

- fixed scene, shader, seed, and frame count produce repeatable counters and readbacks;
- no NaN/Inf is present;
- radiance remains bounded under multiple feedback frames;
- energy changes monotonically toward a stable interval under the locked Cornell setup;
- validation reports fail the process when any required predicate fails.
- failure injection leaves the previous read generation unchanged and never emits a ready completion token.

### Gate policy

A phase cannot advance on screenshots alone. A gate is passed only by a checked-in schema and machine-readable artifact generated by executable code. Expected output pasted into a plan is not evidence.

The repository must include checked-in golden vectors derived directly from `Common.glsl` and `CubeA.glsl`. At minimum they cover chart definitions, atlas addresses, probe positions, direction indices, weights, intervals, transition factors, look-back addresses, radiance-bin addresses, visibility decisions, hit-chart feedback addresses, and every material/hit category. The vectors record numeric constants, fixed reference time, precision assumptions, sampler rules, and tolerances. A CPU oracle and GLSL agreeing with each other cannot override a failed golden vector.

---

## 11. Refactor and Migration Phases

Each phase preserves a runnable application. Each phase has a narrow exit gate. Do not begin transport tuning before the corresponding semantic gate passes.

### Phase 0: Freeze and baseline

**Goal:** Establish a reproducible starting point without modifying legacy output.

Work:

- inventory dirty tracked and untracked files;
- preserve user work without reverting it;
- record baseline build, runtime arguments, shader hashes, GPU, and captures;
- label legacy backends explicitly;
- document that old `SurfaceRC` is experimental and non-parity;
- make validation commands return reliable exit codes;
- fix shader-source synchronization so source and runtime hashes can agree.

Exit:

- G0 passes for the existing executable;
- baseline artifacts can be reproduced from one documented command;
- no algorithm claim is made.

### Phase 1: Introduce the strangler shell

**Goal:** Add `App3D` beside `Demo3D` without changing rendered output.

Work:

- introduce the minimum frame coordinator and backend selection;
- wrap the entire existing `Demo3D` runtime in one opaque `Demo3DBackend` adapter;
- parse only the startup options needed to select legacy or reference mode;
- establish canonical shader-source loading and typed GPU resources needed by the parity slice;
- do not migrate PT, hybrid, volumetric, old surface internals, UI, or capture ownership yet;
- retain the old path as the default during this phase.

Exit:

- old and new entry modes produce equivalent baseline output;
- both modes build and run;
- backend selection and resource revisions are logged;
- no GI code is duplicated into the new shell;
- legacy internals remain frozen behind one adapter.

### Phase 2: Add the minimal parity scene slice

**Goal:** Supply the new reference kernel without migrating every legacy backend.

Work:

- implement the locked analytic `ParitySceneSpec` and `ReferenceTraceHit`;
- introduce a minimal immutable `SceneSnapshot` used only by the reference backend;
- check in the exact eight-chart table and golden vectors;
- expose fixed reference time, analytic materials, sky, and directional sun;
- leave `Demo3DBackend` scene/SDF/PT state untouched.

Exit:

- the legacy adapter still reproduces its Phase 0 baseline;
- the reference backend can acquire a revisioned parity scene snapshot;
- G1 and parity-scene golden vectors pass.

### Phase 3: Build the parity layout kernel

**Goal:** Implement atlas indexing and direction construction without lighting.

Work:

- allocate six atlas pairs;
- implement the golden fixtures and then a CPU layout oracle;
- implement GLSL layout decode;
- port the exact square-ring mapping;
- use the locked `1/256` chart scale and exact reference cascade reach;
- write diagnostic direction and interval outputs only.

Exit:

- G2, G3, and G4 pass;
- all six cascades are visible in readback evidence;
- no radiance, feedback, or merge code is enabled yet.

### Phase 4: Implement local single-cascade transport

**Goal:** Produce correct independent per-cascade ray results.

Work:

- trace the locked analytic `ParitySceneSpec` only;
- write RGB plus first-hit distance;
- implement sky, every locked material category, and directional-sun visibility at fixed reference time;
- apply solid-angle and Lambert weighting;
- keep upper merge and temporal bounce disabled.

Exit:

- G5 and G8 pass;
- per-cascade synthetic and Cornell probes match CPU oracles;
- no calibration floor, proxy visibility, or direct-scale bypass exists.

### Phase 5: Implement upper-cascade merge

**Goal:** Reproduce reference top-down spatial hierarchy behavior.

Work:

- update C5 to C0;
- implement chart-local candidate-probe and radiance-bin lookup;
- implement distinct look-back direction quantization and distance fetch;
- implement look-back visibility from upper hit distance with the reference bias;
- implement visible-weight renormalization;
- implement distance-transition blend;
- remove all generic image-pyramid code from the reference path.

Exit:

- G6 passes for every lower cascade;
- perturbing C1 changes C0 through the expected path;
- final reference C0 depends on all reachable upper levels;
- C5 makes no invalid upper read.

### Phase 6: Implement temporal hit-chart feedback

**Goal:** Close recursive multi-bounce transport correctly.

Work:

- classify traced diffuse hits to reference chart and UV;
- reconstruct the exact four-bin completed previous-frame C0 value at the hit;
- add directional sunlight before material and directional weighting;
- enforce read/write atlas separation and atomic swap;
- add deterministic reset and revision invalidation.

Exit:

- G7 and G10 pass;
- a controlled bounce propagates between two different charts;
- same-texel EMA cannot satisfy the test;
- multi-frame radiance is finite and stable.

### Phase 7: Integrate the final consumer

**Goal:** Render Cornell through the proven reference C0 contract.

Work:

- add `ReferenceSurfaceC0` to `LightingView`;
- implement one chart-aware final sampler;
- bind only the completed C0 read view;
- preserve legacy and direct-only comparison modes;
- expose diagnostic atlas views as read-only.

Exit:

- G9 passes;
- reference-disabled baseline remains controlled;
- direct atlas and hierarchy atlas cannot be confused;
- no upper-cascade stub remains in selected code.

### Phase 8: Cornell semantic parity milestone

**Goal:** Declare the Cornell reference kernel semantically complete.

Work:

- run G0-G10 from a clean launch;
- capture deterministic diagnostic artifacts;
- compare native output to the PT reference as a non-blocking quality report;
- document deliberate differences from ShaderToy, if any;
- prohibit progression if a difference alters a locked invariant.

Exit:

- all semantic gates pass in one report;
- no hidden calibration constants exist;
- Cornell produces stable indirect transport and color bleeding;
- the milestone is labeled semantic parity, not general mesh support.

### Phase 9: Cut over the app shell

**Goal:** Make `App3D` the default runtime and reduce legacy orchestration.

Work:

- migrate remaining UI and CLI commands to typed coordinator commands;
- extract full `SceneSystem` from `Demo3D` and adapt volumetric, PT, hybrid, and old surface paths through `LegacyGIBridge`;
- migrate resource, final-render, PT/hybrid, capture, validation, and UI ownership to their target subsystem interfaces;
- stop constructing `Demo3D` in the default path;
- retain an explicit legacy executable or backend during a deprecation window;
- update README and runtime instructions to reflect actual architecture.

Exit:

- all supported modes run through `App3D`;
- `Demo3D` is no longer default;
- baseline and reference gates pass;
- no runtime feature silently falls back to old global state.

### Phase 10: Retire broken surface architecture

**Goal:** Delete code whose replacement is proven.

Delete or archive from the active build:

- old `SurfaceRC` direct-atlas production path;
- generic surface cascade downsample/upsample passes;
- same-texel feedback modes presented as GI;
- Sponza proxy radiance floor and visibility scaling;
- obsolete phase-numbered runtime controls and debug modes;
- direct-atlas substitution for C0;
- duplicate chart classification implementations.

Exit:

- no active shader or C++ path references retired resource meanings;
- G0-G10 still pass;
- legacy volumetric/PT/hybrid retirement is decided separately, not bundled into surface parity.

### Phase 11: Generalization design, not automatic porting

**Goal:** Decide how to move from hardcoded Cornell charts to real mesh surfaces.

This phase starts a new design cycle. Candidate approaches include:

- authored UV2 charts;
- meshlet or triangle charts;
- surfel pages;
- virtual-textured surface caches.

Sponza is not accepted through AABB planes or box proxies under the name of general surface RC. It requires geometry-linked surface identity, material identity, and stable world-to-chart mapping.

Generalization must preserve the proven kernel contracts while replacing only the chart provider and tracer integration.

---

## 12. Legacy Freeze Policy

During Phases 0-8:

Allowed legacy changes:

- compile fixes;
- crash fixes;
- deterministic baseline fixes;
- adapter work required by the new shell;
- validation instrumentation that does not alter output.

Forbidden legacy changes:

- new GI heuristics;
- scene-specific tuning to improve parity metrics;
- new hybrid compensation layers;
- reuse of legacy alpha semantics in the reference path;
- claiming parity from legacy quality output.

This keeps comparison paths useful while preventing two moving targets.

---

## 13. Retirement Criteria

### 13.1 Old `SurfaceRC`

May be removed after:

- G0-G10 pass through `App3D`;
- Cornell reference mode is the only path named surface RC;
- required diagnostics and captures have replacements;
- no CLI or test depends on old texture IDs or debug mode numbers.

### 13.2 `Demo3D`

May be removed after:

- scene, resource, render, capture, validation, UI, and CLI behavior run through new systems;
- the legacy backends have direct adapters not depending on `Demo3D` ownership;
- startup and shutdown pass leak/resource checks;
- baseline commands use `App3D` by default.

### 13.3 Volumetric, hybrid, and PT paths

- PT remains as validation infrastructure unless a replacement is approved.
- volumetric and hybrid paths are independent algorithms and are not deleted merely because Cornell surface parity passes.
- their product status should be decided using separate quality, performance, and maintenance criteria.

---

## 14. Risks and Mitigations

### Risk: App-shell rebuild expands scope indefinitely

Mitigation:

- migrate vertical slices;
- keep each phase runnable;
- do not redesign scene formats or UI styling during parity work;
- limit new abstractions to explicit ownership and contracts.

### Risk: The reference scene and native Cornell scene differ

Mitigation:

- create a locked Cornell reference-scene provider;
- record transforms and dimensions;
- test expected chart points independently;
- treat native OBJ generalization as a later provider.

### Risk: ShaderToy code contains approximations or temporary fixes

Mitigation:

- preserve semantics for parity first;
- mark known approximations, such as flatland visibility and `TMP fix` NaN guard;
- improve them only in named post-parity modes with independent derivation.

### Risk: GPU tests become vendor-sensitive

Mitigation:

- compare with tolerances;
- prefer discrete indices and counters where possible;
- record GPU/driver metadata;
- maintain CPU oracles for layout and merge math.

### Risk: Temporal tests are nondeterministic

Mitigation:

- fixed seed and fixed frame count;
- no camera-dependent update in parity mode;
- explicit atlas clear and revision control;
- headless validation mode with no UI mutation.

### Risk: Dirty working-tree diagnostics are lost

Mitigation:

- inventory them in Phase 0;
- preserve useful alpha-contract metrics as requirements;
- do not revert user changes;
- port only behavior supported by the locked contract.

---

## 15. Deliverables

The refactor is complete only when the repository contains:

- a new `App3D`-based runtime path;
- explicit scene, resource, GI, rendering, validation, capture, and UI boundaries;
- a six-cascade Cornell reference kernel;
- a versioned atlas payload contract;
- CPU oracles for chart, layout, direction, interval, and merge math;
- executable G0-G10 validation;
- deterministic JSON and capture artifacts;
- updated README and runtime commands;
- removal of the broken old surface path after gates pass;
- a separate approved design before mesh-driven Sponza generalization.

---

## 16. Immediate Next Implementation Step

Begin Phase 0 only.

The first implementation change should not be another radiance shader patch. It should establish source/runtime shader integrity, baseline metadata, reliable validation exit status, and explicit backend naming. That creates the evidence boundary needed for every subsequent step.

After Phase 0 passes, add only the minimal `App3D` coordinator, an opaque `Demo3DBackend`, and the resource contracts required by the reference slice. Then implement the locked parity scene and reference kernel. Broad migration of PT, hybrid, volumetric, capture, UI, and legacy scene ownership waits until the Cornell semantic milestone. This preserves both approved goals: parity kernel first and a strangler rebuild of the full app shell.

---

## 17. Final Status Statement

The current 3D project is a valuable research harness, not a correct 3D radiance-cascade implementation. It contains useful scene, SDF, PT, capture, and debug infrastructure, but the active GI code mixes several incompatible algorithms and payload meanings.

The approved recovery path is:

```text
freeze legacy behavior
-> add a minimal strangler coordinator and opaque legacy adapter
-> lock the analytic reference scene and resource contracts
-> implement exact Cornell ShaderToy semantics
-> pass machine semantic gates
-> complete and cut over the rebuilt app shell
-> retire broken surface code
-> design real mesh charting separately
```

This plan prioritizes falsifiable transport correctness over additional quality tuning.
