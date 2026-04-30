# 09 Phase 5d: What the Full 3D Upper-Cascade Lookup Would Be

This note answers one specific question:

If we use non-co-located cascades in 3D, what is the correct spatial analogue of ShaderToy's upper-cascade interpolation?

Short answer:

- ShaderToy does 2D interpolation across `4` upper probes
- this 3D volumetric branch should do 3D interpolation across `8` upper probes

The older Phase 5d branch did not do that. It picked one parent upper probe with integer division:

```glsl
upperProbePos = probePos / 2;
```

That was a useful simplification for experiments, but it was not the full 3D equivalent.

The latest codebase has now moved past that simplification:

- non-co-located directional merge can spatially trilinearly blend 8 upper probes
- but it still does not do full per-corner visibility weighting in the ShaderToy spirit

---

## 1. Why `32 -> 16 -> 8 -> 4` is a natural 3D hierarchy

Current non-co-located mode halves probe resolution on each axis:

- C0: `32^3`
- C1: `16^3`
- C2: `8^3`
- C3: `4^3`

That means cell size doubles on each axis:

- C0: `0.125 m`
- C1: `0.25 m`
- C2: `0.5 m`
- C3: `1.0 m`

This is the 3D version of a standard mip-like hierarchy.

Important consequence:

- one C1 probe spans a `2 x 2 x 2` block of C0 probes
- one C2 probe spans a `2 x 2 x 2` block of C1 probes

So the “parent block” relation is `8`, not `4`.

Why people sometimes think “4”:

- ShaderToy is effectively working in a 2D surface parameterization
- `2 x 2 = 4` there
- but this project is volumetric, so it is `2 x 2 x 2 = 8`

---

## 2. Is the current upper-probe center placement reasonable?

Yes.

The project places probe centers using:

```cpp
worldPos = origin + (index + 0.5) * cellSize
```

That means probe centers always sit at cell centers.

Example on one axis:

- lower spacing = `0.125`
- upper spacing = `0.25`

Lower centers:

- `0.0625`
- `0.1875`
- `0.3125`
- `0.4375`

Upper centers:

- `0.125`
- `0.375`

Notice:

- `0.125` is centered between `0.0625` and `0.1875`
- `0.375` is centered between `0.3125` and `0.4375`

So the upper probe really is centered over each pair of lower cells per axis.

In 3D, that means each upper probe is centered over a full `2 x 2 x 2` lower block.

So the branch's current **probe placement** is fine.

---

## 3. What the old simplified code did

The original Phase 5d shortcut was:

```glsl
upperProbePos = probePos / 2;
```

This means:

- every lower probe picks exactly one upper probe
- all `8` lower probes in the same `2 x 2 x 2` block read the same upper probe

That is equivalent to nearest-parent sampling in space.

Benefits:

- simple
- easy to debug
- cheap

Costs:

- spatial transitions can become blocky
- there is no smooth interpolation across the upper grid
- this is not full ShaderToy-style behavior

---

## 4. What the full 3D version should do

The full 3D analogue of ShaderToy's merge is:

1. take the current lower probe's world position
2. map that position into the upper probe grid
3. find the `8` surrounding upper probes
4. compute trilinear interpolation weights
5. read the correct direction from each upper probe
6. blend those 8 directional samples
7. optionally apply visibility weighting per upper candidate

That is the true “8-neighbor” version.

---

## 5. The math

### Step A: map current world position into upper-grid coordinates

Given:

- `worldPos`
- `upperOrigin`
- `upperCellSize`

Compute continuous upper-grid coordinates:

```glsl
vec3 upperGrid = (worldPos - upperOrigin) / upperCellSize - 0.5;
```

Why `-0.5`:

- integer upper probe index `i` corresponds to center position `(i + 0.5) * upperCellSize`
- subtracting `0.5` converts world position into “probe-center space”

So:

- `upperGrid = (3.2, 5.7, 8.1)` means
  - this lower probe lies spatially between upper probes around index `(3,5,8)`

### Step B: get the base corner and fractional offset

```glsl
ivec3 p000 = ivec3(floor(upperGrid));
vec3  f    = fract(upperGrid);
```

Then clamp `p000` so all 8 neighbors stay in bounds:

```glsl
p000 = clamp(p000, ivec3(0), upperRes - ivec3(2));
```

Now define the 8 corners:

```glsl
ivec3 p100 = p000 + ivec3(1,0,0);
ivec3 p010 = p000 + ivec3(0,1,0);
ivec3 p110 = p000 + ivec3(1,1,0);
ivec3 p001 = p000 + ivec3(0,0,1);
ivec3 p101 = p000 + ivec3(1,0,1);
ivec3 p011 = p000 + ivec3(0,1,1);
ivec3 p111 = p000 + ivec3(1,1,1);
```

### Step C: read one directional value from each upper probe

For each upper probe corner:

1. choose the direction bin for the current ray direction
2. fetch that bin from the upper atlas tile

If upper direction resolution is `Du`, a helper would look like:

```glsl
vec3 sampleUpperProbeDir(ivec3 upperProbePos, vec3 rayDir, int Du) {
    ivec2 bin = dirToBin(rayDir, Du);
    return texelFetch(
        uUpperCascadeAtlas,
        ivec3(upperProbePos.x * Du + bin.x,
              upperProbePos.y * Du + bin.y,
              upperProbePos.z),
        0
    ).rgb;
}
```

Then gather:

```glsl
vec3 s000 = sampleUpperProbeDir(p000, rayDir, Du);
vec3 s100 = sampleUpperProbeDir(p100, rayDir, Du);
vec3 s010 = sampleUpperProbeDir(p010, rayDir, Du);
vec3 s110 = sampleUpperProbeDir(p110, rayDir, Du);
vec3 s001 = sampleUpperProbeDir(p001, rayDir, Du);
vec3 s101 = sampleUpperProbeDir(p101, rayDir, Du);
vec3 s011 = sampleUpperProbeDir(p011, rayDir, Du);
vec3 s111 = sampleUpperProbeDir(p111, rayDir, Du);
```

### Step D: trilinear blend

Blend along X:

```glsl
vec3 sx00 = mix(s000, s100, f.x);
vec3 sx10 = mix(s010, s110, f.x);
vec3 sx01 = mix(s001, s101, f.x);
vec3 sx11 = mix(s011, s111, f.x);
```

Blend along Y:

```glsl
vec3 sxy0 = mix(sx00, sx10, f.y);
vec3 sxy1 = mix(sx01, sx11, f.y);
```

Blend along Z:

```glsl
vec3 upperDir = mix(sxy0, sxy1, f.z);
```

That `upperDir` is the full 3D trilinear upper-cascade answer for that ray direction.

---

## 6. Where visibility weighting would attach

If visibility weighting is kept, it should be evaluated per upper candidate, not once for the whole parent block.

Meaning:

- `p000` may be visible
- `p100` may be blocked
- `p010` may be visible
- etc

So conceptually you would:

1. compute an occlusion weight for each upper probe candidate
2. multiply each sample by its weight
3. blend weighted samples
4. normalize by the total surviving weight

Pseudo-shape:

```glsl
vec3 accum = vec3(0.0);
float wsum = 0.0;

for each corner probe Pi:
    float wSpatial = trilinear weight for Pi;
    float wVis     = visibilityWeight(Pi, worldPos, rayDir);
    float w        = wSpatial * wVis;
    accum += sampleUpperProbeDir(Pi, rayDir, Du) * w;
    wsum  += w;

upperDir = (wsum > 0.0) ? (accum / wsum) : vec3(0.0);
```

That is much closer to the spirit of ShaderToy's `WeightedSample()`.

---

## 7. What the current branch still stops short of

Because this full version is significantly more expensive and more complex:

- `8` upper probes instead of `1`
- each upper probe may already do directional bin lookup or bilinear-in-direction lookup
- visibility weighting multiplies the cost again
- edge clamping and different `D` values per cascade complicate it further

The branch no longer stops at one parent probe, but it still stops short of the fully weighted version:

- it does the 8-neighbor spatial blend
- it does directional lookup per upper probe
- it does not add full per-corner visibility weighting back in

---

## 8. The clean conclusion

Your understanding is correct after one 2D-to-3D correction:

- not “1 upper to 4 lower”
- but “1 upper centered over 8 lower”

And your stronger challenge is also correct:

- if we want the real 3D equivalent of ShaderToy's upper-cascade interpolation, we should not stop at one parent probe
- we should spatially blend the `8` surrounding upper probes

So:

- current Phase 5d placement: reasonable
- current latest branch: already does `8`-neighbor trilinear spatial blend for the non-co-located directional path
- full 3D ShaderToy-like weighted version would still add per-corner visibility weighting on top
