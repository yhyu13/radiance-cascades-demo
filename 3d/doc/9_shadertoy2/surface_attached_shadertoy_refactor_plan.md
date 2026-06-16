# v5 / ShaderToy2 — Surface-Attached Radiance Cascades Refactor Plan

**Date:** 2026-05-28  
**Status:** Draft plan for implementation — amended after Critique 01  
**Direction:** Total refactor toward ShaderToy surface-attached topology, staged to avoid breaking the current volumetric path  
**Supersedes decision:** `doc/8_shadertoy/v4_closeout_report.md` deferred Path B; user has now selected Path B as the direction. Critique 01 amendments are binding before Phase 2 work begins.

---

## 0. Executive Decision

We will move toward the ShaderToy reference topology:

```text
surface-attached probes + hemisphere sampling + surface-space cascade merge
```

The previous volumetric path is not the target architecture for Cornell-class point-lit enclosed scenes. It remains as a fallback during the refactor, but the new implementation should converge quickly toward the ShaderToy model instead of spending more effort tuning the volumetric cascade.

Primary goal:

```text
Retire the Cornell point-light under-emit by replacing free-volume probe sampling with surface-attached cascade radiance.
```

Secondary goal:

```text
Move fast, but preserve a working renderer at every checkpoint.
```

---

## 1. Why This Refactor Exists

The v4 program proved the key constraint:

| Scene/light class | Current volumetric cascade |
|---|---|
| Sponza/open/broad lighting | Works after gain tuning |
| Cornell directional light | Works well, ratio ~0.93 |
| Cornell point light | Under-emits, ratio ~0.49 |
| Cornell point light + hybrid | Usable, ratio ~0.83 |

The failure is not a shader typo. It is a topology mismatch.

Current engine:

```text
3D grid probe in free space
  casts uniform S² rays
  hits whatever surface happens to be along each ray
  averages bright and dim surface patches probabilistically
```

ShaderToy:

```text
probe lives on a surface
  casts hemisphere rays above that surface normal
  directly captures that exact surface point's outgoing radiance
  merges in surface parameter space
```

For point-lit Cornell, the bright floor patch is localized. A volumetric probe only sees it by chance. A surface-attached probe on that floor patch captures it by construction.

Therefore, copying more ShaderToy fragments into `radiance_3d.comp` is the wrong path. The correct path is to create a new surface-attached pipeline.

---

## 2. Self-Critique of the First Plan

The initial plan was safe, but too slow and too generic. Specific issues:

### SC1 — Too much infrastructure before matching ShaderToy

The first plan proposed several generic abstractions before producing a ShaderToy-like image. That delays the key proof.

**Correction:** Start with a hardcoded Cornell surface atlas that mirrors ShaderToy's hardcoded chart logic. General mesh support is explicitly deferred.

### SC2 — Too much emphasis on a reusable CPU data model

A clean `SurfaceChart` C++ abstraction is useful, but not the fastest route to validating the algorithm.

**Correction:** Use a small fixed Cornell chart table first. The GPU shader can decode chart ID from atlas coordinates like ShaderToy's `CubeA.glsl` lines 68-121. C++ only allocates textures and passes constants.

### SC3 — Final render lookup was left too vague

The hard problem is hit point -> surface atlas coordinate. Leaving this late risks building a bake that cannot be consumed precisely.

**Correction:** For Cornell, implement analytic hit-to-chart mapping early. This should happen before multi-cascade work.

### SC4 — Direct lighting alone is not enough

A surface direct atlas proves chart mapping, but not radiance cascade behavior.

**Correction:** Use direct atlas as a short diagnostic phase only. Move quickly to one-bounce hemisphere integration and final render consumption.

### SC5 — The plan preserved volumetric updates too long

Keeping both paths always active is safe but wastes performance and hides integration problems.

**Correction:** As soon as surface mode can produce a debug GI image, allow `--use-surface-rc=1` to bypass `updateRadianceCascades()` for Cornell. The old path remains available by flag, not always running.

### SC6 — It under-specified ShaderToy equivalence

The first plan described surface cascades generally, but the implementation should intentionally match ShaderToy concepts:

- hardcoded charts first,
- surface-local tangent/bitangent/normal,
- `probeSize = 2^(cascade + 1)`,
- `probePositions = gRes / probeSize`,
- hemisphere ring/square direction mapping,
- interval grows with probe size,
- weighted bilinear upper merge.

**Correction:** Treat `shader_toy/CubeA.glsl` as the algorithm spec for Phase 2 onward.

### SC7 — It deferred validation metrics too late

We need metrics immediately after the surface path can produce final GI.

**Correction:** Add a Cornell point-light ratio gate starting at the first final-consumable surface GI phase.

---

## 3. Faster Strategy

Do not build a general surface system first.

Build this order:

```text
Cornell hardcoded surface atlas
  -> ShaderToy ring-packed C0 surface bake
  -> point-light NEE at probe-ray hit points
  -> persistent ping-pong self-feedback for recursive bounce closure
  -> final render samples surface atlas
  -> one-bounce GI ratio beats volumetric
  -> add ShaderToy chart-local cascade hierarchy + weighted merge
  -> only then generalize
```

This gets to the essential test quickly:

```text
Can surface-attached RC beat 0.49 and approach/beat hybrid 0.83 on Cornell point light?
```

---

## 4. Non-Negotiable Safety Rules

1. Current volumetric renderer remains default until surface RC passes Cornell gates.
2. New path is behind a flag:

```text
--use-surface-rc=1
```

3. New shaders are separate files. Do not rewrite `radiance_3d.comp` for the first implementation wave.
4. Cornell-only is acceptable and preferred at first **only because Sponza remains on the unchanged volumetric path**. The v3 Sponza-first-class lock still applies to any hybrid-retirement or production-default claim.
5. If surface RC is enabled on unsupported scenes, log a warning and fall back or render diagnostic magenta/black intentionally.
6. Every phase must have a debug output that can be visually inspected.
7. Delete nothing from hybrid until surface RC clears the locked retirement bar: `|p95| <= 0.50` on both Cornell and Sponza-class baselines, with EXR-backed evidence.

---

## 5. Target Pipeline

### Current volumetric path

```text
SDF/albedo volume
  -> radiance_3d.comp
  -> 3D probe atlas
  -> reduction_3d.comp
  -> raymarch.frag samples volume GI
```

### New surface-attached path

```text
Cornell chart atlas
  -> surface_radiance.comp
  -> ShaderToy ring-packed persistent cascade-band atlas
  -> chart-local weighted upper merge
  -> raymarch.frag maps visible hit to chart UV
  -> samples surface GI
```

Target steady-state:

```text
if useSurfaceRC && supportedScene:
    updateSurfaceRadianceCascades()
    raymarch uses surface GI
else:
    updateRadianceCascades()
    raymarch uses volumetric GI
```

---

## 5.1 Critique 01 Binding Amendments

Critique 01 identified three critical spec drifts and two process-contract conflicts. The following decisions are now part of this plan:

### A1 — Atlas layout follows ShaderToy first

The first surface RC implementation uses the ShaderToy **ring-packed cascade-band layout**, not a `surfaceAtlasWidth * dirRes` outer-product layout.

For each chart/cascade band:

```text
gRes = chart resolution in atlas texels
probeSize = 2^(cascade + 1)
probePositions = gRes / probeSize
within each chart rectangle:
  modUV / probePositions selects the direction coordinate within the probe tile
  mod(modUV, probePositions) selects the surface probe coordinate
```

This preserves the ShaderToy invariant:

```text
direction resolution is coupled to probeSize and cascade index
```

The separate `dirRes = 8` texture layout is rejected for the first implementation wave. It may return later only as a deliberate divergence with a new derivation.

### A2 — Surface atlas is persistent

The surface atlas is a persistent ping-pong buffer sampled from the previous frame, matching ShaderToy's `iChannel3` self-feedback model.

Required behavior:

```text
surfaceAtlasPrev -> surface_radiance.comp -> surfaceAtlasNext
swap(prev, next)
```

At probe-ray surface hits, the shader must sample previous-frame surface radiance at the hit chart/UV before adding current direct lighting. This is the recursive multi-bounce closure that the volumetric path lacked.

### A3 — Point-light next-event estimation is mandatory

The Cornell point light is not a chart. Probe rays must not rely on randomly hitting a tiny emissive object.

At every probe-ray surface hit:

```text
lightDir = normalize(lightPos - hitPos)
visibility = shadowTrace(hitPos + hitNormal*bias, lightPos)
Li_direct = lightColor * max(dot(hitNormal, lightDir), 0) * visibility / distance^2
```

This is the point-light generalization of ShaderToy's sun-light block in `CubeA.glsl:172-177`.

### A4 — Sponza lock remains active

Surface RC may be Cornell-only during proof-of-topology because Sponza continues to use the unchanged volumetric path. Any claim that hybrid can be retired, or that surface RC is the new production radiance path, must include Sponza EXR evidence.

### A5 — Retirement bar remains strict

Cornell `ratio >= 0.90` is a progress target, not a retirement criterion. The locked retirement criterion remains:

```text
|p95| <= 0.50 on both Cornell and Sponza-class baselines
```

No "acceptable visuals" escape hatch is allowed for retirement claims.

---

## 6. ShaderToy Concepts to Preserve

From `shader_toy/CubeA.glsl`:

### 6.1 Hardcoded surface charts

ShaderToy determines `gTan`, `gBit`, `gNor`, `gPos`, and `gRes` from atlas coordinates.

We should do the same first.

Initial Cornell atlas charts:

| Chart | Normal | Suggested res | Notes |
|---|---:|---:|---|
| Floor | +Y | 256x256 | Most important point-light bounce source |
| Ceiling | -Y | 256x256 | Contains/near light source |
| Left wall | +X | 128x256 | Red wall |
| Right wall | -X | 128x256 | Green wall |
| Back wall | +Z or -Z depending scene convention | 128x256 | White wall |
| Optional front/open wall | opposite Z | 128x256 | Only if scene has it |

First atlas may directly mirror ShaderToy dimensions:

```text
base surface atlas: approximately 1024 x 256 for primary room charts
additional rows for interior walls/objects later
```

### 6.2 Surface-local probe placement

ShaderToy:

```glsl
probeCascade = floor(mod(UV.y, 1536.) / 256.);
probeSize = pow(2., probeCascade + 1.);
probePositions = gRes / probeSize;
probePos = gPos
         + mod(modUV.x, probePositions.x) * probeSize / 256. * gTan
         + mod(modUV.y, probePositions.y) * probeSize / 256. * gBit;
```

Our first implementation should use the same model conceptually:

```text
surface probe spacing grows by powers of two per cascade
probe lives on chart plane
probe normal is chart normal
```

### 6.3 Hemisphere direction distribution

ShaderToy generates directions from the probe tile coordinate and transforms them by chart TBN:

```glsl
probeDir = localHemisphereDir.x * gTan
         + localHemisphereDir.y * gBit
         + localHemisphereDir.z * gNor;
```

Use this ring/square hemisphere mapping before considering octahedral/S² bins. The old volumetric atlas uses full-sphere octahedral directions; surface RC should use ShaderToy's hemisphere packing first.

### 6.4 Interval scaling

ShaderToy:

```glsl
tInterval = (1. / 64.) * probeSize * 2.;
```

Our Cornell dimensions differ, but the rule should remain:

```text
interval grows with surface probeSize
```

Derive the interval from chart world size and chart resolution before tuning. ShaderToy's unit chart has pixel size `1/256` and uses:

```glsl
float tInterval = (1.0 / 64.0) * probeSize * 2.0;
```

Equivalently, for a chart with world extent `chartWorldSize` and texel resolution `chartRes`, compute the chart pixel size and choose the Cornell constant so C0 matches ShaderToy's interval/probe-spacing ratio before any empirical tuning.

### 6.5 Merge upper cascade in surface space

ShaderToy blends upper cascade samples by surface-local bilinear coordinates, not 3D trilinear volume coordinates.

This is the main architectural fix.

---

## 7. Implementation Phases

## Phase 0 — Flag and Empty Surface Path

**Goal:** Add the switch without changing default output.

### Work

- Add `bool useSurfaceRC = false` to `Demo3D`.
- Add CLI:

```text
--use-surface-rc=0|1
```

- Add UI/debug text:

```text
Radiance topology: Volumetric / Surface Attached Experimental
```

- Add startup log:

```text
[SurfaceRC] disabled
[SurfaceRC] enabled: Cornell-only experimental path
```

### Acceptance

- Default render unchanged.
- Enabling flag does not crash.
- Unsupported scene logs a clear warning.

### Fail action

Revert only the flag plumbing.

---

## Phase 1 — Hardcoded Cornell Surface Atlas Debug

**Goal:** Produce a ShaderToy-like hardcoded chart atlas debug view quickly.

### Work

Add:

```text
res/shaders/surface_cornell_debug.comp
```

It writes a 2D texture:

```text
RGB = normal * 0.5 + 0.5 or chart color
A   = valid chart mask
```

C++ allocates:

```text
surfaceDebugTex RGBA16F or RGBA8
surfaceAtlasWidth
surfaceAtlasHeight
```

No final render integration yet.

### Debug modes

Add a surface atlas viewer or reuse existing debug quad logic.

Modes:

```text
0 chart ID color
1 normal
2 world position
3 albedo
4 valid mask
```

### Acceptance

- Floor, ceiling, and walls appear in expected atlas regions.
- Normals are correct.
- Albedo matches Cornell wall colors.
- Chart extents are numerically bound to the actual Cornell scene world bounds; no guessed 256x256 chart is accepted without world-min/world-max verification.
- No impact on volumetric rendering.

### Fail action

Fix chart decode before any lighting work.

---

## Phase 2 — ShaderToy-Style C0 Surface Radiance Bake

**Goal:** Generate first useful surface-attached directional radiance atlas for C0.

### Work

Add:

```text
res/shaders/surface_radiance.comp
```

For each ring-packed chart/cascade-band texel:

```text
1. Decode chart, cascade, probe coordinate, and ring direction coordinate exactly as ShaderToy does.
2. Compute world position, normal, tangent, bitangent.
3. Compute hemisphere direction from the ring-packed direction coordinate.
4. Trace from surface position + normal bias for this cascade interval.
5. If hit surface: classify hit chart/UV, sample previous-frame surface atlas at the hit, and evaluate point-light NEE at the hit.
6. If miss: write sky/env if enabled.
7. Store RGB radiance and A hit distance / miss code for future WeightedSample.
```

Initial output layout:

```text
surfaceAtlas width  = packed chart width
surfaceAtlas height = packed chart height * cascadeCount or ShaderToy-equivalent cascade bands
```

Do not introduce a separate `dirRes` in the first implementation. Direction count is implied by `probeSize` and the ShaderToy ring layout.

### Precision requirements

- Hemisphere only; no below-surface directions.
- Apply ShaderToy cosine/solid-angle normalization deliberately.
- Use point-light NEE at probe-ray hit points; do not rely on stochastic light hits.
- Use previous-frame atlas sampling for recursive closure.
- Store hit distance in alpha for future WeightedSample.

### Acceptance

- C0 atlas has nonzero radiance.
- Floor center bins see strong point-light contribution indirectly/directly as expected.
- No NaN/Inf.
- Debug view can inspect individual direction bins.

### Fail action

Do not add final render lookup until C0 atlas is sane.

---

## Phase 2.5 — Persistent Self-Feedback Closure

**Goal:** Match ShaderToy's recursive buffer semantics before judging energy.

### Work

- Allocate previous/current surface atlas textures.
- In `surface_radiance.comp`, sample previous-frame surface atlas at probe-ray hit chart/UV.
- Add current-frame direct lighting from point-light NEE.
- Ping-pong atlas textures after dispatch.
- Add a convergence rule for captures: surface RC metrics are reported after a fixed warm-up frame count or convergence threshold, not from frame 1.

### Acceptance

- Frame 1 is direct/first-bounce only.
- Later frames monotonically approach a stable multi-bounce field without runaway brightness.
- Cornell floor-center luminance increases after feedback is enabled.

### Fail action

If feedback explodes or collapses, inspect normalization and albedo placement before adding cascades.

---

## Phase 3 — Final Render Consumes Surface C0

**Goal:** Get to an image and metrics fast.

### Work

Modify `raymarch.frag` behind a flag:

```glsl
uniform int uUseSurfaceRC;
uniform sampler2D uSurfaceC0Atlas;
uniform int uSurfaceAtlasWidth;
uniform int uSurfaceAtlasHeight;
uniform int uSurfaceCascadeCount;
```

For Cornell mode only:

```text
visible hit position + normal
  -> classify chart analytically
  -> compute surface UV
  -> sample/integrate C0 surface hemisphere atlas
  -> use as indirect GI
```

Add render modes:

```text
20 Surface RC GI only
21 Surface RC final composite
```

### Chart classification

Use analytic plane tests:

```text
near floor plane    -> floor chart
near ceiling plane  -> ceiling chart
near left wall      -> left wall
near right wall     -> right wall
near back wall      -> back wall
```

Do not solve arbitrary mesh lookup in this phase.

### Acceptance

- Cornell image renders using surface GI.
- Volumetric path still works when flag is off.
- Surface GI-only capture exists.

### First metric gate

Compare Cornell point-light:

```text
surface_c0_gi / pt_gi > 0.60
```

This is a progress gate only. Capture after the Phase 2.5 warm-up rule, because persistent feedback makes frame-1 metrics meaningless.

### Fail action

Likely causes:

1. chart mapping wrong,
2. hemisphere normalization wrong,
3. final lookup chart mismatch,
4. direct-light scale mismatch,
5. shadow bias too strong.

Fix diagnostics before adding cascades.

---

## Phase 4 — One-Bounce Surface RC Quality Pass

**Goal:** Make C0 surface result competitive with hybrid before adding full cascade complexity.

### Work

Improve `surface_radiance.comp`:

- Better hit normal estimation.
- Better direct-light evaluation at ray hit.
- Match Cornell material albedo with path tracer.
- Match point light attenuation/intensity with existing shaders.
- Correct hemisphere integral:

```text
binWeight = (cos(theta - deltaTheta) - cos(theta + deltaTheta)) / binsInRing
weightedRadiance = Li * binWeight * cos(theta)
```

Albedo placement must match ShaderToy:

```text
hit outgoing radiance is multiplied by hit albedo at the hit branch
the ring weight and cosine are applied to the bin output
do not add a second /π unless the consumer is changed to expect unnormalized irradiance
```

### Acceptance

Cornell point-light:

```text
surface_c0_gi / pt_gi >= 0.80
```

This should approach hybrid (`~0.83`) before adding upper cascades.

### Fail action

Add focused diagnostics:

- direct-only surface atlas vs PT direct,
- unshadowed direct atlas,
- normal visualization at hit points,
- per-chart luminance means,
- floor-center luminance probe print/readback.

---

## Phase 5 — Add Surface Cascade Levels C1..Cn

**Goal:** Implement actual ShaderToy-like surface cascades.

### Work

Add textures:

```text
surfaceCascadeAtlas[MAX_CASCADES]
surfaceCascadeHistory[MAX_CASCADES] optional later
```

For cascade `c`:

```text
probeSize = 2^(c + 1)
probe grid density = chart resolution / probeSize
interval = baseInterval * probeSize
```

Dispatch coarse-to-fine or match ShaderToy's buffer update semantics. For this engine, prefer:

```text
coarse -> fine
```

because lower levels need upper levels already available for misses/merge.

### Merge rule

Implement ShaderToy chart-local upper merge. Simple bilinear may exist as a debug fallback, but the production candidate is weighted merge in the same phase.

```text
if ray reaches interval end or needs far field:
    sample upper cascade within the same chart band at corresponding surface UV and direction
    apply chart-local WeightedSample visibility
```

Do not use volume trilinear. Do not cross charts in the first implementation.

### Acceptance

- Enabling C1 does not darken C0 result.
- Enabling C2 smooths or extends indirect light.
- Cornell ratio stays >= Phase 4 result.

### Metric gate

```text
surface_cascade_gi / pt_gi >= 0.85
```

---

## Phase 6 — Port ShaderToy WeightedSample in Correct Topology

**Goal:** Validate and harden the chart-local WeightedSample path added in Phase 5.

### Work

The first version is chart-local only, matching ShaderToy:

```text
upperProbePos = chartToWorld(same chart, upper UV)
currentProbePos = chartToWorld(same chart, current UV)
relVec = currentProbePos - upperProbePos
lookBackPhi = atan/project relVec in chart tangent/bitangent basis
lookBackBin = ShaderToy ring coordinate
upperHitDist = upperAtlas[upperProbe, lookBackBin].a
visible = upperHitDist < sky_code || length(relVec) < upperHitDist * cone + bias
```

Cross-chart WeightedSample is explicitly out of scope until the chart-local path passes metrics.

### Acceptance

- WeightedSample reduces leaks/seams.
- It does not kill Cornell floor energy.
- Ratio does not regress by more than 5% from Phase 5.

### Metric gate

```text
surface_weighted_gi / pt_gi >= 0.85
```

Target:

```text
>= 0.90
```

---

## Phase 7 — Temporal Accumulation and Stability

**Goal:** Add stability only after correctness. This is separate from the persistent ping-pong atlas required by Phase 2.5.

### Work

- Add optional EMA for surface atlases after recursive feedback is correct.
- Add history clamp if necessary.
- Add jitter only if aliasing is visible.

### Acceptance

- Static camera converges.
- Camera reset does not ghost badly.
- No temporal path is required for first correctness proof.

### Rule

Do not use temporal accumulation to hide wrong energy. Correct static/no-temporal output first.

---

## Phase 8 — Replace Cornell Default Path

**Goal:** Use surface RC for Cornell point-light by default when stable.

### Preconditions

- Cornell point-light surface RC meets progress target (`ratio >= 0.90`) and retirement target (`|p95| <= 0.50`) on EXR metrics.
- Sponza remains unchanged on the volumetric path, or a surface/generalized path also clears `|p95| <= 0.50`; otherwise no hybrid-retirement claim is allowed.
- No significant leaks/regressions in standard Cornell camera captures.
- Volumetric path still available by flag.

### Work

Default selection:

```text
if scene == Cornell && useSurfaceRCAuto:
    use surface RC
else:
    use volumetric RC
```

Keep explicit override:

```text
--use-surface-rc=0|1
```

Do not delete hybrid yet.

---

## Phase 9 — Generalization After Cornell Success

**Goal:** Move beyond hardcoded Cornell only after proving the topology.

Options:

### Option A — Mesh UV charts

Pros:
- natural chart lookup,
- matches raster material systems.

Cons:
- overlapping/repeated UVs,
- OBJ material/texture complexity,
- Sponza may not have unique lightmap UVs.

### Option B — Surfel atlas

Pros:
- works for arbitrary triangle meshes,
- no UV unwrap required,
- closer to probe/surface sampling needs.

Cons:
- visible hit -> nearest surfel lookup requires spatial data structure,
- atlas filtering is harder.

### Recommendation

Use a hybrid progression:

```text
Cornell analytic charts first
simple OBJ planar/box charts second
surfel atlas third
full mesh UV/lightmap charts only if needed
```

Do not let Sponza generalization slow the Cornell proof.

---

## 8. File Plan

### New files first wave

```text
src/surface_rc.h
src/surface_rc.cpp
res/shaders/surface_cornell_debug.comp
res/shaders/surface_radiance.comp
```

### Possible later files

```text
res/shaders/surface_reduce.comp
res/shaders/surface_common.glsl
res/shaders/surface_debug.frag
```

### Existing files touched

```text
src/demo3d.h          // flag/member + SurfaceRC ownership
src/demo3d.cpp        // lifecycle + render integration
src/main3d.cpp        // CLI flag
res/shaders/raymarch.frag // surface GI sampling path behind uniform
CMakeLists.txt        // if new .cpp added
```

### Files to avoid changing early

```text
res/shaders/radiance_3d.comp
res/shaders/reduction_3d.comp
res/shaders/hybrid_correction.comp
```

---

## 9. Acceptance Gates Summary

| Phase | Gate | Pass condition | Fail action |
|---|---|---|---|
| 0 | No-change default | Default output unchanged | Revert flag plumbing |
| 1 | Chart debug | Cornell charts/normals/albedo correct | Fix chart decode only |
| 2 | C0 atlas sane | Nonzero directional radiance, no NaN/Inf | Fix bake before final integration |
| 2.5 | Self-feedback | Ping-pong atlas converges without runaway brightness | Fix normalization/albedo placement |
| 3 | First image | Surface GI final render works after warm-up | Debug hit->chart mapping |
| 3 | First metric | Cornell point surface/PT GI ratio > 0.60 after warm-up | Diagnose mapping/normalization |
| 4 | Hybrid parity | Ratio >= 0.80 | Add direct/normal/scale diagnostics |
| 5 | Cascade safe | C1/C2 do not darken C0 | Fix merge formula |
| 5 | Cascade metric | Ratio >= 0.85 | Keep C0 path, debug upper merge |
| 6 | Weighted merge | No >5% regression | Disable weighted merge by default |
| 8 | Cornell default | Ratio target >= 0.90 and Cornell `|p95| <= 0.50` | Keep opt-in only |
| 8+ | Hybrid retirement | `|p95| <= 0.50` on both Cornell and Sponza-class baselines | Hybrid remains available/default fallback |

---

## 10. Diagnostics to Build Early

Required shader/debug views:

```text
Surface chart ID
Surface normal
Surface albedo
Surface world position
Surface C0 radiance per selected bin
Surface C0 integrated GI
Hit-point chart classification
PT GI delta heatmap against surface GI
```

Required logs/readbacks:

```text
per-chart mean luminance
floor-center luminance
floor-edge luminance
valid texel count
NaN/Inf count
surface/PT ratio summary
```

These diagnostics are not optional. They prevent repeating the v2/v3 pattern of tuning blind.

Implementation estimate discipline:

```text
shader debug views: expected in the same phase as the feature they inspect
CPU readbacks/JSON metrics: expected before any metric gate can be claimed
```

### 10.1 Measurement Protocol

EXR-backed measurement is mandatory for every metric gate.

Initial protocol:

```text
capture target: Cornell point-light cam0
reference: PT full and PT direct EXR at N=2048 unless a new lock says otherwise
surface outputs: surface_gi, final_composite, optional direct-only
metric JSON: doc/9_shadertoy2/baseline_lock_surface_rc.json
```

The exact script may reuse the existing v3/v4 capture/analyzer chain, but the first implementation slice that claims a ratio must also add or document the command that produced the EXR and JSON. No LDR-only verdicts.

### 10.2 Stop-Loss Policy

Each implementation phase has a hard stop-loss:

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if still failing: stop feature work, update this plan/reply with the new diagnosis
```

This prevents repeating the v2.x pattern of long knob sweeps after a structural mismatch is already visible.

---

## 11. Most Likely Failure Sources

Before applying broad fixes, check these in order:

1. **Hit-to-chart mapping wrong** — final render samples the wrong atlas texel.
2. **Hemisphere normalization wrong** — energy too bright/dim globally.
3. **Normal orientation wrong** — rays cast into walls or below floor.
4. **Light scale mismatch** — surface shader direct term differs from PT/raymarch.
5. **Shadow bias wrong** — self-shadowing kills floor energy.
6. **Atlas chart seam/clamp bug** — bilinear crosses unrelated charts.
7. **Upper merge wrong** — C1/C2 import dim or unrelated radiance.

Distill expected top two early bugs:

```text
hit-to-chart mapping
hemisphere normalization
```

Add diagnostics for those before changing algorithms.

---

## 12. Implementation Commandments

1. Match ShaderToy first; generalize later.
2. Hardcode Cornell first; abstract later.
3. One new topology path; do not mutate volumetric path as a shortcut.
4. Every phase must produce a visible/debuggable artifact.
5. No broad refactors inside `Demo3D` until surface RC correctness is proven.
6. No deletion of hybrid until surface RC beats it.
7. No Sponza/general mesh surface-RC work until Cornell point-light gate passes; however Sponza volumetric baselines remain binding for any retirement/default claim.
8. Prefer exact chart math over clever sampling.
9. Keep `radiance_3d.comp` stable until Phase 8+.
10. If a change cannot be measured, do not make it part of the core path.

---

## 13. First Coding Slice

The first implementation slice should include only:

```text
Phase 0 + Phase 1
```

Concrete tasks:

1. Add `SurfaceRC` class skeleton.
2. Add `--use-surface-rc` CLI flag.
3. Allocate one 2D surface debug atlas.
4. Implement `surface_cornell_debug.comp`.
5. Dispatch it for Cornell when surface RC is enabled.
6. Add atlas debug view/readback log.
7. Do not affect final mode 0 rendering.

Expected output:

```text
[SurfaceRC] enabled: Cornell hardcoded charts
[SurfaceRC] debug atlas 1024x512, valid charts=5
```

Visible result:

```text
A debug atlas showing Cornell floor/ceiling/walls with correct colors/normals.
```

This slice is small, safe, and directly moves toward the ShaderToy topology.

---

## 14. Cross-References

Previous program:

- `doc/8_shadertoy/v4_shadertoy_adoption_scope.md`
- `doc/8_shadertoy/v4_closeout_report.md`
- `doc/8_shadertoy/cornell_point_light_constraint.md`

Reference implementation:

- `shader_toy/CubeA.glsl`
- `shader_toy/Common.glsl`

Current volumetric implementation to keep stable:

- `res/shaders/radiance_3d.comp`
- `res/shaders/reduction_3d.comp`
- `src/demo3d.cpp`

---

## 15. Final Desired Outcome

The end state is not merely a new debug mode. The end state is:

```text
Cornell point-light uses surface-attached radiance cascades after EXR-backed gates pass.
Hybrid correction becomes optional/fallback for Cornell only after Cornell gates pass; full hybrid retirement requires the locked both-scene `|p95| <= 0.50` bar.
Volumetric cascade remains available for Sponza/open scenes until surface generalization catches up.
The implementation follows ShaderToy topology closely enough that future ShaderToy improvements can be ported structurally, not as mismatched fragments.
```
