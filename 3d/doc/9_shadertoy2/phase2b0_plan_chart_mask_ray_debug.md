# ShaderToy2 Phase 2B-0 Plan — Chart Mask + Ray Origin/Direction Debug

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Phase 2B-0 only — add `chartActive[6]`, reuse the verified ShaderToy ring decode, and add ray-origin/ray-direction debug modes. No scene tracing, no NEE, no feedback.

---

## 1. Goal

Move from Phase 2A coordinate indexing to the smallest useful pre-radiance step:

```text
inactive chart masking + surface probe origin/debug direction visualization
```

This phase must prepare the exact data semantics Phase 2B radiance will need, without yet casting rays.

Required carry-over from Critique 02/03:

```text
Chart 6/front wall is not present in cornell_box.obj.
It must be inactive before any persistent radiance/feedback path exists.
```

---

## 2. Non-Goals

Do **not** implement in this phase:

```text
- SDF/mesh ray tracing from probes
- point-light NEE
- direct lighting at probe hits
- persistent ping-pong feedback
- hit-distance alpha semantics
- final raymarch surface GI lookup
- EXR/PT metric comparison
```

Reason:

```text
If ray origin and hemisphere direction are wrong, radiance would only hide the bug. Debug the geometry first.
```

---

## 3. Implementation Plan

### 3.1 `chartActive[6]` source of truth

Add to `SurfaceRC`:

```cpp
int chartActive[6];
```

Initial policy:

```text
cornell / cornell_orig / cornell_orig_alcove:
  chart 1 floor   active
  chart 2 ceiling active
  chart 3 left    active
  chart 4 right   active
  chart 5 back    active
  chart 6 front   inactive for cornell_box.obj
```

For unsupported/non-Cornell scenes:

```text
keep charts visible only as magenta-tinted debug if needed,
but final Phase 2B radiance must not treat them as supported surfaces.
```

### 3.2 Pass active mask to ring debug shader

Add uniform:

```glsl
uniform int uChartActive[6];
```

After `decodeChart`, apply:

```glsl
if (c.valid && uChartActive[c.id - 1] == 0) {
    c.valid = false;
}
```

For debug readability:

```text
inactive chart area should render dark checker/invalid rather than valid chart color.
```

This makes Chart 6 visibly inactive in all ring modes.

### 3.3 Add ray debug modes

Extend ring debug modes from 0..4 to 0..6:

```text
0 chart+cascade
1 probe coordinate
2 direction coordinate
3 ring/theta
4 probe world position
5 ray origin
6 hemisphere direction
```

Mode 5:

```glsl
probeWorldPos = chartToWorld(c, probeUVChart);
rayOrigin = probeWorldPos + normal * bias;
rgb = remapWorld(rayOrigin);
```

Mode 6:

Compute a ShaderToy-style hemisphere direction from the ring-packed direction coordinate.

Minimum acceptable first implementation:

```glsl
vec2 disk = probeRel / max(probeSize * 0.5, 1.0);
disk = clamp(disk, vec2(-1.0), vec2(1.0));
float z = sqrt(max(0.0, 1.0 - dot(disk, disk)));
vec3 localDir = normalize(vec3(disk.x, disk.y, z));
vec3 worldDir = normalize(localDir.x * c.tangent + localDir.y * c.bitangent + localDir.z * c.normal);
rgb = worldDir * 0.5 + 0.5;
```

This may not be the final ShaderToy angular distribution, but it verifies the TBN and hemisphere orientation. The implementation must label this as debug hemisphere mapping if it diverges from the exact `CubeA.glsl` mapping.

### 3.4 Update C++ UI/CLI

Update debug mode clamp/name tables:

```text
ringDebugMode 0..6
```

Update ImGui combo:

```text
Ray Origin
Hemisphere Direction
```

CLI already supports numeric mode via:

```text
--surface-ring-debug-mode=N
```

No new CLI needed.

---

## 4. Verification Gates

### Build / runtime

```powershell
cmake --build build --config Debug
.\build\RadianceCascades3D.exe --load-obj=cornell --use-surface-rc=1 --surface-debug-target=ring --surface-ring-debug-mode=6 --exit-frames=1 --window-size=1024,768
```

### Screenshot/readback evidence

Capture at minimum:

```text
tools/phase2b0_visual/ring_m0_chart_mask.png
tools/phase2b0_visual/ring_m5_ray_origin.png
tools/phase2b0_visual/ring_m6_ray_dir.png
```

Expected:

```text
m0: Chart 6/front wall region is inactive/dark across all cascade bands.
m5: Ray origin gradients match probe world-position gradients with normal bias only.
m6: Direction colors vary by ring/tile and respect chart normals/TBN.
```

### Structure checks

At minimum use `System.Drawing` to confirm:

```text
Chart 6 overlay region has low/non-chart color in mode 0.
Mode 5 has nontrivial variation in active charts.
Mode 6 has nontrivial variation in active charts.
```

Do not claim Phase 2B-0 complete from build + clean exit only.

---

## 5. Stop-Loss

Carry forward the Critique 01 policy locally:

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if still failing: stop feature work and write a diagnosis/update doc before continuing
```

---

## 6. Self-Critique of This Plan

### SC1 — Ray direction mapping may not exactly match ShaderToy

Accepted. The proposed disk-to-hemisphere mapping is a debug-friendly construction, not a guaranteed exact copy of `CubeA.glsl`'s angular mapping.

Improvement:

```text
Mark mode 6 as hemisphere-direction debug, not final sampling distribution.
Before radiance, compare/port the exact ShaderToy probeDir generation if needed.
```

### SC2 — Chart active mask only handles front wall

Accepted. This is sufficient for `cornell_box.obj`, but future OBJ variants/interior object charts need more granular masks.

Improvement:

```text
Store chartActive[6] in SurfaceRC now and pass to shader. Later it can be populated from scene analysis rather than hardcoded sceneKey.
```

### SC3 — Visual verification still depends partly on screenshots

Accepted. Unlike radiance phases, this is geometry/debug data, so screenshots plus small pixel summaries are acceptable.

Improvement:

```text
Add chart-6 low-color region check and active-region variation checks, not just screenshots.
```

### SC4 — Masking Chart 6 changes Phase 2A screenshots

Accepted. That is desired behavior before Phase 2B. Phase 2A preserved six-chart debug for layout; Phase 2B-0 changes semantics to real scene-active charts.

Improvement:

```text
Document that Phase 2B-0 m0 differs from Phase 2A m0 by making Chart 6 inactive.
```

---

## 7. Improved Final Plan

Implement exactly:

```text
1. chartActive[6] in SurfaceRC.
2. uChartActive[6] in surface_ring_debug.comp.
3. Ring modes 5 and 6 for ray origin and hemisphere direction debug.
4. UI combo updates.
5. Build + smoke test.
6. Screenshot/readback evidence for m0/m5/m6.
7. Implementation doc with self-critique.
```

Stop before tracing or lighting.
