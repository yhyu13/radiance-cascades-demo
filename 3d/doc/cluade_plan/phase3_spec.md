# Phase 3 Spec — Multi-Level Cascade Hierarchy

**Date:** 2026-04-22  
**Branch:** 3d  
**Prerequisite:** Phase 2.5 must be visually confirmed first (see blocking items below)

---

## Part 0 — Completion Audit (is Phase 2.5 done?)

### What the recommendation required (06_updated_recommendation.md + 03_action_plan.md)

| Item | Required By | Status |
|------|-------------|--------|
| Fix UI redundancy / confusing labels | Phase 2.5 | **Done** (commit 0b8c685) |
| Performance metrics checkbox works | Phase 2.5 | **Done** (commit 0b8c685) |
| RC debug render modes visually distinct | Phase 2.5 | **Done** (commit 0b8c685) |
| SDF debug colorized, sane default | Phase 2.5 | **Done** (commit 0b8c685) |
| **Albedo/material color volume** | Phase 2.5-A | ❌ Not started |
| **Probe-side shadow ray** | Phase 2.5-B | ❌ Not started |
| Status drift cleanup (TODO comments, dead code) | Phase 2.5-C | ❌ Not started |
| Visual A/B smoke test (screenshots) | Gate for Phase 3 | ❌ Blocked on A+B |
| Shadow ray in primary rays (raymarch.frag) | After smoke test | ❌ Blocked |
| raysPerProbe 4 → 8 | After smoke test | ❌ Blocked |

**Verdict:** The UI/debug half of Phase 2.5 is complete. The rendering half (albedo, shadows, smoke test) is not done. **Phase 3 is blocked.**

### Remaining Phase 2.5 work (do these before Phase 3)

#### 2.5-A — Albedo/material color (highest priority)

Without this, walls are all gray regardless of primitive color — there is no visible red/green Cornell Box. This is the single change with the largest visible impact.

**Files to change:**

1. **`demo3d.h`**: Add `GLuint albedoTexture;`

2. **`demo3d.cpp` `createVolumeBuffers()`**: Create albedo texture  
   ```cpp
   albedoTexture = gl::createTexture3D(
       volumeResolution, volumeResolution, volumeResolution,
       GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
   ```

3. **`sdf_analytic.comp`**: Add second image3D output for albedo. For each voxel, write the color of the nearest primitive (use same loop, track which primitive gave minimum distance).  
   ```glsl
   layout(rgba8, binding = 1) uniform image4D albedoVolume;
   // After minDist loop, write color of closest primitive
   imageStore(albedoVolume, coord, vec4(closestColor, 1.0));
   ```

4. **`raymarch.frag`**: Add `uniform sampler3D uAlbedo;`. At surface hit:  
   ```glsl
   vec3 uvw = (pos - uVolumeMin) / (uVolumeMax - uVolumeMin);
   vec3 albedo = texture(uAlbedo, uvw).rgb;
   vec3 surfaceColor = albedo * (diff * uLightColor + vec3(0.05));
   ```

5. **`radiance_3d.comp`**: Same albedo sampling at hit point in `raymarchSDF()`.

6. **`demo3d.cpp` `sdfGenerationPass()`**: Bind albedo texture as image binding 1 alongside SDF.

7. **`demo3d.cpp` `raymarchPass()`**: Bind albedo texture to sampler slot 2, pass `uAlbedo`.

#### 2.5-B — Probe shadow ray (radiance_3d.comp only)

Prevents bright lit indirect bounce from geometry that should be in shadow. Do after albedo so you can see the effect.

```glsl
bool inShadow(vec3 hitPos, vec3 lightPos) {
    vec3 toLight = lightPos - hitPos;
    float dist = length(toLight);
    vec3 dir = toLight / dist;
    float t = 0.05;
    for (int i = 0; i < 32 && t < dist; ++i) {
        float d = sampleSDF(hitPos + dir * t);
        if (d < 0.002) return true;
        t += max(d * 0.9, 0.01);
    }
    return false;
}
// In raymarchSDF(), replace diff line:
float diff = inShadow(pos, uLightPos) ? 0.0 : max(dot(n, lightDir), 0.0);
```

#### 2.5-C — Status drift cleanup

1. `renderTutorialPanel()`: replace `✗ SDF generation (placeholder)` and `✗ Full raymarching (placeholder)` with accurate bullets
2. `injectDirectLighting()`: delete ~80 lines of dead code after `return;`, leave one comment
3. Destructor: remove `// TODO: Implement destructor` (body already exists)
4. `destroyCascades()`: remove stale `// TODO: Implement cascade cleanup`

#### 2.5-D — Visual smoke test (gate)

Run the app and capture two screenshots:
1. **Cascade GI OFF** — direct light only
2. **Cascade GI ON** — with indirect bounce

The toggle must produce a visible difference (color bleed from red/green walls onto center boxes). If not visible, raise `raysPerProbe` from 4 → 8 in `initCascades()` and repeat.

---

## Part 1 — Phase 3 Scope

Phase 3 implements the "cascade" structure that Phase 2 skips: **a hierarchy of probe grids** where each level covers a larger spatial interval and samples from the level above it.

### Goal

After Phase 3 the image should show:
- Correct multi-bounce indirect light that reaches geometry too far from the light for a single probe level to capture
- Visible qualitative difference between 1-level (Phase 2) and 2-level (Phase 3) GI, especially in shadow areas

### What Phase 3 does NOT include

- Temporal reprojection (Phase 4)
- OBJ mesh / JFA SDF (separate track)
- Dynamic scenes / per-frame cascade update (requires temporal)

---

## Part 2 — Cascade Hierarchy Architecture

### Interval structure

In radiance cascades, each level covers a ray interval `[t_start, t_end)`. Cascade 0 (finest) covers near-field; cascade 1 covers mid-field; and so on. The merge equation is:

```
L_probe[c] = local_radiance + (1 - local_hit_alpha) * L_probe[c+1] sampled at probe position
```

Each level has:
- **Same probe resolution** (32³ in our case — trade resolution for interval coverage)
- **Larger cell size** (cascade 1 cell = 2× cascade 0, so cascade 1 covers 2× the world extent per probe)
- **More rays** (cascade 1 = 4× rays of cascade 0, to sample the wider interval uniformly)
- **Different interval** (cascade 1 starts where cascade 0 ends)

### Two-level design (Phase 3 target)

| Level | Probes | Cell size | Rays/probe | Interval |
|-------|--------|-----------|-----------|----------|
| 0 (Phase 2) | 32³ | 0.125 | 4 | [0, √3 × 4] |
| 1 (Phase 3) | 32³ | 0.25 | 16 | [√3 × 4, √3 × 8] |

Level 1 uses 4× the rays to maintain uniform angular density over the larger interval. The interval end for level 0 becomes the interval start for level 1.

---

## Part 3 — Implementation Plan

### Step 3-A — Generalize cascade initialization

**File:** `demo3d.cpp` `initCascades()`

Change from single fixed cascade to N-level loop:

```cpp
void Demo3D::initCascades() {
    cascadeCount = 2;  // Phase 3: two levels

    const int probeRes = 32;
    // Level 0: finest, same as Phase 2
    float cellSz0 = volumeSize.x / float(probeRes);
    cascades[0].initialize(probeRes, cellSz0, volumeOrigin, 4);
    cascades[0].intervalStart = 0.0f;
    cascades[0].intervalEnd   = length(volumeSize);  // diagonal

    // Level 1: double cell size, 4× rays
    float cellSz1 = cellSz0 * 2.0f;
    cascades[1].initialize(probeRes, cellSz1, volumeOrigin, 16);
    cascades[1].intervalStart = cascades[0].intervalEnd;
    cascades[1].intervalEnd   = cascades[0].intervalEnd * 2.0f;
}
```

Add `intervalStart` and `intervalEnd` to `RadianceCascade3D` struct in `demo3d.h` (already declared but unused).

### Step 3-B — Update cascade dispatch (updateSingleCascade)

**File:** `demo3d.cpp` `updateSingleCascade()`

Pass interval bounds to shader. Update in reverse order (coarser first, then fine reads from coarse):

```cpp
// In updateRadianceCascades(): iterate coarse→fine
for (int i = cascadeCount - 1; i >= 0; --i) {
    updateSingleCascade(i);
}

// In updateSingleCascade(): add interval uniforms + upper cascade sampler
glUniform1f(glGetUniformLocation(prog, "uIntervalStart"), c.intervalStart);
glUniform1f(glGetUniformLocation(prog, "uIntervalEnd"),   c.intervalEnd);

// Bind upper cascade for merging (if not the coarsest level)
if (cascadeIndex + 1 < cascadeCount && cascades[cascadeIndex + 1].active) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, cascades[cascadeIndex + 1].probeGridTexture);
    glUniform1i(glGetUniformLocation(prog, "uUpperCascade"), 1);
    glUniform1i(glGetUniformLocation(prog, "uHasUpperCascade"), 1);
} else {
    glUniform1i(glGetUniformLocation(prog, "uHasUpperCascade"), 0);
}
```

### Step 3-C — Update radiance_3d.comp

**File:** `res/shaders/radiance_3d.comp`

Key changes:
1. Add `uIntervalStart`, `uIntervalEnd` uniforms
2. Add `uUpperCascade` sampler3D + `uHasUpperCascade` bool
3. Cap ray march to `[uIntervalStart, uIntervalEnd]`
4. After local march: if no hit within interval AND upper cascade exists, sample upper cascade at the probe world position for the "far field" contribution

```glsl
uniform float uIntervalStart;
uniform float uIntervalEnd;
uniform sampler3D uUpperCascade;
uniform bool uHasUpperCascade;

// In main():
for (int i = 0; i < uRaysPerProbe; ++i) {
    vec3 rayDir = getRayDirection(i);
    vec4 hit = raymarchSDF(worldPos, rayDir, uIntervalStart, uIntervalEnd);
    if (hit.a > 0.0) {
        totalRadiance += hit.rgb;
    } else if (uHasUpperCascade) {
        // No hit in this interval — sample upper cascade for far-field
        vec3 uvw = (worldPos - uGridOrigin) / uGridSize;
        totalRadiance += texture(uUpperCascade, uvw).rgb;
    }
}
```

### Step 3-D — Update raymarch.frag to use cascade 0 (no change needed)

The primary ray shader already samples `cascades[0].probeGridTexture` as `uRadiance`. With a proper multi-level cascade, cascade 0 now contains merged radiance (near-field + far-field via cascade 1), so the final image automatically benefits without shader changes. Verify with side-by-side comparison.

### Step 3-E — Update UI

**File:** `demo3d.cpp` `renderSettingsPanel()`

Replace the hardcoded probe info line:
```cpp
// Old:
ImGui::Text("Probe grid: 32^3, rays/probe: %d", cascades[0].raysPerProbe);
// New:
for (int i = 0; i < cascadeCount; ++i) {
    ImGui::Text("  Level %d: 32^3, rays=%d, interval=[%.1f, %.1f]",
        i, cascades[i].raysPerProbe,
        cascades[i].intervalStart, cascades[i].intervalEnd);
}
```

---

## Part 4 — Acceptance Criteria

Phase 3 is complete when:

1. **Build passes** with `cascadeCount = 2` and both cascade textures allocated
2. **Level 1 dispatch runs** without GL errors (check console output)
3. **Visual test A**: Cornell Box with Cascade GI ON shows more fill light in shadow areas compared to Phase 2 (1-level)
4. **Visual test B**: Toggling `useCascadeGI` ON/OFF produces a visible brightness/color difference — same as Phase 2 gate
5. **Debug mode 3** ("Indirect\*5") shows non-black values throughout the room, not just near the light

---

## Part 5 — What Phase 3 Still Will NOT Have

- Temporal reprojection (probes re-used across frames)
- Dynamic scene updates (cascade re-dispatches per frame)
- More than 2 cascade levels
- Cone-angle filtering between cascade levels (the actual RC angular interval math)

These are Phase 4+. Phase 3 only adds the second level and the merge step.
