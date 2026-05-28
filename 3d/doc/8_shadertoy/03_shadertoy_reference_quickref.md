# ShaderToy RC Reference — Quick-Reference for Porting

**Purpose:** Single-page reference for the ShaderToy 3D Radiance Cascades reference implementation, with cross-references to the current volumetric implementation.

---

## 1. Architecture Summary

The ShaderToy reference (`shader_toy/`) implements surface-attached radiance cascades:

```
Image.glsl → CubeA.glsl (duplicate result pass)
                ↓
         mainCubemap() — single-pass: both bakes AND merges in one shader
                ↓
         For each probe bin:
           1. Compute probeDir from bin center (octahedral coords → sphere)
           2. TraceRay(probePos, probeDir) → HIT record
           3. If miss (sky): store sky color, mark w=-1
           4. If surface hit: fetch 4-tap bounce light from previous cubemap
           5. Pre-weight: L · cos(θ) · ΔΩ (cosine + solid angle)
           6. If cascade < 4.5: merge with upper cascade via 4-corner bilinear
              WeightedSample for per-corner visibility gating
```

**Key structures:**
- `HIT { t, uv, uvo, res, n, c }` — hit record from trace ray
- `TraceRay(p, d, maxt, time)` — single-ray trace against Cornell box
- `TextureCube(uv[, lod])` — reads cubemap texture array (atlas)
- `WeightedSample(luvo, luvd, luvp, uvo, probePos, ...)` — per-corner visibility gate

---

## 2. File-by-File Inventory

### `shader_toy/Common.glsl` (232 lines)

| Section | Lines | Purpose |
|---------|-------|---------|
| Constants | 1-5 | ICYCLETIME, I256, I512, I1024 |
| SDF functions | 19-50 | Rotate2, Repeat2, DFBox (2D+3D) |
| Geometry | 52-73 | InteriorIntersection, DFIntersection |
| Ray tracing | 75-171 | AQuad, ABox, ABoxNormal, ASphere, ACylZ, TraceRay |
| BRDF/Math | 173-232 | TBN, BRDF_GGX |

**TraceRay outputs:**
- `uvt.z` = hit distance (or -1 for no hit)
- `uvt.xy` = 2D hit position on wall (0-1 range, wall-local coords)
- `.uvo/res` = wall atlas offset and resolution (for cubemap lookup)
- `.n` = world-space normal (or vec3(-20) for miss)
- `.c` = base color (or vec3(-2) for specular, vec3(-1) for miss)

### `shader_toy/CubeA.glsl` (240 lines) — THE REFERENCE

| Section | Lines | Purpose |
|---------|-------|---------|
| TextureCube | 3-12 | Cubemap fetch from 6-face 2D atlas |
| WeightedSample | 14-36 | Per-corner visibility check for bilinear merge |
| mainCubemap | 38-240 | **Entire cascade bake + merge in one function** |

**mainCubemap workflow (annotated):**

```
1. Load self from previous frame (line 39)
2. Convert rayDir → atlas UV (lines 40-52) — octant-to-face mapping
3. Set gTan/gBit/gNor/gPos for current probe's wall (lines 63-103)
   — 7 walls: floor, ceiling, 6 walls (including interior walls)
   — Each wall has its own UV offset (uvo) and resolution (res)
4. Compute probe position from atlas UV + grid (lines 131-141)
   — probePos = gPos + mod(modUV.x, probePositions.x)*probeSize/256*gTan + ...
   — probeUV = floor(modUV / probePositions) + 0.5
5. Compute spherical bin center direction (lines 141-148)
   — probeTheta from probe axis (octahedral-like azimuthal mapping)
   — probePhi from probe boundary (different from octahedral!)
   — probeDir = gTan*sin(phi)*sin(theta) + gBit*cos(phi)*sin(theta) + gNor*cos(theta)
6. TraceRay to get hit record (line 151)
7. If surface hit with color (c.x > 1.0): add direct light (lines 162-174)
   — Fetch 4-tap bounce from cubemap (TextureCube 4× offset)
   — Add sun direct if visible
   — Multiply by surface color
8. If sky/miss: store sky color (lines 180-181)
9. Pre-weight with cosine + ΔΩ (lines 183-192)
   — cos(theta-Δθ/2) - cos(theta+Δθ/2) = ΔΩ factor
   — Divide by (4 + 8*floor(theta_i)) = azimuthal bin count
   — Multiply by cos(theta) = Lambertian diffuse
10. Merge with upper cascade (lines 194-219)
    — interpMinDist / interpMaxInterval for blend factor l
    — 4-corner bilinear with WeightedSample per corner
    — lastOutput = mix(S0..S3) / max(0.01, mix(S0.w..S3.w))
    — Output = Output*l + lastOutput*(1-l)
11. Write to cubemap (line 240)
```

### `shader_toy/Image.glsl` (240 lines) — DUPLICATE

Identical to CubeA.glsl. This is a ShaderToy multipass artifact — one Buffer renders to the other, but the code is the same.

---

## 3. ShaderToy PT Reference (`shader_toy_pt/`)

This is a SEPARATE ShaderToy that implements a path tracer — NOT radiance cascades. It was saved alongside the RC reference for comparison.

| File | Purpose |
|------|---------|
| `BufferA.glsl` | MIS PT: sphere intersection, NEE, Russian roulette, temporal accumulation |
| `Common.glsl` | LCG RNG, plastic sequence, cosineSample, coneSample, fresnel |
| `Image.glsl` | Display pass: sqrt tonemap of accumulated samples |

**Key PT settings:**
- `RAYS_PER_PIXEL = 6`
- `NEXT_EVENT_ESTIMATION` enabled
- 7 spheres + 1 light (Cornell-box-like sphere scene)
- Camera: `vec3(-2.5, uv.x, -uv.y)` with 60° FOV

---

## 4. Key Differences: ShaderToy → Current Implementation

### 4.1. Probe Placement

| ShaderToy | Current (`radiance_3d.comp`) |
|-----------|------------------------------|
| Surface-attached: each probe has `gTan/gBit/gNor/gPos` | Volumetric: 3D grid, no surface association |
| Probes only on walls (7 wall surfaces) | Probes on a regular 3D lattice |
| 2D atlas layout (UV within wall surface) | 3D atlas layout (XYZ within volume) |
| probeSize = 2^(cascade+1) — along 2D surface | probeSize = 2^cascadeCount — in 3D volume |

### 4.2. Bin Spherical Direction

| ShaderToy | Current |
|-----------|---------|
| Custom spherical mapping: `probeTheta` from octahedral-like axis, `probePhi` from per-probe boundary arc-length | Standard octahedral mapping: `octToDir((vec2(bin) + 0.5) / D)` |
| `probeTheta ∈ [0, π/probeSize]` (per-probe angular extent) | `octToDir` covers S² uniformly |
| Bin center = `probeDir = gTan·sinφ·sinθ + gBit·cosφ·sinθ + gNor·cosθ` | Bin center = octahedral encode/decode pair |

### 4.3. Bake Storage

| ShaderToy | Current |
|-----------|---------|
| `L · cos(θ) · ΔΩ` per bin (pre-integrated irradiance) | Raw `L` per bin + binary `α` (hit/miss) |
| No separate α — the `.w` component stores hit distance for WeightedSample merge | `.a` = 0 (surface hit), 1 (transparent), 0 (sky) |
| Consumer is cubemap fetch (no integration at consume time) | Consumer is `(4/D²)·Σ(L·cos⁺)` Riemann sum |

### 4.4. Merge Between Cascades

| ShaderToy | Current |
|-----------|---------|
| 4-corner bilinear in 2D (wall-surface space) | 8-corner trilinear in 3D (volumetric space) |
| Per-corner WeightedSample gates BOTH `.rgb` AND `.w` | Phase 3 v3: trilinear `.rgb` + scalar WeightedSample `.a` attenuation |
| Interpolation factor: `l = 1 - clamp((hitDist - minDist)/maxInterval, 0, 1)` | Same smoothstep blend formula (line 780-781) |
| Merge: `Output = Output*l + lastOutput*(1-l)` | Merge: `rad = hit.rgb*l + upperDir.rgb*(1-l)*aFactor*uGIStrength` |

### 4.5. WeightedSample Mechanism

| ShaderToy | Current |
|-----------|---------|
| Reads previous-frame cubemap at look-back direction | Reads upper cascade atlas at look-back bin (texelFetch) |
| Cone half-angle: theta = (lProbeSize/2 - 0.5)/(lProbeSize/2) * π/2 | Cone half-angle: `uUpperBinConeSin` (0.248 for D=8) |
| Visibility test: `length(relVec) < lProbeRayDist * cos(π/2 - theta) + 0.01` | Visibility test: `length(relVec) < lProbeRayDist * uUpperBinConeSin + 0.01` |
| Rejected corner contributes vec4(0.0) → excluded from bilinear | Phase 3 v3: rejected corner excluded from `.rgb` sum + `.a` fraction |
| lProbeSize = upper cascade probe size (2^(K+2)) | No equivalent — cone is uniform across cascades |

### 4.6. Multi-Bounce

| ShaderToy | Current |
|-----------|---------|
| Deterministic 4-tap spatial average from cubemap at hit surface: `TextureCube(suv) + TextureCube(suv + dx) + TextureCube(suv + dy) + TextureCube(suv + dxy)` | Stochastic 1-sample MC with cosine PDF: `sampleC0AtlasStochastic(pos, normal, seed)` |
| No random sampling — deterministic averaging | Random direction → 8-corner trilinear at one bin |
| Bounce light included in every bake ray | Bounce light included once per ray; temporal EMA accumulates |

---

## 5. Which Deltas Target Which Differences

| Delta | Target difference | Current status |
|-------|-------------------|----------------|
| #1, #2 | Consumer integration (Riemann sum bugfix) | LANDED |
| #3 | Merge per-corner gating (WeightedSample in 8-corner trilinear) | Code exists, not gated |
| #6 | WeightedSample cone size (ShaderToy geometric vs current bin-derived) | Code exists, not gated |
| #4 | Multi-bounce formulation (deterministic 4-tap vs stochastic 1-sample) | NOT IMPLEMENTED |
| #5 | Bake-time cosine pre-weighting (requires surface-attached topology) | PATH B ONLY |
| #7 | Probe-position offset convention | CONFORMANT (no work) |

---

## 6. Porting Guide

### Path A (Current Approach): Port What Fits Volumetric

| Step | What | ShaderToy reference |
|------|------|---------------------|
| 1 | #1, #2 — consumer integral fix | (n/a — these were bugs, not ports) |
| 2 | #7 — probe position convention | CubeA.glsl:132 (+0.5 offset), CubeA.glsl:206 (-0.5) |
| 3 | #3 — per-corner gated trilinear | CubeA.glsl:21-42 (WeightedSample) + 196-219 (merge) |
| 4 | #6 — geometric cone | CubeA.glsl:22-23 (theta formula), 238-239 (cos(π/2-theta)) |
| 5 | #4 — multi-bounce formulation | CubeA.glsl:166-170 (4-tap spatial avg) |

### Path B (Future Option): Surface-Attached Topology

Requires all of the above PLUS:
- Probe placement on mesh surfaces (gTan/gBit/gNor/gPos for each wall)
- Cubemap/octahedral atlas layout (replace 3D texture atlas)
- Hemisphere-only bake (above gNor, no below-horizon rays)
- Bake-time cosine + ΔΩ pre-weighting (L·cos(θ)·ΔΩ per bin)
- Consumer rewrite: cubemap fetch instead of Riemann sum

---

## 7. Code Location Map

```
shader_toy/Common.glsl → res/shaders/radiance_3d.comp
  TraceRay               → sampleSDF + light evaluation
  AQuad                  → (no analog; volumetric doesn't use quad rays)
  BRDF_GGX               → (not used in bake; used in display only)

shader_toy/CubeA.glsl    → res/shaders/radiance_3d.comp
  TextureCube            → sampleC0AtlasOneBin / sampleUpperDir
  WeightedSample         → sampleUpperDirWeighted
  mainCubemap            → main() merge block (lines 660-789)
  merge (lines 194-219)  → smoothstep blend (lines 779-789)

shader_toy/Image.glsl    → raymarch.frag
  (display pass)          → sampleProbeDir (consumer-side integral)