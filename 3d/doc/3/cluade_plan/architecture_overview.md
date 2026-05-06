# 3D Radiance Cascades — Architecture Overview

**Date:** 2026-04-22  
**Branch:** 3d  
**Covers:** Phase 1 (SDF + analytic primitives), Phase 2 (probe grid, debug modes),
Phase 2.5 (albedo volume, probe shadow), Phase 3 (multi-level cascade, merge).

---

## 1. System Pipeline (Per-Frame Render Loop)

```mermaid
flowchart TD
    A([Frame Start]) --> B{sceneDirty?}
    B -->|yes| C[Pass 1: voxelizationPass\nupload primitives to SSBO]
    B -->|no| D
    C --> D{sdfReady?}
    D -->|no| E[Pass 2: sdfGenerationPass\nsdf_analytic.comp\nwrites SDF + albedo volumes]
    D -->|yes| F
    E --> F{cascadeReady?}
    F -->|no| G[Pass 3: updateRadianceCascades\nradiance_3d.comp × 4 levels\nC3→C2→C1→C0]
    F -->|yes| H
    G --> G2[Probe Readback\nglGetTexImage × 4\nstats for UI]
    G2 --> H[Pass 4: raymarchPass\nraymarch.frag\nfullscreen quad]
    H --> I[Pass 5: renderSDFDebug\ntop-left 400×400 panel]
    I --> J[Pass 6: renderRadianceDebug\ntop-right 400×400 panel]
    J --> K[renderUI\nImGui panels]
    K --> L([Frame End])

    style E fill:#2d6a9f,color:#fff
    style G fill:#2d6a9f,color:#fff
    style H fill:#2d6a9f,color:#fff
```

**Invalidation chain:**
- `sceneDirty` → clears `sdfReady` → clears `cascadeReady`
- `disableCascadeMerging` toggle → clears `cascadeReady` (via `lastMergeFlag` sentinel)
- Passes 2 and 3 only run when their respective dirty flags are set — effectively once per scene change for static scenes.

---

## 2. Scene Representation — Analytic SDF Primitives

```mermaid
flowchart LR
    subgraph CPU ["CPU (demo3d.cpp)"]
        P1[AnalyticSDF::createCornellBox\naddBox / addSphere]
        P2[GPUPrimitive SSBO\n64 bytes each:\ntype + pad + pos + scale + color]
        P1 -->|uploadPrimitivesToGPU| P2
    end

    subgraph GPU ["GPU (sdf_analytic.comp)"]
        P3["Per-voxel loop over SSBO\nfor each voxel coord:\n  minDist = min(sdfBox, sdfSphere)\n  closestColor = color of nearest prim"]
        P4[sdfVolume\nR32F 64³\nSigned distance field]
        P5[albedoVolume\nRGBA8 64³\nNearest primitive color]
        P3 --> P4
        P3 --> P5
    end

    P2 -->|SSBO binding 0| P3
    P4 -->|image binding 0| P4
    P5 -->|image binding 1| P5
```

**Primitive layout (std430):**
```
struct Primitive {
    int  type;           // 0=box, 1=sphere
    float pad0,pad1,pad2;// 12 bytes padding → position at offset 16
    vec4 position;       // world center
    vec4 scale;          // box: half-extents; sphere: radius in .x
    vec4 color;          // albedo RGB
};  // total: 64 bytes
```

**Cornell Box composition:** 7 boxes (floor, ceiling, back wall, left wall RED, right wall GREEN, box1, box2) + optional sphere.

---

## 3. Cascade Hierarchy — Interval Structure

Each of the 4 cascade levels covers a distinct **distance shell** from each probe.
All levels share the same 32³ probe grid at identical world positions.

```mermaid
flowchart LR
    subgraph Intervals ["Ray Interval per Level  (d = 0.125m)"]
        C0["C0: [0.02, d]\n= [0.02, 0.125m]\nnear-field only"]
        C1["C1: [d, 4d]\n= [0.125, 0.5m]\nmid-field"]
        C2["C2: [4d, 16d]\n= [0.5, 2.0m]\nwalls reachable"]
        C3["C3: [16d, 64d]\n= [2.0, 8.0m]\nfar-field / corners"]
    end

    C0 -.->|misses read| C1
    C1 -.->|misses read| C2
    C2 -.->|misses read| C3
    C3 -.->|no upper level| VOID[" "]

    style C0 fill:#1a7a1a,color:#fff
    style C1 fill:#6aaa1a,color:#fff
    style C2 fill:#aa6a1a,color:#fff
    style C3 fill:#7a1a1a,color:#fff
```

**Interval formula in shader:**
```glsl
float d = uBaseInterval;  // 0.125 for all levels
float tMin = (uCascadeIndex == 0) ? 0.02
           : pow(4.0, float(uCascadeIndex - 1)) * d;
float tMax = pow(4.0, float(uCascadeIndex)) * d;
```

**Cornell Box reachability:**
- Center probe (0,0,0) is 2.0m from every wall
- C0: walls unreachable (0.125m max) → almost always misses
- C1: walls unreachable (0.5m max) → misses for center probes
- C2: walls reachable (2.0m max) → hits for center probes ✓
- C3: overshoots for center (starts at 2.0m, walls are at 2.0m) → mostly empty

---

## 4. Cascade Update + Merge Pass

```mermaid
flowchart TD
    subgraph Dispatch ["Dispatch Order: Coarse → Fine"]
        D3[Dispatch C3\nuHasUpperCascade = 0\nNo upper level]
        D2[Dispatch C2\nuHasUpperCascade = 1\nuUpperCascade = C3.probeGrid]
        D1[Dispatch C1\nuHasUpperCascade = 1\nuUpperCascade = C2.probeGrid]
        D0[Dispatch C0\nuHasUpperCascade = 1\nuUpperCascade = C1.probeGrid]
        D3 --> D2 --> D1 --> D0
    end

    subgraph PerProbe ["Per-Probe Logic (radiance_3d.comp)"]
        R1[For each ray direction\nFibonacci sphere, 8 rays]
        R2{raymarchSDF hit\nin interval?}
        R3[Add hit.rgb\nLambertian + albedo + shadow]
        R4{uHasUpperCascade?}
        R5[Sample uUpperCascade\nat probe UVW\nisotropic merge]
        R6[Skip\nno far-field data]
        R1 --> R2
        R2 -->|yes| R3
        R2 -->|no| R4
        R4 -->|yes| R5
        R4 -->|no| R6
        R3 --> R1
        R5 --> R1
        R6 --> R1
    end

    D0 --> PerProbe
```

**Sampler bindings during cascade update:**

| Slot | Texture | Uniform |
|------|---------|---------|
| Sampler 0 | `sdfTexture` (R32F 64³) | `uSDF` |
| Sampler 1 | `albedoTexture` (RGBA8 64³) | `uAlbedo` |
| Sampler 2 | `cascades[N+1].probeGridTexture` | `uUpperCascade` |
| Image 0 | `cascades[N].probeGridTexture` (RGBA16F 32³) | `oRadiance` WRITE |

**Shadow ray (inShadow):** 32-step sphere-march from hit point toward light. Returns true if SDF < 0.002 at any step. Makes probe values respect occlusion — shadow regions stay dark in the cascade.

---

## 5. Texture Inventory

```mermaid
flowchart LR
    subgraph Textures ["GPU Textures"]
        T1["sdfTexture\nGL_R32F\n64³ voxels\nSigned distance field"]
        T2["albedoTexture\nGL_RGBA8\n64³ voxels\nNearest primitive color"]
        T3["cascades[0].probeGridTexture\nGL_RGBA16F\n32³ probes\nMerged radiance C0"]
        T4["cascades[1].probeGridTexture\nGL_RGBA16F\n32³ probes\nMerged radiance C1"]
        T5["cascades[2].probeGridTexture\nGL_RGBA16F\n32³ probes\nMerged radiance C2"]
        T6["cascades[3].probeGridTexture\nGL_RGBA16F\n32³ probes\nRaw radiance C3"]
    end

    subgraph Passes
        P1["sdf_analytic.comp\nWRITES T1, T2"]
        P2["radiance_3d.comp\nREADS T1, T2\nREADS T(N+1), WRITES T(N)"]
        P3["raymarch.frag\nREADS T1, T2, T(selectedCascade)"]
    end

    P1 --> T1
    P1 --> T2
    T1 --> P2
    T2 --> P2
    P2 --> T3
    P2 --> T4
    P2 --> T5
    P2 --> T6
    T1 --> P3
    T2 --> P3
    T3 --> P3
```

---

## 6. Raymarch Pass — Render Modes

```mermaid
flowchart TD
    A[Ray from camera through pixel] --> B[intersectBox: find tNear/tFar]
    B -->|miss| SKY[Sky color 0.1,0.1,0.15]
    B -->|hit| C[March along ray\nSDF-guided steps]
    C -->|dist < EPSILON| HIT[Surface hit at pos]
    C -->|t > tFar| MISS2[No hit → mode 5 check]

    HIT --> M1{uRenderMode}

    M1 -->|1| N1["Normal as RGB\nnormal × 0.5 + 0.5"]
    M1 -->|2| N2["Depth map\nt-tNear / tFar-tNear\nnear=white far=dark"]
    M1 -->|3| N3["Indirect × 5\ntoneMapACES\nprobe at hit UVW"]
    M1 -->|4| N4["Direct only\nalbedo × Lambertian\nno cascade"]
    M1 -->|6| N6["GI only raw\nindirect × 2\nno ACES no gamma"]
    M1 -->|0| N0["Final\ndirect + indirect×1\ntoneMapACES + gamma"]

    MISS2 -->|uRenderMode==5| N5["Step heatmap\nstepCount / 32\ngreen→yellow→red"]
    MISS2 -->|other| N0b["Tone map + gamma\naccumulated color"]

    style N0 fill:#2d6a9f,color:#fff
    style N6 fill:#1a7a1a,color:#fff
    style N4 fill:#7a1a1a,color:#fff
    style N3 fill:#6aaa1a,color:#fff
```

**Mode quick-reference:**

| Mode | Name | Tone Map | Gamma | GI checkbox matters? |
|------|------|----------|-------|----------------------|
| 0 | Final | ACES | yes | yes — adds `indirect × 1.0` |
| 1 | Normals | no | no | no |
| 2 | Depth | no | no | no |
| 3 | Indirect × 5 | ACES | no | no — always samples uRadiance |
| 4 | Direct only | ACES | yes | no — bypasses cascade |
| 5 | Step heatmap | no | no | no |
| 6 | GI only | **no** | **no** | no — raw linear, always samples |

**Cascade selector:** `selectedCascadeForRender` (C0–C3 radio) controls which `probeGridTexture` is bound to `uRadiance` in the raymarch pass. Modes 3, 6, and 0+GI all read from this binding.

---

## 7. Full Data Flow

```mermaid
flowchart LR
    subgraph CPU
        PRIM[Analytic Primitives\nCornell Box / Sphere]
        SSBO[GPU Primitive SSBO\nstd430 layout]
        PRIM --> SSBO
    end

    subgraph Pass2 ["Pass 2: sdf_analytic.comp"]
        SDF_GEN[Evaluate SDF + albedo\nper 64³ voxel]
    end

    subgraph Pass3 ["Pass 3: radiance_3d.comp × 4"]
        PROBE_C3[C3 dispatch\nSpheres rays 2–8m\nno merge]
        PROBE_C2[C2 dispatch\nRays 0.5–2m\nmerge from C3]
        PROBE_C1[C1 dispatch\nRays 0.125–0.5m\nmerge from C2]
        PROBE_C0[C0 dispatch\nRays 0–0.125m\nmerge from C1]
        PROBE_C3 --> PROBE_C2 --> PROBE_C1 --> PROBE_C0
    end

    subgraph Pass4 ["Pass 4: raymarch.frag"]
        RM[Primary ray\nSDF-guided march\nmode 0–6]
    end

    SSBO -->|SSBO binding 0| SDF_GEN
    SDF_GEN -->|R32F image 0| SDF_VOL[(sdfTexture\n64³)]
    SDF_GEN -->|RGBA8 image 1| ALB_VOL[(albedoTexture\n64³)]

    SDF_VOL -->|sampler 0| PROBE_C3
    ALB_VOL -->|sampler 1| PROBE_C3

    PROBE_C3 -->|RGBA16F image 0| CAS3[(C3 probeGrid\n32³)]
    CAS3 -->|sampler 2| PROBE_C2
    PROBE_C2 --> CAS2[(C2 probeGrid\n32³)]
    CAS2 -->|sampler 2| PROBE_C1
    PROBE_C1 --> CAS1[(C1 probeGrid\n32³)]
    CAS1 -->|sampler 2| PROBE_C0
    PROBE_C0 --> CAS0[(C0 probeGrid\n32³)]

    SDF_VOL -->|sampler 0| RM
    ALB_VOL -->|sampler 2| RM
    CAS0 -->|sampler 1\nselectedCascade| RM

    RM --> FB[Framebuffer\nfinal image]
```

---

## 8. Key Design Decisions and Tradeoffs

### Fixed 32³ probe resolution across all levels

All 4 cascade levels use identical 32³ grids at the same world positions.
The cascade INDEX determines the ray distance shell, not the probe density.

| What this buys | What it costs |
|---|---|
| Simple implementation (same texture format and size) | Over-sampling: C0 has 32³ probes for a 0.125m shell |
| Same UVW mapping — upper cascade lookup at `uvwProbe` is exact | 4× memory vs. halving resolution per level |
| Easy cascade selector in UI | Phase 4 should halve resolution per level for efficiency |

### Isotropic probe merge (no per-direction storage)

On ray miss, the upper cascade contributes its **average** radiance at the probe position, not the directional radiance along the missed ray.

```
Correct: totalRadiance += uUpperCascade_in_direction_of_missed_ray
Current: totalRadiance += texture(uUpperCascade, uvwProbe).rgb   // average
```

Effect: indirect lighting has no directionality. Color bleed is averaged across all 8 ray samples, diluting red/green wall contribution. Directional merging requires SH2 (9 coefficients per probe per channel) — Phase 4.

### Single `cascadeReady` flag for all levels

All 4 levels recompute together on any invalidation. This is correct for a static scene. For dynamic scenes, per-level dirty tracking and partial updates would be needed.

### Shadow ray in probe computation

The `inShadow()` function adds 32 additional march steps per surface hit in the cascade dispatch. This makes probes dark in shadowed regions, producing physically better indirect lighting but reducing overall probe brightness. At 8 rays × 32 shadow steps × 128 march steps = ~12k steps per probe, this is the dominant compute cost.

---

## 9. Debug Infrastructure Summary

| Tool | Location | What it shows |
|------|----------|--------------|
| Mode 1: Normals | raymarch.frag | SDF gradient at surface hit |
| Mode 2: Depth | raymarch.frag | Ray travel distance, near=white |
| Mode 3: Indirect×5 | raymarch.frag | Probe radiance at hit, magnified |
| Mode 4: Direct only | raymarch.frag | Lambertian + albedo, no cascade |
| Mode 5: Step heatmap | raymarch.frag | Ray march cost, normalized to 32 steps |
| Mode 6: GI only | raymarch.frag | Raw linear probe value ×2, no tonemapping |
| SDF debug panel | top-left 400×400 | SDF slice / max-proj / surface normals |
| Radiance debug panel | top-right 400×400 | Probe grid slice / projection |
| Cascade selector C0–C3 | Cascades panel | Which level drives modes 3, 6, 0+GI |
| Disable Merge toggle | Cascades panel | Recomputes all levels with/without upper cascade |
| Per-cascade probe stats | Cascades panel | Non-zero%, maxLum, meanLum per level |

---

## 10. File Map

| File | Role |
|------|------|
| `src/demo3d.h` | Demo3D class, RadianceCascade3D struct, all member declarations |
| `src/demo3d.cpp` | Render loop, all passes, UI, scene setup |
| `src/analytic_sdf.h/.cpp` | CPU-side primitive list, Cornell Box builder |
| `res/shaders/sdf_analytic.comp` | GPU: writes sdfVolume + albedoVolume from SSBO primitives |
| `res/shaders/radiance_3d.comp` | GPU: probe injection with per-cascade intervals + merge |
| `res/shaders/raymarch.frag` | GPU: primary ray, all 7 render modes |
| `res/shaders/sdf_debug.frag` | SDF slice visualization (top-left panel) |
| `res/shaders/radiance_debug.frag` | Probe grid slice visualization (top-right panel) |
