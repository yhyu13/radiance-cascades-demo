# 06 — Phase 5c: Directional Merge

**Purpose:** Understand what changes when a ray that misses reads the upper cascade
by direction bin instead of by probe average.

---

## What isotropic merge does (the old way)

When a C0 ray in direction D misses (no surface within 0–12.5 cm):
```glsl
vec3 upperSample = texture(uUpperCascade, uvwProbe).rgb;
rad = upperSample;
```

`uvwProbe` is the probe's world position normalized to [0,1]³. `texture()` with a 3D
sampler does trilinear interpolation between 8 neighboring C1 probes and returns their
average color. **The same averaged color is returned for every direction.**

Problem: if the C1 probe sees red in one direction and green in another, `texture()`
returns orange for both. A C0 ray going toward the red wall gets orange. A C0 ray
going toward the green wall also gets orange. Both are wrong.

---

## What directional merge does (Phase 5c)

When a C0 ray in direction D misses:
```glsl
ivec2 upperBin = dirToBin(rayDir, uUpperDirRes);
vec3 upperSample = texelFetch(uUpperCascadeAtlas,
    ivec3(upperProbePos.x * uUpperDirRes + upperBin.x,
          upperProbePos.y * uUpperDirRes + upperBin.y,
          upperProbePos.z), 0).rgb;
rad = upperSample;
```

Steps:
1. `dirToBin(rayDir, D)` → find which direction bin index matches the ray direction.
2. Compute the atlas texel: upper probe's tile origin + bin offset.
3. `texelFetch` reads exactly that one texel — no blending, no interpolation.

Now a C0 ray going toward the red wall gets C1's "red direction" bin. A ray going toward
green gets C1's "green direction" bin. Colors are now directionally correct.

---

## Co-located simplification

In co-located mode (default), all cascades share the same 32³ probe positions. Probe (px, py, pz)
in C0 and probe (px, py, pz) in C1 are at the **same world position**.

This means `upperProbePos = probePos` (same integer index). The upper atlas texel for
direction (dx, dy) is:
```
ivec3(px × D + dx,  py × D + dy,  pz)
```

...which is **exactly the same index** as the current cascade's texel for that direction.
The upper cascade's atlas texel for bin (dx, dy) is the same integer address as the
current cascade's texel for bin (dx, dy). The addresses coincide because the probes
are co-located and we are looking at the same direction bin.

This co-located simplification breaks in Phase 5d (non-co-located) and Phase 5e
(different D per cascade), which require computing `upperProbePos` and `upperBin`
separately.

---

## The toggle: A/B comparison

A toggle `uUseDirectionalMerge` lets you switch between:
- 1 = directional atlas read (Phase 5c) 
- 0 = isotropic texture() read (Phase 4 fallback)

Both `uUpperCascade` (isotropic probeGridTexture) and `uUpperCascadeAtlas` are bound
simultaneously so either path can be taken at runtime without rebinding.

When this toggle changes, `cascadeReady = false` — the bake must re-run because the
merge changed.

---

## What changes visually

With directional merge ON:
- The red wall casts red light on adjacent surfaces (directional accuracy).
- The green wall casts green.
- Color bleeding is now directional — the right wall (green) doesn't taint the left wall (red).

The banding at cascade boundaries also improves: instead of "upper cascade average color" 
suddenly appearing at tMax, the upper cascade's specific-direction color appears, which
more closely matches what the current cascade would have seen if it reached farther.

---

## Phase 5c in the shader — the full merge path

```glsl
vec3 upperDir = vec3(0.0);
if (uHasUpperCascade != 0) {
    if (uUseDirectionalMerge != 0) {
        // Phase 5c: directional atlas read
        ivec2 upperBin = dirToBin(rayDir, uUpperDirRes);
        ivec3 upperAtlasTxl = ivec3(
            upperProbePos.x * uUpperDirRes + upperBin.x,
            upperProbePos.y * uUpperDirRes + upperBin.y,
            upperProbePos.z);
        upperDir = texelFetch(uUpperCascadeAtlas, upperAtlasTxl, 0).rgb;
    } else {
        // Phase 4 isotropic fallback
        upperDir = texture(uUpperCascade, uvwProbe).rgb;
    }
}

// Apply to hit / miss / sky
if      (hit.a < 0.0) rad = hit.rgb;           // sky sentinel
else if (hit.a > 0.0) rad = hit.rgb * l + upperDir * (1.0 - l);  // surface + blend zone
else                   rad = upperDir;           // miss: pull from upper
```

---

## What remains after Phase 5c

Phase 5c reads the nearest bin. With D=4, each bin covers ~36°. A ray direction that
lands near the center of a bin gets the right answer. A ray direction that lands on the
boundary between two bins gets 100% of one bin's color, with a hard step to the
adjacent bin's color.

At D=4 with 16 bins, these hard bin-boundary steps are visible as **banding rings** and
**color bleeding** at bin seams. This is what Phase 5f fixes with bilinear interpolation.
