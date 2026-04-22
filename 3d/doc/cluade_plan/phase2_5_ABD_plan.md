# Phase 2.5 A/B/D Implementation Plan

**Date:** 2026-04-22  
**Branch:** 3d  
**Prerequisite:** Phase 2 gate passed — 77.7% non-zero probes, MaxLum=0.95 (confirmed 2026-04-22)

---

## Current State (from code reading)

| File | Relevant fact |
|---|---|
| `sdf_analytic.comp` | `Primitive.color` field exists (vec4) but is **never written** to any output — comment says "not used in SDF pass" |
| `sdf_analytic.comp` | `evaluateSDF()` tracks only `minDist`, not which primitive won — need to rewrite the loop in `main()` |
| `demo3d.h` | No `albedoTexture` field exists yet |
| `createVolumeBuffers()` | Creates `voxelGridTexture`, `sdfTexture`, 3× `*RadianceTexture` — no albedo texture |
| `sdfGenerationPass()` | Binds: SSBO at binding 0, `sdfTexture` at **image** binding 0. Albedo image binding 1 is free. |
| `updateSingleCascade()` | Binds: SDF at **sampler** GL_TEXTURE0 (`uSDF=0`), probe write at **image** binding 0. Sampler 1 is free for albedo. |
| `raymarchPass()` | Binds: SDF at sampler 0, cascade at sampler 1. Sampler **2 is free** for albedo. |
| `radiance_3d.comp` | `diff * uLightColor + vec3(0.02)` — gray, no albedo, no shadow |
| `raymarch.frag` | `diff * uLightColor + vec3(0.05)` — gray, no albedo |

---

## 2.5-A — Albedo Volume

**Estimated effort:** ~2h  
**Files touched:** `demo3d.h`, `demo3d.cpp` (3 functions), `sdf_analytic.comp`, `radiance_3d.comp`, `raymarch.frag`

### Step A1 — `demo3d.h`: add texture field

After the `sdfTexture` declaration (around line 595):
```cpp
/** Albedo/material color volume (RGBA8) — written by sdf_analytic.comp */
GLuint albedoTexture;
```

Also add `albedoTexture(0)` to the constructor initializer list.

### Step A2 — `demo3d.cpp` `createVolumeBuffers()`: create texture

After `sdfTexture = gl::createTexture3D(...)`:
```cpp
albedoTexture = gl::createTexture3D(
    volumeResolution, volumeResolution, volumeResolution,
    GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
```

### Step A3 — `demo3d.cpp` `destroyVolumeBuffers()` / destructor: delete texture

```cpp
if (albedoTexture) { glDeleteTextures(1, &albedoTexture); albedoTexture = 0; }
```

Check where `sdfTexture` is deleted and add alongside it.

### Step A4 — `sdf_analytic.comp`: write albedo alongside SDF

Add output binding:
```glsl
layout(rgba8, binding = 1) uniform image3D albedoVolume;
```

Rewrite `main()` to track the winning primitive inline (the `evaluateSDF()` helper returns only distance — can't recover which primitive won after the call):

```glsl
void main() {
    ivec3 coord = ivec3(gl_GlobalInvocationID);
    ivec3 size  = imageSize(sdfVolume);
    if (any(greaterThanEqual(coord, size))) return;

    vec3 uvw      = (vec3(coord) + 0.5) / vec3(size);
    vec3 worldPos = volumeOrigin + uvw * volumeSize;

    float minDist     = 1e10;
    vec3  closestColor = vec3(0.5);  // fallback gray

    for (int i = 0; i < primitiveCount; ++i) {
        Primitive prim = primitives[i];
        vec3 localPos  = worldPos - prim.position.xyz;

        float dist;
        if (prim.type == 0)
            dist = sdfBox(localPos, prim.scale.xyz);
        else
            dist = sdfSphere(localPos, prim.scale.x);

        if (dist < minDist) {
            minDist      = dist;
            closestColor = prim.color.rgb;
        }
    }

    imageStore(sdfVolume,    coord, vec4(minDist, 0.0, 0.0, 0.0));
    imageStore(albedoVolume, coord, vec4(closestColor, 1.0));
}
```

**Note:** Remove or leave the `evaluateSDF()` helper function — it is now unused. Removing it is cleaner.

### Step A5 — `demo3d.cpp` `sdfGenerationPass()`: bind albedo image

After `glBindImageTexture(0, sdfTexture, ...)`:
```cpp
glBindImageTexture(1, albedoTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
```

Also update the `glMemoryBarrier` to include the albedo write:
```cpp
glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
// (already covers both image bindings — no change needed)
```

### Step A6 — `radiance_3d.comp`: sample albedo at hit point

Add sampler declaration (alongside `uSDF`):
```glsl
uniform sampler3D uAlbedo;
```

In `raymarchSDF()`, after computing `n` and `lightDir`:
```glsl
// Sample material albedo from volume
vec3 uvw   = (pos - uGridOrigin) / uGridSize;
vec3 albedo = texture(uAlbedo, uvw).rgb;

float diff  = max(dot(n, lightDir), 0.0);
vec3 color  = albedo * (diff * uLightColor + vec3(0.02));
return vec4(color, 1.0);
```

### Step A7 — `demo3d.cpp` `updateSingleCascade()`: bind albedo sampler

After the SDF sampler bind:
```cpp
glActiveTexture(GL_TEXTURE1);
glBindTexture(GL_TEXTURE_3D, albedoTexture);
glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 1);
```

### Step A8 — `raymarch.frag`: sample albedo at hit point

Add uniform:
```glsl
uniform sampler3D uAlbedo;
```

In the hit block (mode 0 and mode 4), at surface hit:
```glsl
vec3 uvw    = (pos - uVolumeMin) / (uVolumeMax - uVolumeMin);
vec3 albedo = texture(uAlbedo, uvw).rgb;

vec3 lightDir    = normalize(uLightPos - pos);
float diff       = max(dot(normal, lightDir), 0.0);
vec3 surfaceColor = albedo * (diff * uLightColor + vec3(0.05));

if (uUseCascade) {
    vec3 indirect = texture(uRadiance, uvw).rgb;
    surfaceColor += indirect * 0.3;  // indirect is already albedo-weighted from probes
}
```

**Note:** Mode 4 (direct-only) also benefits from albedo — apply the same sample there.

### Step A9 — `demo3d.cpp` `raymarchPass()`: bind albedo sampler

After the cascade sampler bind block:
```cpp
glActiveTexture(GL_TEXTURE2);
glBindTexture(GL_TEXTURE_3D, albedoTexture);
glUniform1i(glGetUniformLocation(prog, "uAlbedo"), 2);
```

Bind it unconditionally (even when cascade is OFF) since direct lighting also needs albedo.

### Acceptance for A

With Cornell Box loaded:
- Left wall = red, right wall = green in both mode 0 and mode 4
- Probe readback: Center sample should now show a warm/neutral value (room averages gray+red+green ≈ neutral), but backwall and side probes should tint red/green depending on their nearest wall
- Radiance slice viewer: should show distinct color regions across the 32×32 grid

---

## 2.5-B — Probe Shadow Ray

**Estimated effort:** ~30 min  
**Files touched:** `radiance_3d.comp` only

Add `inShadow()` before `raymarchSDF()`:

```glsl
bool inShadow(vec3 hitPos, vec3 lightPos) {
    vec3 toLight = lightPos - hitPos;
    float dist   = length(toLight);
    vec3  dir    = toLight / dist;
    float t      = 0.05;  // start offset avoids self-intersection
    for (int i = 0; i < 32 && t < dist; ++i) {
        float d = sampleSDF(hitPos + dir * t);
        if (d < 0.002) return true;
        t += max(d * 0.9, 0.01);
    }
    return false;
}
```

In `raymarchSDF()`, replace:
```glsl
float diff = max(dot(n, lightDir), 0.0);
```
with:
```glsl
float diff = inShadow(pos, uLightPos) ? 0.0 : max(dot(n, lightDir), 0.0);
```

**Do after 2.5-A.** Without albedo the visual effect of shadows is hard to judge (gray-on-gray). With albedo, shadow regions become visually distinct.

### Acceptance for B

- Mode 3 (Indirect×5) with Cornell Box: the two interior boxes should cast a darker shadow footprint on floor/ceiling — visible as darker patches in the probe radiance distribution
- The dark patches were absent before (uniform probe brightness)

---

## 2.5-D — Visual Smoke Test

**Estimated effort:** 10 min (screenshots + comparison)  
**No code change.** This is a procedure gate.

### Procedure

1. Load Cornell Box (click "Cornell Box" in the Quick Start panel)
2. Verify cascade has re-run (Cascade panel shows updated probe stats)
3. Set render mode to 0 (Final)
4. **Cascade GI OFF:** Take screenshot → `doc/screenshots/phase2_smoke_A_direct_only.png`
5. **Cascade GI ON:** Take screenshot → `doc/screenshots/phase2_smoke_B_with_gi.png`
6. Compare: A/B must show a visible brightness or color difference

### What to look for

| Observation | Meaning |
|---|---|
| B is brighter overall than A | Indirect bounce is being added — basic GI works |
| B shows reddish tint on gray surfaces near left wall | Color bleed from red wall reaching probe grid — albedo + cascade working together |
| B and A are identical | `uUseCascade` not reaching shader OR probe texture all-zero after scene switch |
| B is **darker** than A | Indirect term negative — unlikely but would mean probe stores negative values |

### Required outcome

A visible difference between A and B. Color bleed preferred but not required — even a uniform brightness lift in B vs A is sufficient to pass.

If no difference: raise `raysPerProbe` from 4 → 8 in `initCascades()`, reload shaders or restart, repeat.

---

## Implementation Order

```
A1 → A2 → A3   (header + buffer setup — compile-test, no visual change yet)
A4              (shader rewrite — must match A5 binding before running)
A5              (cpp binding — can now test albedo generation)
A6 → A7        (probe shader + cpp bind — now probes have color)
A8 → A9        (raymarch shader + cpp bind — now final image has color)
  → Visual check: Cornell Box walls red/green?
B               (shadow ray — 30 min, single shader file)
  → Visual check: mode 3 shows shadow patches?
D               (smoke test screenshots)
```

---

## Binding Reference Table

| Pass | Sampler/Image | Unit | Texture | Shader Uniform |
|---|---|---|---|---|
| SDF gen | image | 0 | `sdfTexture` | `sdfVolume` |
| SDF gen | image | 1 | `albedoTexture` | `albedoVolume` ← NEW |
| Cascade update | sampler | 0 | `sdfTexture` | `uSDF` |
| Cascade update | sampler | 1 | `albedoTexture` | `uAlbedo` ← NEW |
| Cascade update | image | 0 | `probeGridTexture` | `oRadiance` |
| Raymarch | sampler | 0 | `sdfTexture` | `uSDF` |
| Raymarch | sampler | 1 | `probeGridTexture` | `uRadiance` |
| Raymarch | sampler | 2 | `albedoTexture` | `uAlbedo` ← NEW |

No binding conflicts. SSBO binding 0 (primitives) is a separate namespace from image/sampler bindings.

---

## What 2.5 Does NOT Include

- Shadow ray in `raymarch.frag` primary rays (separate task, lower priority)
- Temporal reprojection of albedo (Phase 4)
- Per-voxel albedo from OBJ mesh textures (requires JFA SDF track)
- 2.5-C status cleanup (stale TODO comments) — low priority, separate commit
