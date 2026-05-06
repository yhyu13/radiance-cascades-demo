# Phase 7 — Analytic SDF Toggle: Implementation Notes

**Date:** 2026-05-01
**Branch:** 3d
**Status:** Implemented, pending visual experiment

---

## What was built

A UI checkbox "Analytic SDF (smooth, no grid)" that switches the raymarch fragment shader
between two SDF evaluation modes at runtime, without rebuilding the cascade or reloading
the scene.

| Mode | Uniform | Behavior |
|---|---|---|
| OFF (default) | `uUseAnalyticSDF = 0` | Texture lookup from precomputed 128³ R32F volume — trilinear interpolation between exact voxel-center samples |
| ON | `uUseAnalyticSDF = 1` | Evaluate `sdfBox`/`sdfSphere` directly per-sample in the fragment shader — no grid, no interpolation |

---

## Why this is a clean diagnostic

The current SDF pipeline:

```
sdf_analytic.comp  →  exact analytic distance at each of 128³ voxel centers
                   →  stored in R32F 3D texture
                   →  raymarch.frag reads via trilinear texture lookup
```

The texture lookup is analytically exact *at voxel centers* but piecewise-linear
*between* them. For flat Cornell Box walls the error is tiny — but the grid boundary
crossings are not zero, and they happen at 3.125 cm spacing (128³ over 4 m volume).

The analytic toggle bypasses the texture entirely. The fragment shader evaluates the
same `sdfBox`/`sdfSphere` math that the compute shader used to bake the texture — but
now per-sample, at arbitrary continuous positions.

**Experiment:** Run mode 5 (step heatmap) with toggle OFF, then ON.

- **Banding disappears or shrinks** → the 128³ grid boundary crossings were causing
  discrete step-count jumps. Increasing resolution further (or the toggle itself) is the
  fix path.
- **Banding stays identical** → the banding is intrinsic to the Cornell Box geometry and
  the integer `stepCount` variable, not the grid. The rectangular pattern is the natural
  iso-contour shape of a box-SDF; the discrete bands are from `stepCount` being an
  integer. No resolution increase can fix this — it is expected behavior.

Mode 7 (ray travel distance, continuous float) can be run alongside mode 5. If mode 7
is smooth where mode 5 is banded under *both* SDF modes, the diagnosis is confirmed:
pure integer quantization, not SDF structure.

---

## Implementation details

### Shader — `res/shaders/raymarch.frag`

**Version bump:** `#version 330 core` → `#version 430 core`
Required for Shader Storage Buffer Object (SSBO) support. The rest of the shader is
compatible with both versions; all compute shaders already use 4.3.

**SSBO declaration:**

```glsl
struct Primitive {
    int   type;
    float pad0, pad1, pad2;
    vec4  position;
    vec4  scale;
    vec4  color;
};
layout(std430, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
uniform int uPrimitiveCount;
uniform int uUseAnalyticSDF;
```

The struct layout matches `sdf_analytic.comp` exactly (including the three float pads
that align `position` to 16 bytes per std430 rules).

**Analytic evaluation functions:**

```glsl
float sdfBox(vec3 p, vec3 b) {
    vec3 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}
float sdfSphere(vec3 p, float r) { return length(p) - r; }

float sampleSDFAnalytic(vec3 worldPos) {
    float minDist = INF;
    for (int i = 0; i < uPrimitiveCount; ++i) {
        vec3 localPos = worldPos - primitives[i].position.xyz;
        float d = (primitives[i].type == 0)
            ? sdfBox(localPos, primitives[i].scale.xyz)
            : sdfSphere(localPos, primitives[i].scale.x);
        minDist = min(minDist, d);
    }
    return minDist;
}
```

**Modified `sampleSDF()`:**

```glsl
float sampleSDF(vec3 worldPos) {
    if (uUseAnalyticSDF != 0)
        return sampleSDFAnalytic(worldPos);
    // ... texture path unchanged
}
```

### C++ — `src/demo3d.cpp`

SSBO binding reuses the existing `primitiveSSBO` object. Binding point 0 is shared with
the compute pass — safe here because the compute shader is not running during the render
draw call. OpenGL SSBO bindings persist until explicitly changed, so one call suffices:

```cpp
glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, primitiveSSBO);
glUniform1i(glGetUniformLocation(prog, "uUseAnalyticSDF"),
            useAnalyticRaymarch ? 1 : 0);
glUniform1i(glGetUniformLocation(prog, "uPrimitiveCount"),
            static_cast<GLint>(analyticSDF.getPrimitiveCount()));
```

UI:

```cpp
ImGui::Checkbox("Analytic SDF (smooth, no grid)", &useAnalyticRaymarch);
```

Placed directly under the render mode radio buttons so it is always visible alongside
the diagnostic modes.

### `src/demo3d.h`

```cpp
bool useAnalyticRaymarch;  // default false
```

---

## Performance note

The analytic path loops over all primitives per sample (~8–12 for Cornell Box).
For the Cornell Box this is negligible. For denser scenes (Sponza with many pillars)
it would be slower. This is a diagnostic tool, not a production path.

---

## What this does NOT affect

- The cascade bake pass (`radiance_3d.comp`) still reads from the SDF texture.
  The toggle affects only the display-path raymarch, not the GI baking.
- The albedo texture lookup is unchanged — surface color still comes from the
  precomputed albedo volume.
- Modes 0–7 all route through `sampleSDF()`, so the toggle works in every mode.

---

## Expected next steps

1. Rebuild and open app
2. Switch to mode 5 (Steps)
3. Toggle "Analytic SDF (smooth, no grid)" ON/OFF
4. Observe whether the banding pattern changes
5. Document result → update `phase7_sdf_quantization_analysis.md` with confirmed hypothesis

| Result | Conclusion | Action |
|---|---|---|
| Banding disappears with analytic | Grid is the cause | Use analytic SDF permanently for display; consider higher-res texture for bake |
| Banding stays identical | Natural iso-contours + integer step quantization | Pursue mode 7 comparison; dithering is the only mitigation |
| Banding changes shape/intensity | Both causes contribute | Measure magnitude; decide if resolution increase is worth the memory |
