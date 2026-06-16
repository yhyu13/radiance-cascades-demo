# Milestone C — Sponza Surface-RC Chart Definitions

**Date:** 2026-06-16  
**Status:** Plan  
**Scope:** Add Sponza surface-RC chart definitions, generalize the chart system from Cornell-only to multi-scene, validate Sponza UV round-trips, capture before/after artifacts, measure quality, and prove Cornell regression control.  
**Preconditions:** Phase 3 stabilization PASS, Phase 2F smoke gate PASS, Phase 2F quality gate PENDING.  
**Blocks:** Phase 4 quality/PT work remains blocked until Phase 2F surface-RC produces nonzero GI on Sponza.

---

## 0. Goal

The surface-RC chart system is currently Cornell-only (5 room planes + 12 box faces + 1 reserved = 18 charts, all Cornell box geometry). Milestone C extends it to Sponza so that:

1. Surface-RC produces nonzero GI on Sponza (not just Cornell).
2. The chart system is generalized (scene-key-dependent chart tables, not hardcoded Cornell planes).
3. Sponza lighting is as measurable and controlled as Cornell now is.

---

## 1. Sponza Chart Identification

Sponza atrium (simplified OBJ) has these major surface groups:

| Group | Approx. geometry | Chart IDs | Notes |
|-------|------------------|-----------|-------|
| Floor | flat plane, y≈-1 | 1 | Large continuous surface |
| Ceiling | flat plane, y≈1 | 2 | Sponza ceiling (not flat in master variant — simplified is flat) |
| Left wall | vertical plane, x≈-1.9 | 3 | Long wall with arch openings |
| Right wall | vertical plane, x≈+1.9 | 4 | Mirror of left wall |
| Back wall | vertical plane, z≈-1.9 | 5 | Columns attached |
| Front wall | vertical plane, z≈+1.9 | 6 | Entrance |
| Column cluster A | 8 cylinders, left side | 7-8 | Grouped: shaft + base |
| Column cluster B | 8 cylinders, right side | 9-10 | Mirror of cluster A |
| Arch left | 3 arches, left openings | 11 | Curved — needs SDF approximation |
| Arch right | 3 arches, right openings | 12 | Mirror |
| Roof beams | horizontal beams | 13 | Small surface area, low GI contribution |

Total: ~13 Sponza charts (vs 18 Cornell charts).

**Approach:** Start with the 6 flat planes (floor, ceiling, 4 walls) — these have exact `worldToChart` mappings (plane equation → UV). Columns and arches can be approximated as cylinders/boxes for the first pass; chart-aware SDF sampling would be Phase 4 quality work.

---

## 2. Implementation Steps

### Step C0: Generalize Chart Table

Current: `surface_rc.h:155` has `std::array<int, 18> chartActive` with Cornell-specific chart IDs hardcoded in `worldToChart()` and `chartToWorld()`.

Target: Replace with a scene-key-dependent chart table:

```cpp
struct ChartDef {
    int id;             // Chart ID (1-based)
    std::string name;   // "floor", "ceiling", etc.
    glm::vec3 planeNormal;
    glm::vec3 planeOrigin;
    glm::vec2 uvScale;  // World-space extent → UV mapping scale
    glm::vec3 bmin, bmax;  // Bounds for hit classification
    bool isBox;         // true for box-face charts (Cornell boxes)
    bool isCylinder;    // true for column charts (Sponza columns)
};
```

Store a `std::vector<ChartDef>` per scene, populated by `updateScene()` based on `sceneLabel`.

### Step C1: Sponza Chart Definitions

In `surface_rc.cpp`, add Sponza chart definitions to `updateScene()`:

```cpp
if (sceneKey == "sponza" || sceneKey == "sponza_master") {
    // Floor: y = -1.0, normal = (0, 1, 0)
    charts.push_back({1, "floor", {0,1,0}, {0,-1,0}, {4.0, 4.0},
                      {-2,-1,-2}, {2,-1,2}, false, false});
    // Ceiling: y = 1.0, normal = (0, -1, 0)
    charts.push_back({2, "ceiling", ...});
    // Left wall, right wall, back wall, front wall
    ...
}
```

Use the actual OBJ bounds (`sceneBMin`, `sceneBMax`) for chart extents, not hardcoded values. Sponza OBJ bounds are approximately `(-1.9, -1, -1.9)` to `(1.9, 1, 1.9)`.

### Step C2: Sponza `worldToChart()` / `chartToWorld()`

Replace the hardcoded Cornell classification with chart-table lookup:

```cpp
bool SurfaceRC::worldToChart(const glm::vec3& p, int& chartID, float& u, float& v) const {
    for (const auto& chart : charts) {
        // Check if point is near chart's plane (within eps)
        float dist = dot(p - chart.planeOrigin, chart.planeNormal);
        if (abs(dist) < eps && p is within chart bounds) {
            chartID = chart.id;
            // UV mapping depends on chart type
            if (chart.isBox) { ... }  // existing box-face logic
            else if (chart.isCylinder) { ... }  // Sponza columns
            else { ... }  // plane mapping (existing wall/ceiling logic)
            return true;
        }
    }
    return false;
}
```

### Step C3: Shader-Side Sponza Classification

Port the chart classification into `raymarch.frag` (or `surface_radiance_debug.comp`) for Sponza. This is the chart-aware consumer that Phase 2F is missing.

For Sponza planes, the GLSL classification is the same as Cornell walls (dot-product with plane normal → chart ID + UV). For columns, approximate as boxes (simpler than cylinders for first pass).

Add Sponza chart uniforms:
```glsl
uniform vec3 uSponzaFloorOrigin;
uniform vec3 uSponzaCeilingOrigin;
uniform vec3 uSponzaWallOrigins[4];   // left, right, back, front
uniform vec3 uSponzaWallNormals[4];
uniform vec3 uSponzaWallBmin[4];
uniform vec3 uSponzaWallBmax[4];
```

### Step C4: Sponza UV Round-Trip Validation

Extend `validation_helpers.cpp` `validateUVRoundTrip()` to run on Sponza charts too. The validation should:

1. For each active Sponza chart, sample (u,v) pairs
2. Convert to world via `chartToWorld(chartID, u, v)`
3. Convert back via `worldToChart(worldPos, recoveredChartID, recoveredU, recoveredV)`
4. Check recoveredChartID == original and recoveredU/recoveredV within tolerance

Output: `sponza_uv_roundtrip_metrics.json` with same format as Cornell.

### Step C5: Capture Harness

Create `tools/milestone_c/sponza_capture.ps1` and `tools/milestone_c/cornell_control_capture.ps1`:

**Sponza before/after:**
```powershell
# Before: volumetric baseline (gain=0.10)
.\build\RadianceCascades3D.exe --load-obj=sponza --mb-gain-per-scene=1 --render-mode=0 --screenshot=tools/milestone_c/sponza_volumetric_before.png --screenshot-exr=... --exit-frames=5 --window-size=640,480

# After: surface-RC enabled (Phase 2F toggle)
.\build\RadianceCascades3D.exe --load-obj=sponza --use-surface-rc=1 --enable-surface-rc-gi=1 --render-mode=0 --screenshot=tools/milestone_c/sponza_surface_rc_after.png --screenshot-exr=... --exit-frames=5 --window-size=640,480
```

**Cornell control:**
```powershell
# Re-run Cornell validation — must not regress
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --validate-uv-roundtrip --phase3-validation-json=tools/milestone_c/cornell_control_metrics.json --screenshot=tools/milestone_c/cornell_control.png --exit-frames=2 --window-size=640,480
```

### Step C6: Quality Metrics

Extend `phase2f_binding_metrics.json` to include Sponza rows:

```json
{
  "sponza_volumetric": {
    "path": "...",
    "mean_luma": ...,
    "ratio_self": ...,
    "abs_p95": ...
  },
  "sponza_surface_rc": {
    "path": "...",
    "mean_luma": ...,
    "nonblack_pixels": ...,
    "surface_rc_gi_only_nonblack": "PASS or FAIL"
  },
  "cornell_control": {
    "pass_rate": 1.0,
    "regression": "NONE"
  }
}
```

---

## 3. Acceptance Criteria

Milestone C passes when ALL of the following are true:

| # | Criterion | Measurement |
|---|-----------|-------------|
| 1 | Sponza chart definitions exist in `surface_rc.cpp` | `isChartActive()` returns true for Sponza floor/ceiling/walls |
| 2 | Sponza UV round-trip validation passes | `sponza_uv_roundtrip_metrics.json` reports PASS, pass_rate ≥ 0.99 |
| 3 | Sponza surface-RC GI-only is non-black | `surface_rc_gi_only_nonblack=PASS` for Sponza |
| 4 | Sponza before/after PNG pair exists and is visually distinct | Both files exist, nonblank, surface-RC mode differs from volumetric |
| 5 | Sponza quality metrics JSON exists | `ratio_self`, `|p95|`, `dim_pct`, `bright_pct` computed for both paths |
| 6 | Cornell control capture shows no regression | Cornell UV round-trip still PASS, pass_rate=1.0; Cornell PNG not visually degraded |
| 7 | Build succeeds, runtime exits 0 | `cmake --build build --config Release` succeeds; Sponza+surface-RC smoke exits 0 |

---

## 4. Known Risks

| Risk | Mitigation |
|------|-----------|
| Sponza chart extents depend on OBJ bounds, not hardcoded Cornell values | Use `sceneBMin/sceneBMax` from `updateScene()` for chart bounds |
| Columns/arches are curved, not planar | Approximate as boxes for first pass; exact cylinder classification deferred to Phase 4 |
| `chartActive` array size changes from 18 (Cornell) to 13 (Sponza) | Use `std::vector<ChartDef>` instead of fixed-size array; `getChartActive()` returns scene-dependent size |
| Shader-side classification needs Sponza-specific uniforms | Add Sponza chart uniform block; gate on `uSceneType` or scene-key uniform |
| Sponza-master has 262K faces vs simplified ~3K | Chart classification should work on both; OBJ bounds are similar |

---

## 5. Dependency Map

```text
Phase 2F smoke gate (PASS) ──→ C0: generalize chart table ──→ C1: Sponza chart defs ──→ C2: worldToChart/chartToWorld
                                                                                      │
                                                                                      ├──→ C3: shader-side classification
                                                                                      │
                                                                                      ├──→ C4: UV round-trip validation
                                                                                      │
                                                                                      └──→ C5: capture harness + C6: quality metrics ──→ acceptance gate
                                                                                                                               │
                                                                                                                               └──→ E: lock Cornell regression (milestone E)
```

---

## 6. What Does NOT Change

- Phase 4 quality/PT gates remain blocked until surface-RC produces real EXR/PT quality evidence
- Milestone D (scene-authoring) remains deferred
- The volumetric-bridge shader consumer (`sampleSurfaceRC_GI`) stays as-is for now; chart-aware consumer is a separate Phase 2F completion step
- Cornell chart definitions are preserved; only generalized (not removed)

---

## 7. Estimated Effort

| Step | Effort | Notes |
|------|--------|-------|
| C0: Generalize chart table | ~2h | Replace `std::array<int,18>` with `std::vector<ChartDef>`; keep Cornell working |
| C1: Sponza chart definitions | ~1h | 6 planes + column/arch approximations |
| C2: worldToChart/chartToWorld | ~2h | Scene-dependent lookup, preserve Cornell path |
| C3: Shader-side classification | ~2h | Sponza plane uniforms + GLSL classifyHit for Sponza |
| C4: UV round-trip validation | ~1h | Extend validation_helpers.cpp for Sponza |
| C5: Capture harness | ~1h | PowerShell scripts, reuse v3_baseline patterns |
| C6: Quality metrics | ~1h | JSON generation, reuse analyze_baselines.py |
| **Total** | **~10h** | |

---

## 8. Post-C State

After milestone C passes:
- Sponza has surface-RC chart definitions that produce nonzero GI
- The chart system is scene-dependent (works for both Cornell and Sponza)
- Sponza before/after artifacts exist with measurable quality metrics
- Cornell regression is controlled

Then milestone E: lock Cornell Box as automated regression test (baseline capture + metrics JSON + automated gate script).

Phase 4 remains blocked until:
- Surface-RC chart-aware consumer produces real lighting (not volumetric-bridge)
- EXR/PT quality comparison passes the 0.50 gate on both scenes
