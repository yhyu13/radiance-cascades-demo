# 09 — Phase 5f: Directional Bilinear Interpolation

**Purpose:** Understand why nearest-bin lookup causes visible artifacts at D=4, what
bilinear blending fixes, and why the implementation requires manual 4-sample math
instead of using the GPU sampler.

> **Current code note (2026-05):** This page documents Phase 5f at D=4 (16 bins).
> The current default is D=8 (64 bins); each bin spans ~22° rather than ~36°,
> so bin-boundary banding is reduced but bilinear blending is still active and correct.
> The 4-sample math and GL_NEAREST atlas requirement are unchanged.

---

## The artifact: bin-boundary banding

With D=4, the 4×4=16 bins cover the full sphere. Each bin spans roughly 36° of solid angle.
Any ray that falls near the boundary between two adjacent bins gets 100% of one bin's color.
The adjacent bin may have a very different color (e.g., one faces the red wall, the other
faces the green wall).

Consequence: as you trace rays through the scene, directions near bin boundaries produce
hard color steps. These show up as:
- **Banding rings** at cascade boundaries (a visible ring where C0's tMax meets C1)
- **Color bleeding** (green wall color appearing on surfaces facing slightly away from green)

At D=4 (36° bin width) this is clearly visible. At D=16 (≈9° bins) it would be much
milder but still present at extreme bin edges.

---

## Why GL_LINEAR can't fix it

The atlas uses GL_NEAREST (required, see doc 05). Switching to GL_LINEAR would
interpolate across probe tile boundaries — mixing bin (3,0) of probe (px) with bin (0,0)
of probe (px+1). That is cross-probe contamination: a fundamental correctness bug.

So we must implement bilinear interpolation **manually**, staying within one probe's tile.

---

## The bilinear formula — center-to-center interpolation

We want to blend between 4 surrounding bin **centers**, not bin edges.

Bin k's center in octahedral space is at `(k + 0.5) / D`.

Define:
```
octScaled = dirToOct(rayDir) * D - 0.5
```

Why `-0.5`? This maps bin k's center to the integer value k:
```
oct at bin k's center = (k + 0.5) / D
→ octScaled = D × (k + 0.5) / D - 0.5 = k + 0.5 - 0.5 = k
```

So:
- `octScaled = k` exactly → at bin k's center → `floor = k, fract = 0` → 100% bin k
- `octScaled = k + 0.5` → halfway between bin k and k+1 centers → `fract = 0.5` → 50/50
- `octScaled = k + 1` → at bin k+1's center → `floor = k+1, fract = 0` → 100% bin k+1

This is center-to-center linear interpolation — the natural model for smoothly
transitioning between adjacent bin values.

---

## Clamp before floor/fract (critical correctness fix)

**The bug without clamping:** At `dirToOct(dir) ≈ 0.0`, `octScaled = 0 × D - 0.5 = -0.5`.

```glsl
// Without clamp:
ivec2 b00 = clamp(ivec2(floor(-0.5)), 0, D-1);  // floor(-0.5) = -1 → clamp → 0
vec2 f = fract(-0.5);  // = 0.5  ← problem!
```

`fract(-0.5) = 0.5` — a direction that maps to bin 0 (oct ≈ 0) gets a 50% weight on
bin 1. That's wrong: the direction should be 100% bin 0.

**The fix: clamp first:**
```glsl
vec2 octScaled = clamp(dirToOct(rayDir) * float(D) - 0.5, 0.0, float(D - 1));
```

After clamping to [0, D−1], `octScaled = 0.0`. `floor(0.0) = 0`, `fract(0.0) = 0.0`.
Correct: 100% bin 0.

The clamp shifts responsibility for boundary behavior before floor/fract are applied.

---

## The full implementation

```glsl
vec3 sampleUpperDir(ivec3 upperProbePos, vec3 rayDir, int D) {
    if (uUseDirBilinear == 0) {
        // Phase 5c nearest-bin fallback
        ivec2 bin = dirToBin(rayDir, D);
        return texelFetch(uUpperCascadeAtlas,
            ivec3(upperProbePos.x*D+bin.x, upperProbePos.y*D+bin.y, upperProbePos.z),
            0).rgb;
    }

    // Clamp octScaled to [0, D-1] BEFORE floor/fract to fix low-edge weight bug
    vec2 octScaled = clamp(dirToOct(rayDir) * float(D) - 0.5, 0.0, float(D - 1));

    ivec2 b00 = ivec2(floor(octScaled));                    // lower-left bin
    ivec2 b11 = clamp(b00 + ivec2(1), ivec2(0), ivec2(D-1)); // upper-right (clamped)
    ivec2 b10 = ivec2(b11.x, b00.y);                        // lower-right
    ivec2 b01 = ivec2(b00.x, b11.y);                        // upper-left
    vec2  f   = fract(octScaled);                            // blend weights

    int bx = upperProbePos.x * D, by = upperProbePos.y * D, bz = upperProbePos.z;
    vec3 s00 = texelFetch(uUpperCascadeAtlas, ivec3(bx+b00.x, by+b00.y, bz), 0).rgb;
    vec3 s10 = texelFetch(uUpperCascadeAtlas, ivec3(bx+b10.x, by+b10.y, bz), 0).rgb;
    vec3 s01 = texelFetch(uUpperCascadeAtlas, ivec3(bx+b01.x, by+b01.y, bz), 0).rgb;
    vec3 s11 = texelFetch(uUpperCascadeAtlas, ivec3(bx+b11.x, by+b11.y, bz), 0).rgb;

    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);
}
```

All 4 bin indices are independently within [0, D-1] by construction (b00 is clamped
before floor; b11 is clamped after adding 1). No read ever touches another probe's tile.

---

## Boundary behavior (asymmetric)

**High edge** (oct → 1.0, e.g. D=4: `octScaled = D - 0.5 = 3.5`):
- `b00 = 3`, `b11 = clamp(4, 0, 3) = 3`. Both same bin.
- `f = 0.5` but blend is `b00` and `b11` = same value → 100% border bin.
- Equivalent to GL_CLAMP_TO_EDGE at the high boundary.

**Low edge** (oct → 0.0: `octScaled = 0.0` after clamp):
- `b00 = 0`, `b11 = 1`. Different bins.
- `f = 0.0` → 100% bin 0 (correct). Not GL_CLAMP_TO_EDGE — but correct.

The cross-probe invariant (all reads within [0, D-1]) holds at both edges. No correctness
issue. The asymmetry only matters at the exact oct=0 and oct=1 boundaries, which are
rarely reached by real directions through normalized `dirToOct`.

---

## Isotropic merge also gets bilinear

When `uUseDirectionalMerge = 0` (isotropic fallback, Phase 4 behavior), `uUseDirBilinear`
still has an effect:

- **Bilinear ON:** `texture(uUpperCascade, uvwProbe)` — the `probeGridTexture` has
  `GL_LINEAR` by default, so hardware trilinear interpolation runs across 8 neighboring
  probes. Smooth spatial probe blending.
- **Bilinear OFF:** `texelFetch(uUpperCascade, upperProbePos, 0)` — reads exactly one
  probe, bypassing the sampler. Nearest-probe, no interpolation.

This ensures the bilinear toggle has a visible effect in both merge modes.

---

## Performance

Phase 5c nearest-bin: 1 texelFetch per direction bin per probe.
Phase 5f bilinear: 4 texelFetch per direction bin per probe.

At D=4 and 32³ probes co-located: 16 bins × 4 fetches × 32,768 probes = 2 million fetches
per cascade per bake. This is a static bake (not per frame), so the cost is paid once.

All 4 fetches stay within the probe's D×D tile. No cache thrashing from cross-tile access.

---

## Debug mode 6: visualizing the bilinear blend

Mode 5 (nearest-bin): shows the radiance at the selected `(dx, dy)` bin, snapping to
the nearest bin for every probe.

Mode 6 (bilinear): samples at `vec2(uAtlasBin) + 0.5` — the midpoint between the selected
bin and its `(+1,+1)` neighbor, where `f = (0.5, 0.5)` (maximum blend point).
At that midpoint, the bilinear blend uses equal weights from all 4 surrounding bins.

If Phase 5f is working, mode 6 should show a smooth color gradient where mode 5 shows a
hard step at the bin boundary. Toggle mode 5 ↔ 6 to directly compare.

Mode 6 is accessible via the ImGui "Bilinear" radio button or the `[F]` key cycle
(wraps at %7, covering modes 0–6).
