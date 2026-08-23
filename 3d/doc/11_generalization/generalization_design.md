# Phase 11: Generalization Design — From Hardcoded Cornell Charts to Real Mesh Surfaces

**Status:** Design decision landed; M1 CPU packer implemented (2026-08-23). No GPU.
**Source plan:** `doc/10_refactor/3d_radiance_cascades_refactor_plan.md` §Phase 11
**Date:** 2026-08-06
**Scope:** Decide how the proven reference kernel moves from the hardcoded Cornell chart set to arbitrary mesh surfaces. This document does **not** port anything; it fixes the chart-provider and tracer boundary and selects a primary approach.

---

## 1. Non-negotiable constraints

1. **Sponza is not accepted through AABB planes or box proxies.** General surface RC requires geometry-linked surface identity, material identity, and a stable world-to-chart mapping. The retired `SurfaceRC` path (`doc/9_shadertoy2/`) used box-proxy charts and was removed in Phase 10 for exactly this reason.
2. **Proven kernel contracts are preserved.** Generalization replaces only the chart provider and tracer integration. The transport/merge/feedback/final stages and the two-page (parity) / single-page (legacy) layout machinery stay byte-compatible.
3. **The parity gates (G0–G10) remain green.** Any future chart-provider or tracer change must keep the Cornell parity scene bit-identical.

---

## 2. Proven kernel contracts (what must be preserved)

The data-driven transport kernel (`res/shaders/reference_transport.comp`) reads a single scene SSBO and dispatches by primitive kind. The CPU mirrors are `src/reference_cornell_scene.h` and `src/reference_legacy_scene.h`; layout is `src/reference_layout.h`; pipeline ownership is `src/reference_pipeline.h`.

### 2.1 Scene SSBO (`GpuReferenceSceneData`, `reference_cornell_scene.h:115-128`)

- `GpuReferenceSceneHeader` — identity, counts, light ids, room bounds, constants, sun, sky, opening parameters.
- `GpuReferenceMaterial[8]` — kind (Diffuse / BlackUncharted / Reflective / Emissive / Sky) + response + emission.
- `GpuReferenceChart[8]` — the atlas atom (see 2.2).
- `GpuReferencePrimitive[13]` — the world-space trace atom (see 2.3).

### 2.2 Chart (the radiance atom)

```cpp
struct ReferenceSurfaceChart {          // reference_cornell_scene.h:42
    ReferenceChartId id;                 // stable per-surface identity
    uint32_t materialId;                 // material identity
    glm::vec3 origin, tangent, bitangent, normal;  // planar orthonormal frame
    glm::vec2 extent;                    // world-space chart size
    glm::uvec2 resolution;               // atlas texel resolution
    glm::uvec2 logicalBase;              // position in the logical atlas domain
    int handedness;
};
```

Charts own: **surface identity**, **material identity**, and the **world↔atlas mapping** (via the tangent frame + extent). Every probe and every feedback texel is addressed through a chart.

### 2.3 Primitive (the trace atom)

The tracer is kind-dispatched over the primitive SSBO (`reference_transport.comp:110-299`):

| kind | primitive | role |
|------|-----------|------|
| 0 | chart quad (plane, chartId + material + flip/tie flags) | planar chart shell |
| 1 | cylinder | legacy Cornell light / geometry |
| 2 | sphere | reflective probe ball |
| 3 | box (ABoxNormal) | Cornell interior boxes |
| 4 | exclusion operator | CSG subtraction (never intersected) |

A hit carries `chartId`, `chartUv`, `normal`, and `materialKind` — exactly what transport/feedback need.

### 2.4 Layout

- 6 cascades, logical domain → physical per-cascade atlas (parity: 1024×3072 logical, two pages; legacy: 1472×1536 single page).
- Square-ring hemisphere packing, theta/phi solid-angle + Lambert weights, texel scale 1/256 (parity) vs 1/128 (legacy) — **per-scene layout parameters**, already scene-driven.

### 2.5 Stages

Transport (direct + feedback sampling into the band image) → merge (C5→C0 hierarchy) → feedback (previous-generation C0) → final consumer (`renderFinalView`). None of these need to know *how* a chart was produced.

---

## 3. The generalization seam

The only two things that change for arbitrary meshes:

1. **Chart provider** — produce the `GpuReferenceChart[]` + `GpuReferencePrimitive[]` + `GpuReferenceMaterial[]` arrays from a mesh asset instead of a hardcoded `ParitySceneSpec`.
2. **Tracer integration** — extend the kind-dispatched primitive set so a ray can hit a *real mesh surface* and return `(chartId, chartUv, normal, materialKind)`, not a proxy plane.

Everything downstream — layout decode, transport, merge, feedback, final — consumes `(chart, hit)` records and is untouched.

The key architectural consequence: **the chart remains the radiance atom.** A mesh contributes radiance only through charts, and a ray contributes to a chart only when the tracer maps the world hit to that chart's UV. This is what "stable world-to-chart mapping" means.

---

## 4. Requirements for real-mesh charts

| # | requirement | why it matters |
|---|-------------|----------------|
| R1 | geometry-linked surface identity | a chart must map to actual triangles, not an approximating plane |
| R2 | material identity | per-island material binding (the SSBO already supports it) |
| R3 | stable world→chart mapping | the same world point must decode to the same `(chartId, uv)` every frame |
| R4 | atlas memory budget | charts must pack into the per-scene logical/physical domain |
| R5 | seam & texel-bleed control | adjacent islands must not cross-bleed radiance |
| R6 | per-island resolution | texel resolution should adapt to surface size / visual importance |
| R7 | probe compatibility | the chart must satisfy the layout's probe-coupling (square-ring) assumption |

---

## 5. Candidate approaches

### 5.1 Authored UV2 charts

Mesh exporter/artist produces an explicit second UV channel; each connected UV2 island becomes a chart. The island's 3D triangles carry the UV2; the chart is the island + its tangent frame.

- **R1/R2/R3:** exact — identity and mapping come from the authored data.
- **R4/R5/R6:** packer responsibility — an offline atlas packer sorts islands, sizes resolution, and records `logicalBase`.
- **R7:** islands are planar-ish; the existing chart frame + probe layout applies per island. Deeply curved or heavily distorted islands violate the planar-frame assumption (mitigation below).
- **Kernel fit:** the *highest* — charts map 1:1 onto `ReferenceSurfaceChart`; tracer needs only a new "triangle-mesh island" primitive kind (see §6).
- **Cost:** content pipeline (export tooling, UV2 authoring or auto-unwrap), atlas packer.

### 5.2 Meshlet / triangle charts

Automatically derive a chart per meshlet (or per connected triangle fan) with a local planar paramization, no authored UV2.

- **R1–R3:** acceptable per meshlet; identity is geometric.
- **R4/R5:** poor — thousands of tiny charts waste the fixed per-chart atlas layout and cascade probe budget; seams multiply.
- **Kernel fit:** moderate — same chart SSBO, but the layout's probe-coupling and per-cascade texel costs were sized for ~8–18 charts, not thousands.
- **Cost:** low authoring cost, high integration/quality risk.

### 5.3 Surfel pages

Precomputed surface samples (position + normal + radiance) stored in world-space pages; tracing hits a surfel rather than decoding chart UV.

- **R1–R3:** strong for identity.
- **R4/R5/R7:** conflicts — replaces the chart/atlas feedback contract with a different accumulation model; transport/merge/feedback stages would have to change. Violates the "preserve kernel contracts" constraint.
- **Kernel fit:** low (new accumulation architecture).

### 5.4 Virtual-textured surface caches

GPU virtual-texturing-style residency: virtual surface regions paged into physical pages on demand.

- **R4/R6:** best for huge scenes and streaming.
- **R5/R7:** requires residency tracking and page invalidation integrated into feedback — new infrastructure.
- **Kernel fit:** low now, high value later.
- **Cost:** highest.

---

## 6. Recommended approach

**Primary: authored UV2 charts (§5.1), with a new triangle-mesh island primitive kind in the tracer.**

**Fallback (meshes without authored UV2): meshlet charts (§5.2) as a lossy derivation, gated behind an explicit "derived charts" flag — never sold as general surface RC.**

**Deferred: surfel pages and virtual-textured surface caches (§5.3/§5.4) — revisit only after the UV2 path proves out on Sponza-class scenes.**

### 6.1 Tracer extension

Add one primitive kind to the kind-dispatched tracer:

| kind | primitive | role |
|------|-----------|------|
| 5 | mesh island (triangle range + island tangent frame + chartId) | real mesh surface |

On intersection: triangle Barycentric hit → world point/normal → island UV2 → `chartId + chartUv` returned to the existing transport/feedback path. This is the *only* tracer change; the transport/merge/feedback/final stages consume the identical `(chart, hit)` record shape as today.

Mitigations for the planar-frame assumption (R7):
- Per-island **best-fit tangent frame** from the island's triangles (not the geometric bounding plane), so chart-space UV distortion stays bounded.
- Island **resolution auto-sizing** from 3D surface area ÷ texel scale (R6).
- **Gutter / border** padding in the packer (R5) and explicit adjacent-island exclusion (existing kind-4 pattern).

### 6.2 Chart-provider interface (CPU)

```cpp
// doc/11_generalization — proposed boundary (not implemented)
struct MeshIslandSource { /* mesh handle, triangle range, uv2 range, island frame, material id */ };
struct ChartProviderResult {
    std::vector<GpuReferenceChart> charts;
    std::vector<GpuReferencePrimitive> primitives;   // kind 0 chart quads + kind 5 mesh islands
    std::vector<GpuReferenceMaterial> materials;
    GpuReferenceSceneHeader header;                  // per-scene layout params
};
ChartProviderResult buildCharts(const MeshAsset& mesh, const LayoutBudget& budget);
```

The packer (island → `logicalBase` + resolution) and the layout params are the only new logic; `ReferenceSceneGpuData` shape is reused unchanged.

---

## 7. Suggested milestones (next design cycle)

- **M1 — UV2 island extractor + atlas packer (CPU tool):** produce `ChartProviderResult` for a unique-UV2 mesh; validate packer output against the layout contract (R4/R5/R6) without touching the GPU. **Done (2026-08-23).** Sponza OBJ has tiled albedo `vt`, not unique UV2 — fail-closed (`TiledUv`), no charts emitted. See `tools/11_generalization/phase11_m1/`.
- **M2 — kind-5 mesh-island trace in `reference_transport.comp`:** CPU oracle parity for island hits; Cornell parity scene must remain bit-identical (constraint 3).
- **M3 — Sponza authored-UV2 pilot scene:** run the full transport/merge/feedback/final on a real-mesh chart set and report against the same measurement cameras. **No parity claim from proxy geometry** — this is the first geometry-linked result.
- **M4 — decision on fallbacks:** meshlet derivation quality gate, then a standing proposal for surfel/virtual-texture infra.

---

## 8. Explicit rejections

- AABB planes / box proxies standing in for Sponza surfaces (constraint 1; the Phase 10 retirement is permanent).
- "General surface RC" claims derived from any proxy chart set.
- Any chart-provider or tracer change that alters the locked parity layout constants (`reference_layout.h`) or the parity scene bytes.
- Renaming volumetric/PT/hybrid "parity" — those are independent algorithms with separate product criteria (`refactor_plan.md` §13.3).

---

## 9. Open questions for the next cycle

1. Sponza UV2 availability: **answered 2026-08-23.** `res/scene/sponza.obj` has one tiled albedo `vt` channel (`uv` range outside `[0,1]`, overlapping islands). Not unique UV2. Author a clean UV2 pass before M3. Meshlet fallback remains explicit and is **not** used by M1.
2. Atlas budget: what per-scene logical/physical size is acceptable for Sponza-scale chart counts?
3. Island count target: how small can islands get before the fixed probe-coupling overhead dominates (the §5.2 failure mode)?
4. Double-sided/backface policy for thin walls (the parity scene uses one-sided quads with explicit flip flags).
5. Whether `ReferenceChartId` stays an 8-bit enum or moves to a fully data-driven `uint32` (needed for arbitrary mesh chart counts).
