# ShaderToy2 Phase 2B-0 Implementation — Chart Mask + Ray Origin/Direction Debug

**Date:** 2026-05-29  
**Status:** Implemented + self-critiqued  
**Scope:** `chartActive[6]` mask plus ring-packed ray-origin/hemisphere-direction debug. No tracing, no NEE, no persistent feedback.

---

## 1. Plan Executed

Implemented the plan in:

```text
doc/9_shadertoy2/phase2b0_plan_chart_mask_ray_debug.md
```

The phase intentionally stops before radiance. It only validates the geometry inputs that later radiance will consume:

```text
active chart mask
surface probe world position
ray origin with normal bias
hemisphere direction through chart TBN
```

---

## 2. C++ Changes

Updated:

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.cpp
```

### SurfaceRC chart mask

Added:

```cpp
std::array<int, 6> chartActive;
const std::array<int, 6>& getChartActive() const;
int getActiveChartCount() const;
```

Current policy:

```text
chartActive = {1, 1, 1, 1, 1, 0}
```

Meaning:

```text
1 floor   active
2 ceiling active
3 left    active
4 right   active
5 back    active
6 front   inactive/reserved
```

Rationale:

```text
res/scene/cornell_box.obj has no front_wall object.
Chart 6 remains reserved for ShaderToy layout parity but must not be treated as a real radiance surface.
```

### Uniform upload

`SurfaceRC::dispatchRingDebug()` now uploads:

```cpp
glUniform1iv(glGetUniformLocation(computeProgram, "uChartActive"), 6, chartActive.data());
```

### UI

Ring debug combo now includes seven modes:

```text
0 Chart + Cascade
1 Probe Coord
2 Direction Coord
3 Ring / Theta
4 Probe World Pos
5 Ray Origin
6 Hemisphere Direction
```

Settings UI also displays:

```text
Active charts: 5/6 [1 1 1 1 1 0]
```

---

## 3. Shader Changes

Updated:

```text
res/shaders/surface_ring_debug.comp
```

### Active chart mask

Added:

```glsl
uniform int uChartActive[6];
```

After chart decode:

```glsl
if (c.valid && uChartActive[c.id - 1] == 0) {
    c.valid = false;
}
```

Inactive Chart 6 therefore renders as invalid/dark in all ring debug modes.

### Shared probe position computation

The shader now computes `probeWorldPos` once and reuses it for modes 4/5:

```glsl
vec2 probeUVChart = clamp((probeCoord + 0.5) * probeSize / c.gRes, vec2(0.0), vec2(1.0));
vec3 probeWorldPos = ... chartToWorld ...;
```

### Mode 5 — Ray origin

```glsl
vec3 rayOrigin = probeWorldPos + c.normal * 0.01;
rgb = remapWorld(rayOrigin);
```

The `0.01` bias is debug-only. Future radiance should make this a uniform tied to scene scale.

### Mode 6 — Hemisphere direction

```glsl
vec2 disk = clamp(probeRel / max(probeSize * 0.5, 1.0), vec2(-1.0), vec2(1.0));
float localZ = sqrt(max(0.0, 1.0 - dot(disk, disk)));
vec3 localDir = normalize(vec3(disk.x, disk.y, localZ));
vec3 worldDir = normalize(localDir.x * c.tangent + localDir.y * c.bitangent + localDir.z * c.normal);
rgb = worldDir * 0.5 + 0.5;
```

Important caveat:

```text
This is a debug hemisphere mapping, not yet a claim of exact ShaderToy angular distribution.
```

It verifies TBN orientation and hemisphere-above-surface behavior before tracing.

---

## 4. Verification

### Build

Command:

```powershell
cmake --build build --config Debug
```

Result:

```text
RadianceCascades3D.exe built successfully
```

Warnings are pre-existing MSVC warnings.

### Screenshot sweep

Captured:

```text
tools/phase2b0_visual/m0.png
tools/phase2b0_visual/m5.png
tools/phase2b0_visual/m6.png
```

Commands used:

```powershell
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=0 --screenshot=tools/phase2b0_visual/m0.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=5 --screenshot=tools/phase2b0_visual/m5.png --exit-frames=2 --window-size=1024,768
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=6 --screenshot=tools/phase2b0_visual/m6.png --exit-frames=2 --window-size=1024,768
```

### Structure-aware checks

A `System.Drawing` check split the visible ring overlay into:

```text
active region:   x 0..335  (charts 1..5)
inactive region: x 336..383 (chart 6/front)
overlay y range: 193..767
```

Result:

```text
m0.png activeUnique4=25  inactiveUnique4=2 inactiveBright=0/432
m5.png activeUnique4=298 inactiveUnique4=2 inactiveBright=0/432
m6.png activeUnique4=691 inactiveUnique4=2 inactiveBright=0/432
```

Interpretation:

```text
Chart 6 is dark/inactive in all tested modes: inactiveBright=0/432.
Ray origin debug has nontrivial active-chart variation: activeUnique4=298.
Hemisphere direction debug has strong active-chart variation: activeUnique4=691.
```

This is sufficient evidence for Phase 2B-0 because no radiance is computed yet.

---

## 5. Self-Critique

### SC1 — Hemisphere direction mapping is debug-only, not exact ShaderToy

Accepted.

The disk-to-hemisphere mapping verifies TBN and above-surface orientation, but the final radiance shader may need to port ShaderToy's exact angular distribution. The code and plan explicitly label this as debug hemisphere mapping.

Required before radiance quality claims:

```text
Compare final sampling direction formula against CubeA.glsl and either match it or document the divergence.
```

### SC2 — Bias is hardcoded as `0.01`

Accepted.

`rayOrigin = probeWorldPos + normal * 0.01` is fine for visualization but not final tracing.

Improvement required in Phase 2B-1:

```text
Use a uniform/derived bias tied to scene bounds or voxel size.
```

### SC3 — `chartActive` is currently hardcoded

Accepted.

This is correct for `cornell_box.obj`, but it is not a general scene-analysis system.

Future improvement:

```text
Populate chartActive from scene variant metadata or geometry-plane checks.
```

### SC4 — Inactive charts disappear entirely in debug

Accepted.

This is the correct radiance-safe behavior. If layout debugging needs to inspect reserved charts later, add a separate mode that shows inactive charts with a distinct dim tint.

### SC5 — Screenshots still require human inspection for final confidence

Accepted.

Structure-aware checks cover the key blockers, but they do not replace direct visual inspection. Human review of `tools/phase2b0_visual/*.png` is still useful.

---

## 6. Improvements Applied After Self-Critique

During self-critique, `updateScene()` contained a redundant supported/unsupported branch that assigned the same mask in both paths. It was simplified to a single documented assignment:

```cpp
chartActive = {1, 1, 1, 1, 1, 0};
```

This avoids implying unsupported scenes have a distinct, validated chart policy.

---

## 7. Current Limitations

Still not implemented:

```text
- surface_radiance.comp
- SDF/mesh tracing from surface probes
- point-light NEE
- persistent ping-pong feedback
- hit-distance alpha
- raymarch surface GI lookup
- EXR/PT metrics
```

Known debug limitations:

```text
- Mode 6 is debug hemisphere mapping, not final ShaderToy sampling proof.
- Ray-origin bias is hardcoded.
- Chart mask is hardcoded for open-front Cornell.
```

---

## 8. Next Implementation Slice

Proceed to **Phase 2B-1: Surface Radiance Skeleton, No Feedback**.

Recommended scope:

```text
1. Add surface_radiance.comp or a separate radiance-debug target.
2. Reuse the exact chartActive + ring decode + probeWorldPos + worldDir helpers from surface_ring_debug.comp.
3. Output ray-origin/worldDir diagnostic values into a future radiance atlas target.
4. Add numeric readback checks for a few known atlas texels:
   - floor center ray origin near y=min+bias
   - ceiling center ray origin near y=max-bias
   - left/right/back normals produce expected direction color center bins
5. Still do not add NEE or feedback until tracing debug is verified.
```

---

## 9. Files Changed In This Phase

```text
src/surface_rc.h
src/surface_rc.cpp
src/demo3d.cpp
res/shaders/surface_ring_debug.comp
doc/9_shadertoy2/phase2b0_plan_chart_mask_ray_debug.md
doc/9_shadertoy2/phase2b0_impl_chart_mask_ray_debug.md
tools/phase2b0_visual/m0.png
tools/phase2b0_visual/m5.png
tools/phase2b0_visual/m6.png
```
