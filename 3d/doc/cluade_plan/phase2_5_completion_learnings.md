# Phase 2.5 Completion Learnings

**Date:** 2026-04-22  
**Branch:** 3d  
**Covers:** Phase 2.5-A (albedo volume), Phase 2.5-B (probe shadow ray), and all bugs surfaced during testing.

---

## What Was Implemented

### Phase 2.5-A — Albedo Volume

Added a second 3D texture (`albedoTexture`, `RGBA8`, same `volumeResolution` as SDF) written alongside the SDF in a single `sdf_analytic.comp` dispatch. Each voxel stores the color of the nearest analytic primitive (union-SDF closest-primitive tracking).

Binding table after Phase 2.5-A:

| Pass | Image/Sampler | Slot | Texture |
|---|---|---|---|
| SDF gen | image | 0 | sdfTexture (R32F) |
| SDF gen | image | 1 | albedoTexture (RGBA8) |
| Cascade update | sampler | 0 | sdfTexture |
| Cascade update | sampler | 1 | albedoTexture |
| Cascade update | image | 0 | probeGridTexture (RGBA16F) |
| Raymarch | sampler | 0 | sdfTexture |
| Raymarch | sampler | 1 | probeGridTexture |
| Raymarch | sampler | 2 | albedoTexture |

Key detail: **image units, sampler units, and SSBO binding points are three separate namespaces**. A `binding=1` SSBO and a `binding=1` image do not collide. Caused confusion early on but not a bug once understood.

### Phase 2.5-B — Probe Shadow Ray

Added `inShadow(vec3 hitPos, vec3 lightPos)` to `radiance_3d.comp`. 32-step sphere-march from the hit point toward the light. If SDF < 0.002 at any step, the diffuse term is zero.

```glsl
bool inShadow(vec3 hitPos, vec3 lightPos) {
    vec3  toLight = lightPos - hitPos;
    float dist    = length(toLight);
    vec3  dir     = toLight / dist;
    float t       = 0.05;  // offset to avoid self-intersection
    for (int i = 0; i < 32 && t < dist; ++i) {
        float d = sampleSDF(hitPos + dir * t);
        if (d < 0.002) return true;
        t += max(d * 0.9, 0.01);
    }
    return false;
}
```

Effect: probes in shadow regions become dark, giving the indirect lighting a non-uniform distribution rather than a uniform glow. This makes GI look more plausible but also makes the overall indirect contribution darker.

---

## Bugs Found During Phase 2.5 Testing

### Bug 1 — Cascade texture unbound when GI checkbox OFF

**Symptom:** Mode 3 (Indirect×5) showed black when "Cascade GI" checkbox was unchecked, even though the cascade had been computed and populated.

**Root cause:** The C++ code only bound `probeGridTexture` to sampler slot 1 when `useCascadeGI == true`. When GI was OFF, slot 1 was left unbound, so `uRadiance` sampled from texture object 0 (uninitialized) → black.

**Fix:** Always bind `probeGridTexture` to slot 1, unconditionally. The `uUseCascade` uniform controls whether mode 0 adds the indirect term — it should not control whether the texture is present in the sampler.

```cpp
// Wrong: conditional bind
if (useCascadeGI && cascades[0].active && ...) {
    glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);
    glUniform1i(..., "uUseCascade"), 1);
} else {
    glUniform1i(..., "uUseCascade"), 0);
}

// Correct: always bind, only control blending
if (cascades[0].active && cascades[0].probeGridTexture != 0) {
    glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);
}
glUniform1i(..., "uUseCascade"), useCascadeGI ? 1 : 0);
```

**Lesson:** Debug-mode texture reads and production-mode blending flags are separate concerns. Don't gate texture binding on feature flags.

### Bug 2 — Mode 3 showed normals fallback when GI OFF

**Symptom:** Switching GI OFF while in mode 3 displayed surface normals instead of probe data.

**Root cause:** The original mode 3 branch had an `else` path that rendered normals when `uUseCascade=false` as a "fallback". This was confusing because mode 3 is a debug view, not a production mode.

**Fix:** Mode 3 always samples `uRadiance` directly, ignoring `uUseCascade`:
```glsl
if (uRenderMode == 3) {
    vec3 uvw3 = (pos - uVolumeMin) / (uVolumeMax - uVolumeMin);
    vec3 indirect = texture(uRadiance, uvw3).rgb;
    fragColor = vec4(toneMapACES(indirect * 5.0), 1.0);
    return;
}
```

**Lesson:** Debug render modes should be unconditional. Production feature flags should not alter debug view behavior.

### Bug 3 — Mode 5 step heatmap appeared flat green

**Symptom:** Switching to mode 5 (step count heatmap) showed a uniform green image with no variation.

**Root cause:** The heatmap normalized by `uSteps = 256`. Inside a Cornell Box, rays typically hit surfaces in 5–25 steps. So `t5 = 5/256 ≈ 0.02`, always at the green extreme of the ramp.

**Fix:** Normalize against 32 (the realistic maximum hit-step budget for this scene), not `uSteps`:
```glsl
float t5 = clamp(float(stepCount) / 32.0, 0.0, 1.0);
```

**Lesson:** Heatmap normalization must be chosen relative to the expected signal range, not the theoretical budget. `uSteps` is a safety cap, not the typical case.

### Bug 4 — GI ON vs GI OFF difference not visible

**Symptom:** Mode 0 with GI ON looked nearly identical to mode 0 with GI OFF (or mode 4 direct-only).

**Root causes (combined):**
1. `raysPerProbe = 4` — too sparse. Each probe averaged only 4 Fibonacci directions, heavily diluting color signal.
2. `indirect * 0.3` blend — too low given the probe values (MaxLum ≈ 0.95 but mean was low due to shadow ray).
3. Shadow ray (Phase 2.5-B) made probe values darker across the board, reducing the already-low indirect contribution further.

**Fixes:**
- Raised `raysPerProbe` from 4 → 8
- Raised indirect blend from `0.3` → `1.0`

**Remaining concern:** With an isotropic probe (all directions averaged equally), the color-bleed signal is already diluted before blending. Even at 8 rays, directional information is lost. Addressing this properly requires storing directional radiance (spherical harmonics or per-direction bins), which is Phase 3 territory.

---

## Architecture Notes for Phase 3

### Probe stores outgoing, not incident, radiance

Current probes store `albedo * (diff * lightColor + ambient)` — the outgoing radiance of the first hit surface, averaged across all ray directions. This means:

- The probe at the center of the Cornell Box stores a mix of red, green, and white contributions
- When blended onto a white floor (`surfaceColor += indirect * 1.0`), the result is a faint uniform tint rather than directional color bleed
- Directional color bleed (red tint on floor near left wall only) requires the probe to know WHICH direction the red light came from

**Phase 3 should store incident radiance per direction** (e.g., SH2 = 9 coefficients, or 2D direction bins). Blending at the surface then evaluates the SH/bins in the surface normal direction.

### Cascade ready flag is per-static-frame, not per-feature

`cascadeReady` resets only when `!sdfReady` (scene changed). It does not reset when `raysPerProbe` or `useCascadeGI` changes. If you change probe count mid-run, you must manually invalidate the cascade (e.g., set `cascadeReady = false`). Currently not exposed in UI.

### Cornell Box probe near-wall is expected zero

The backwall probe sample (probePos = [16, 16, 1]) always returns near-zero because the probe is inside the wall geometry (SDF < 0 at probe center). The `sampleSDF` boundary check returns `INF` before any rays march. This is expected behavior, not a bug.

---

## Final State Checklist (Phase 2 + 2.5 Complete)

| Feature | Status |
|---|---|
| Analytic SDF (Cornell Box, boxes, spheres) | ✅ Working |
| Albedo volume (RGBA8, per-voxel material color) | ✅ Working |
| Probe grid (32³, 8 rays/probe, Fibonacci sphere) | ✅ Working |
| Probe shadow ray | ✅ Working |
| Mode 0: Final (direct + indirect blend) | ✅ Working |
| Mode 1: Surface normals | ✅ Working |
| Mode 2: Depth map | ✅ Working |
| Mode 3: Indirect×5 (always shows probe, GI-checkbox independent) | ✅ Working |
| Mode 4: Direct only (bypass cascade) | ✅ Working |
| Mode 5: Step count heatmap (normalized to 32) | ✅ Working |
| Radiance slice debug panel (top-right) | ✅ Working |
| Live probe stats (non-zero %, maxLum, meanLum, center/backwall) | ✅ Working |
| GI ON vs GI OFF visually distinguishable | ✅ Working (indirect * 1.0) |

**Probe Gate 1 result:** 77.7% non-zero, MaxLum = 0.95 ✅

---

## What Phase 3 Needs

- **Multi-cascade merging:** Cascade 0 → Cascade 1 propagation (interval doubling)
- **Directional probe storage:** SH2 or octahedral map per probe, not isotropic average
- **Temporal accumulation:** Reproject previous frame probes to reduce per-frame ray count
- **JFA-based SDF:** Replace analytic SDF with Jump Flooding for arbitrary mesh support
- **Indirect shadow:** Currently only direct light uses shadow rays; inter-bounce occlusion is not modeled
