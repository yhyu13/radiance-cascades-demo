# ShaderToy2 Phase 2B-6 Plan — Room-only Direct Radiance Atlas, No Feedback

**Date:** 2026-05-29  
**Status:** Plan + self-critique, ready for implementation  
**Scope:** Write single-frame direct-light radiance atlas for classified room-plane hits only. Skip unknown/box hits. No feedback, no accumulation, no final GI lookup.

---

## 1. Goal

Implement a diagnostic direct-radiance atlas that writes shadowed direct lighting to the surface atlas for room-plane hits only:

```text
classified hit point + normal + light direction
  -> compute direct radiance (Lambertian)
  -> binary shadow visibility via SDF trace
  -> write shadowed direct to atlas texture
  -> visualize as debug mode
```

This is the first step toward persistent feedback but remains **single-frame only**. It validates that direct lighting can be computed and written to correct chart locations before adding temporal accumulation.

---

## 2. Non-Goals

Do **not** implement:

```text
- previous-frame sampling / temporal accumulation
- persistent ping-pong feedback loop
- multi-bounce closure
- box/object chart support (short_box/tall_box remain unknown)
- upper cascade merge
- final raymarch surface GI lookup
- EXR/PT quality metrics
- soft/cone shadows (binary visibility only)
```

Reason:

```text
Direct radiance atlas must be validated on room planes before adding feedback complexity.
Box geometry limitation remains active until Phase 2C.
```

---

## 3. Implementation Plan

### 3.1 Add new debug modes

Current radiance debug modes:

```text
0  ray origin
1  hemisphere direction
2  normal
3  active/chart mask
4  trace classification
5  trace distance
6  hit chart id
7  hit chart uv
8  UV round-trip test
9  hit normal
10 unshadowed direct
11 NdotL
12 trace state (with escapes)
13 shadow visibility
14 shadowed direct
```

Add:

```text
15 direct radiance atlas write (single frame, no accumulation)
16 atlas visualization (read back from atlas texture)
```

Mode 15 writes to atlas:

```glsl
if (tr.state == 1 && hs.valid) {
    // Compute direct radiance at hit point
    vec3 L = uLightPos - tr.pos;
    float dist2 = max(dot(L, L), 1e-4);
    vec3 lightDir = normalize(L);
    float ndotl = max(dot(hitNormal, lightDir), 0.0);
    vec3 direct = uLightColor * ndotl / dist2;
    
    // Binary shadow check
    float visibility = shadowVisibility(tr.pos, lightDir, sqrt(dist2));
    vec3 shadowedDirect = direct * visibility;
    
    // Write to atlas at probeUVChart location
    imageStore(oRadianceDebug, p, vec4(shadowedDirect, 1.0));
} else {
    // Clear non-hit texels to black
    imageStore(oRadianceDebug, p, vec4(0.0));
}
```

Mode 16 visualizes atlas content:

```glsl
// Read from atlas texture instead of computing
vec4 atlasSample = texture(uRadianceAtlas, uv);
rgb = atlasSample.rgb;
a = atlasSample.a;
```

### 3.2 Add atlas texture binding

Update shader uniforms:

```glsl
uniform sampler2D uRadianceAtlas;  // For mode 16 readback
```

Update C++ dispatch to bind atlas texture when needed.

### 3.3 C++ updates

Update:

```cpp
SurfaceRC::setRadianceDebugMode clamp 0..16
SurfaceRC::radianceDebugModeName add "direct atlas write", "atlas readback"
ImGui radianceModes[] extend list
```

CLI already supports numeric mode.

### 3.4 Verification

Build:

```powershell
cmake --build build --config Debug
```

Captures:

```text
tools/phase2b6_visual/m15_atlas_write.png
tools/phase2b6_visual/m16_atlas_readback.png
```

Structure checks:

```text
- Chart 6 remains inactive (inactiveBright=0)
- Mode 15 has nonzero pixels in classified regions only
- Mode 15 has zero pixels in unknown/yellow regions (skipped)
- Mode 16 matches mode 15 (write/read consistency)
- Brightness distribution plausible (brighter near light, darker in shadow)
```

Suggested pixel-count buckets for mode 15:

```text
nonzero count (should match classified hits from mode 6)
bright count (> 0.1 luminance)
dark count (< 0.01 luminance, likely shadowed)
unknown skipped count (should equal yellow count from mode 6)
```

---

## 4. Stop-Loss

```text
maximum: 2 implementation attempts + 1 diagnostic-only round
if atlas write produces all-black or all-white, stop and diagnose before adding feedback
if mode 16 doesn't match mode 15, stop and fix texture binding/sampling
```

---

## 5. Self-Critique of This Plan

### SC1 — Single-frame atlas has no temporal smoothing

Accepted. This is diagnostic-only. Temporal accumulation comes later with feedback. Expect some noise/graininess in atlas due to per-probe variance.

Improvement:

```text
Document that mode 15/16 are single-frame diagnostics. Do not judge quality by smoothness.
Judge by correctness: bright where expected, dark in shadow, zero on unknowns.
```

### SC2 — Box hits still produce no atlas contribution

Accepted. This is the explicit limitation. Unknown hits (yellow in mode 6) will have zero radiance in atlas.

Improvement:

```text
Track unknown count explicitly. If > 15% of probes are unknown, strongly recommend Phase 2C before feedback.
Current unknown rate is ~12% (from Phase 2B-3).
```

### SC3 — Binary shadow may over-block near surfaces

Accepted. Same limitation as Phase 2B-5. Conservative UDF causes false occlusion.

Improvement:

```text
Compare mode 10 (unshadowed) vs mode 15 (shadowed) to quantify shadow blocking.
If > 50% of visible direct is blocked, investigate shadow bias or UDF quality.
```

### SC4 — Atlas resolution may not match probe density

Risk: Writing to atlas at probe resolution but reading at different resolution could cause aliasing.

Improvement:

```text
Ensure mode 15 writes at exact atlas texel coordinates (p.x, p.y).
Mode 16 reads with bilinear filtering (default sampler2D behavior).
Verify no moiré patterns in mode 16.
```

### SC5 — No validation that atlas write location matches chart UV

Risk: If probeUVChart is wrong, direct radiance writes to wrong atlas location.

Improvement:

```text
Use mode 8 (UV round-trip test) before mode 15 to verify UV correctness.
If mode 8 shows errors, fix UV mapping before writing atlas.
```

---

## 6. Improved Final Plan

Implement exactly:

```text
1. Add mode 15: compute shadowed direct and write to atlas (single frame).
2. Add mode 16: read back from atlas texture for visualization.
3. Update C++ clamp/labels to 0..16.
4. Build + capture m15/m16.
5. Run structure checks: nonzero count, bright/dark distribution, Chart 6 inactive.
6. Compare mode 10 vs 15 to quantify shadow blocking.
7. Document implementation + self-critique.
```

Stop before feedback, accumulation, or box chart support.

---

## 7. Success Criteria

Phase 2B-6 succeeds if:

```text
✓ Mode 15 writes nonzero radiance to classified room-plane regions
✓ Mode 15 writes zero radiance to unknown/box regions
✓ Mode 16 reads back matching content from atlas
✓ Chart 6 remains inactive (no writes to front wall)
✓ Shadow blocking is plausible (not all-black, not all-visible)
✓ Pixel counts consistent with mode 6 classification
```

Phase 2B-6 fails if:

```text
✗ Mode 15 produces all-black atlas (lighting computation broken)
✗ Mode 15 produces all-white atlas (normalization broken)
✗ Mode 16 doesn't match mode 15 (texture binding error)
✗ Unknown region receives radiance (classification leak)
✗ Chart 6 receives radiance (front wall activation bug)
```

If failure occurs, diagnose and fix before proceeding to Phase 2C or feedback.
