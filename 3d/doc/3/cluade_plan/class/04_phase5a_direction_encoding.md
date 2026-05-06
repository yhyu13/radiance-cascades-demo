# 04 — Phase 5a: Direction Encoding

**Purpose:** Understand how we go from "fire rays in random directions" to
"fire rays at precise, addressable direction bins."

---

## The problem with the old approach

Phase 3–4 used Fibonacci sphere directions: a set of N quasi-random directions that
spread evenly over the sphere. This worked for averaging — fire N rays, sum up colors,
divide by N.

The problem: **Fibonacci directions are not invertible.** Given a ray direction vector,
you cannot quickly compute "which bin number is this?" without searching through all N
directions.

To store per-direction data and later retrieve it by direction, we need a scheme that
maps any direction to a bin index via a math formula, with no search.

---

## Octahedral encoding — the key idea

Take a sphere. Imagine shrink-wrapping it into an octahedron (8-faced diamond shape),
then unfolding the octahedron flat into a square. Every point on the sphere maps to a
unique point in the square, and vice versa.

```
Sphere (3D, all directions)  →  Square [0,1]² (2D, flat)
       dirToOct()                           octToDir()
```

This mapping is analytic (a formula), exact (no search), and invertible.

### dirToOct(dir) — sphere to square

```glsl
vec2 dirToOct(vec3 dir) {
    dir /= (abs(dir.x) + abs(dir.y) + abs(dir.z));  // project onto octahedron
    if (dir.z < 0.0) {
        // bottom hemisphere: fold the four bottom triangles into corners
        vec2 s = sign(dir.xy) * (1.0 - abs(dir.yx));
        dir.xy = s;
    }
    return dir.xy * 0.5 + 0.5;  // remap to [0,1]²
}
```

### octToDir(uv) — square to sphere

```glsl
vec3 octToDir(vec2 uv) {
    uv = uv * 2.0 - 1.0;  // remap to [-1,1]²
    vec3 d = vec3(uv, 1.0 - abs(uv.x) - abs(uv.y));
    if (d.z < 0.0) d.xy = (1.0 - abs(d.yx)) * sign(d.xy);
    return normalize(d);
}
```

---

## D×D direction bins

Divide the [0,1]² square into a D×D grid of equal cells. Each cell = one direction bin.
With D=4: 16 bins, each covering ~36° of solid angle (1/16 of the sphere).

```
[0,1]² square divided into 4×4 = 16 bins:

bin(0,3) bin(1,3) bin(2,3) bin(3,3)
bin(0,2) bin(1,2) bin(2,2) bin(3,2)
bin(0,1) bin(1,1) bin(2,1) bin(3,1)
bin(0,0) bin(1,0) bin(2,0) bin(3,0)
```

### dirToBin(dir, D) — direction to integer bin index

```glsl
ivec2 dirToBin(vec3 dir, int D) {
    vec2 oct = dirToOct(dir);
    return clamp(ivec2(floor(oct * float(D))), ivec2(0), ivec2(D - 1));
}
```

### binToDir(bin, D) — integer bin to representative direction

```glsl
vec3 binToDir(ivec2 bin, int D) {
    vec2 oct = (vec2(bin) + 0.5) / float(D);  // bin center in [0,1]²
    return octToDir(oct);
}
```

The `+ 0.5` places us at the center of each bin cell, not its corner.

---

## Why octahedral instead of Fibonacci?

| Property | Fibonacci | Octahedral |
|---|---|---|
| Even coverage | ✓ quasi-uniform | ✓ equal-area bins |
| Direction → index | ✗ requires search | ✓ formula (dirToBin) |
| Index → direction | ✓ by construction | ✓ formula (binToDir) |
| Atlas addressing | ✗ impossible | ✓ direct |

The invertibility (direction ↔ index) is what Phase 5b needs: we must write radiance
to a specific atlas texel by direction and later read it back by direction.

---

## What Phase 5a changed in the code

**Before (Phase 4):** `getRayDirection(gl_GlobalInvocationID.xy, uRaysPerProbe)` — Fibonacci

**After (Phase 5a):** Nested loop over D×D bins
```glsl
uniform int uDirRes;  // D = 4

for (int dy = 0; dy < uDirRes; ++dy) {
    for (int dx = 0; dx < uDirRes; ++dx) {
        vec3 rayDir = binToDir(ivec2(dx, dy), uDirRes);
        // ... raymarch in rayDir ...
    }
}
```

`uRaysPerProbe` was retired from the shader (no longer needed — ray count is D²).
The D²=16 rays per probe replaced the variable Fibonacci count.

**Rendered output in Phase 5a:** Identical to Phase 4. Direction averaging still happened
(still isotropic). The visual improvement comes in 5b+5c when the per-direction data
is stored and read directionally.

---

## The bin layout — full sphere coverage check

At D=4, do the 16 bins cover the entire sphere with no blind spots?

Yes. `dirToOct` maps the full sphere bijectively to [0,1]². The 4×4 grid of bins partitions
[0,1]² completely. Every direction falls into exactly one bin, including:
- Straight up (+Z) → oct ≈ (0.5, 0.75) → bin (2,3) approximately
- Straight down (−Z) → oct corners → bin near (0,0) or (3,0) etc.
- Horizontal circle → oct along the center diagonal

No direction is missed. No direction overlaps two bins. The encoding is a proper partition.

---

## Why D=4 minimum (not D=2)

At D=2, the 4 bin centers computed by `binToDir` all land on the octahedral equatorial
fold (z=0 plane). Every representative direction for every bin has z=0, meaning all
4 "representative" directions point horizontally. No bin representative points up or down.

When C0 (D=2) reads C1's atlas to merge in the vertical direction, there is no matching
bin — the nearest bin has z≈0 instead, causing severe directional mismatch.

Minimum safe D is **4**. All 16 bin centers at D=4 spread across the sphere including
top and bottom hemispheres.
