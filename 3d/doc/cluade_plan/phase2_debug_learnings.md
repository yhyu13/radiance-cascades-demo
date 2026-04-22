# Phase 2 Debug Views — Implementation Learnings

**Date:** 2026-04-22  
**Commit:** 5aaa970  
**Context:** Recap of what was built, what each tool reveals, and what the signals mean for Phase 2.5.

---

## What Was Built (vs the Plan)

| Planned Tool | Implemented As | Notes |
|---|---|---|
| CPU readback (Tool 1) | Live probe stats in Cascades panel | Runs once per cascade update; re-triggers on scene switch. Replaces one-time static dump. |
| Normal debug (Tool 2 mode 1) | `uRenderMode=1` (already existed) | No change needed |
| SDF dist debug (Tool 2 mode 2) | Changed to **depth map** (near=white, far=dark) | SDF-at-surface was always ≈0 → black everywhere; depth map is more useful |
| Indirect×5 debug (Tool 2 mode 3) | `uRenderMode=3` (already existed) | Works as planned |
| Probe slice view (Tool 3) | `renderRadianceDebug()` using `radiance_debug.frag` | Existing shader already had 4 modes (Slice/MaxProj/Average/Direct); only GL draw call was missing |
| **NEW:** Direct-only mode | `uRenderMode=4` | Direct shading with cascade bypassed; useful A/B without touching UI checkbox |
| **NEW:** Step-count heatmap | `uRenderMode=5` | Green→yellow→red, normalized to `uSteps`; shows ray budget usage per pixel |
| **NEW:** SDF normals via [M] | `sdfVisualizeMode % 4` (was `% 3`) | Mode 3 = surface normals now accessible |

**Cascade panel now shows live:**
```
Non-zero: X / 32768 (Y%)
MaxLum:   Z
MeanLum:  W
Center:   (R, G, B)
Backwall: (R, G, B)
```

---

## Architecture Insights From Implementation

### Main loop order: update() → render() → renderUI()
UI button clicks land in `renderUI()`, one frame after `render()`. This is why the `sceneDirty` flag must be consumed in `render()`, not `update()`. The `probeDumped` reset (`if (!cascadeReady) probeDumped = false;`) exploits this same ordering: cascade-ready transitions to false in render() on the same frame the scene switch fires.

### radiance_debug.vert uses gl_VertexID lookup with 4 entries
The vertex shader defines `vec2 positions[4]` and reads `positions[gl_VertexID]`. `glDrawArrays(GL_TRIANGLES, 0, 6)` with `debugQuadVAO` invokes it 6 times — indices 4 and 5 are technically OOB but GLSL clamps on this GPU. Works identically to `sdf_debug.vert`. Both shaders ignore the bound VBO attribute; they generate positions procedurally.

### Viewport placement: SDF debug = top-left, Radiance debug = top-right
Both are 400×400. `renderSDFDebug()`: `glViewport(0, viewport[3]-400, 400, 400)`. `renderRadianceDebug()`: `glViewport(viewport[2]-400, viewport[3]-400, 400, 400)`. No overlap when both enabled simultaneously.

### Mode 2 was changed: SDF-at-hit → depth map
The original plan said mode 2 = "SDF dist at hit point" (multiplied ×20 for visibility). At the hit point, SDF ≈ EPSILON ≈ 1e-6, so the image was always black. Replaced with `(t - tNear) / (tFar - tNear)` inverted → near=white, far=dark. This is more useful for diagnosing where rays terminate.

---

## Diagnostic Signals: What to Read Before Phase 2.5

### Signal 1 — Probe readback (Cascades panel)

| Value | Healthy | Problem |
|---|---|---|
| Non-zero % | > 30% (≈10 000/32 768) | < 5% → dispatch wrote almost nothing → check `imageStore` binding |
| MaxLum | > 0.3 | ≈ 0 → light doesn't reach any probe |
| MeanLum | 0.05 – 0.15 | = MaxLum → uniform write (SDF UV wrong, all probes same) |
| Center RGB | (0.04–0.15, 0.04–0.15, 0.04–0.15) | All zero → center of room sees no radiance |
| Backwall RGB | > 0.3 on at least one channel | Zero → back wall not illuminated by probe rays |

### Signal 2 — Radiance slice viewer (top-right panel)

Enable "Radiance Debug" toggle. Set mode = Slice, Z-axis, position = 0.5 (horizontal mid-slice).

| Expected | Problem if absent |
|---|---|
| Non-uniform brightness across the 32×32 grid | Uniform (all same) → SDF sampling returns constant → `uGridOrigin`/`uGridSize` wrong |
| Brighter cells near top (ceiling/light) | All dark → probe rays hit nothing (SDF zero everywhere) |
| Gradual falloff away from walls | Noisy salt-and-pepper → Fibonacci sphere has bug |

### Signal 3 — Mode 3 (Indirect×5)

With cascade GI OFF → all black (or normal map fallback).  
With cascade GI ON → should see low-frequency warm blobs.

If ON/OFF looks identical: `uUseCascade` bool uniform not reaching shader — use mode 4 (Direct only) vs mode 0 (Final) to confirm the path.

### Signal 4 — Mode 4 (Direct only) vs Mode 0 (Final)

Mode 4 = pure diffuse + ambient, no cascade.  
Mode 0 = diffuse + ambient + `indirect * 0.3`.  
These should look **slightly different** if the cascade has non-zero data.  
If identical: the `indirect * 0.3` term is being added but indirect is zero → probe texture empty (diagnose via Signal 1).

### Signal 5 — Mode 5 (Step heatmap)

Green everywhere in air, yellow/red at geometry edges = normal (sphere-march is efficient).  
Red everywhere = raymarcher is hitting the step budget in open air = SDF values too small = SDF is wrong.  
All green with no red = no geometry hit (SDF is all-interior or all-exterior flat).

---

## What the Debug Views Reveal About Phase 2.5 Gaps

### Gap 1 — No material albedo (walls are gray regardless of color)

Mode 1 (Normals) will show correct per-face colors, but mode 0 (Final) and mode 4 (Direct only) will render all surfaces the same gray. The probe texture also stores only luminance, not color — so the radiance slice will appear gray/white regardless of which wall a probe sees.

**Confirmation test:** Switch Cornell Box scene. Mode 4 shows gray left wall (should be red) and gray right wall (should be green). This confirms albedo is missing.

**Phase 2.5-A fix:** albedo volume (`RGBA8` texture, written by `sdf_analytic.comp`, sampled in `raymarch.frag` and `radiance_3d.comp`).

### Gap 2 — No probe shadow ray (probes over-bright in shadow)

Indirect×5 mode will show a roughly uniform glow rather than darkened shadow regions. Probes inside geometry shadow will accumulate the same direct light as lit probes because `radiance_3d.comp` uses only `max(dot(n,l), 0.0)` without a visibility ray.

**Confirmation test:** Mode 3 (Indirect×5) with Cornell Box — the two interior boxes should cast distinct shadow regions on the floor and ceiling. If the floor behind a box is equally bright as the exposed floor, shadow ray is missing.

**Phase 2.5-B fix:** `inShadow()` function in `radiance_3d.comp`, 32-step shadow march toward the light.

---

## Pre-Phase-2.5 Checklist

Run the app, check all 5 signals. Record values here before starting 2.5-A.

| Signal | Target | Actual (fill in) |
|---|---|---|
| Probe non-zero % | > 30% | |
| Probe maxLum | > 0.3 | |
| Mode 3 distinct from OFF | Yes (any visible diff) | |
| Mode 4 ≠ Mode 0 (any diff) | Yes | |
| Mode 5 shows red at walls | Yes | |
| Cornell Box walls are gray (confirms albedo gap) | Yes (expected) | |

If first 5 pass: Phase 2 is visually confirmed. Start 2.5-A (albedo).  
If any of first 5 fail: fix the cascade dispatch first — Phase 3 will not work either.
