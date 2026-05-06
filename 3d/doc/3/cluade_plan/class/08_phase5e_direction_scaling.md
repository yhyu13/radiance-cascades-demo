# 08 — Phase 5e: Per-Cascade Direction Resolution Scaling

**Purpose:** Understand why different cascade levels might want different numbers of
direction bins, and what the D=2 degenerate case bug taught us.

---

## Motivation: should all levels use the same D?

Phase 5a–5c used a fixed D=4 for all cascade levels. Is that the right choice?

Consider what each cascade level represents:

**C0 covers [0, 12.5 cm].** Near-field contact lighting. C0 probes care a lot about
*where* they are (dense spatial grid), but nearby surfaces don't vary dramatically in
direction — you either hit the nearby wall or you don't. High spatial precision, lower
directional precision needed.

**C3 covers [2, 8 m].** Far-field illumination from ceiling, distant walls, sky.
C3 probes are spatially coarse (4³ = 64 probes in non-co-located mode), but far-field
illumination is highly directional — ceiling light comes from above, floor light from
below, walls from the sides. High directional precision wanted.

ShaderToy uses a per-cascade D that grows with the cascade level, giving upper cascades
more directional bins.

---

## The A/B toggle

Phase 5e adds `useScaledDirRes` toggle:
- **OFF (default):** all cascades use `D = dirRes = 4`. Flat 16 bins everywhere.
- **ON (ShaderToy-style):** D scales per level.

The scaling formula:
```cpp
int cascD = useScaledDirRes ? std::min(16, dirRes << i) : dirRes;
```

At `dirRes=4`:
| Level | D scaled | Bins | D fixed | Bins |
|---|---|---|---|---|
| C0 | D=4 | 16 | D=4 | 16 |
| C1 | D=8 | 64 | D=4 | 16 |
| C2 | D=16 | 256 | D=4 | 16 |
| C3 | D=16 (capped) | 256 | D=4 | 16 |

C0 is unchanged in either mode. The cap at 16 is a practical memory limit
(D=32 would give 512×512×32 per cascade at co-located mode = 256 MB just for C3).

---

## The critical upper-merge remap

When C0 (D=4) reads C1 (D=8), the direction bins don't match 1:1. C0's bin (dx=2, dy=1)
covers a ~36° solid angle patch. The equivalent patch in C1's D=8 grid covers bins ~(4,2) and (5,2).

Without remapping, C0 would use its own (dx, dy) to address C1's atlas — reading the wrong bin.

The fix: remap the ray direction into the upper cascade's D space:
```glsl
uniform int uUpperDirRes;  // upper cascade's D (may differ from current cascade's D)

// When misses, find which bin in the UPPER cascade matches this ray direction:
ivec2 upperBin = dirToBin(rayDir, uUpperDirRes);  // use uUpperDirRes, not uDirRes
ivec3 upperAtlasTxl = ivec3(
    upperProbePos.x * uUpperDirRes + upperBin.x,
    upperProbePos.y * uUpperDirRes + upperBin.y,
    upperProbePos.z);
```

When D is equal for both cascades, `dirToBin(binToDir(b, D), D) == b` exactly — the
remap is a no-op and Phase 5c behavior is preserved.

---

## Memory impact

**Co-located mode + scaled D ON:**

| Level | D | Atlas dims | VRAM |
|---|---|---|---|
| C0 | 4 | 128×128×32 | 4 MB |
| C1 | 8 | 256×256×32 | 16 MB |
| C2 | 16 | 512×512×32 | 64 MB |
| C3 | 16 | 512×512×32 | 64 MB |
| Total | | | ~148 MB |

**Non-co-located + scaled D ON:**

| Level | D | Probe res | Atlas dims | VRAM |
|---|---|---|---|---|
| C0 | 4 | 32³ | 128×128×32 | 4 MB |
| C1 | 8 | 16³ | 128×128×16 | 2 MB |
| C2 | 16 | 8³ | 128×128×8 | 1 MB |
| C3 | 16 | 4³ | 64×64×4 | 0.125 MB |
| Total | | | | ~7 MB |

Non-co-located benefits doubly: fewer probes (2× halving) AND the smaller atlas texture
from fewer probes partially offsets the larger D. C1's atlas at D=8 is 128×128×16 —
same width as C0's at D=4 because C1 has half the probes.

---

## The D=2 degenerate case bug (found and fixed during Phase 5e)

Original plan: D scaling starts at C0=D2. The formula `1 << (i+1)` gives D=[2,4,8,16].

**What went wrong at D=2:**

`binToDir(ivec2(dx, dy), 2)` for all 4 bins:
- (0,0) → oct=(0.25, 0.25) → `octToDir` → direction with z≈0 (equatorial)
- (1,0) → oct=(0.75, 0.25) → direction with z≈0
- (0,1) → oct=(0.25, 0.75) → direction with z≈0
- (1,1) → oct=(0.75, 0.75) → direction with z≈0

The octahedral fold places all 4 D=2 bin centers on or near the equatorial plane (z=0).
No bin points up (+Z) or down (−Z).

**Consequence:** When C0 (D=4) fired a ray upward and missed, it called `dirToBin(upward_ray, D=2_for_C1)`
and got the nearest equatorial bin. C1's "upward" radiance was mixed with equatorial
radiance. The green wall (vertical) bled everywhere; ceiling light was absent.

**Fix:** Minimum D = 4. Formula changed to `min(16, dirRes << i)` with `dirRes=4`:
- C0=D4 (same as baseline)
- C1=D8, C2=D16, C3=D16 (capped)

C0 stays at D4 — the degenerate D=2 is never used.

---

## Toggling resets the cascade

Changing `useScaledDirRes` changes atlas texture dimensions. The textures must be
destroyed and reallocated:
```cpp
static bool lastScaledDirRes = false;
if (useScaledDirRes != lastScaledDirRes) {
    destroyCascades();
    initCascades();       // reallocates atlases with new D
    cascadeReady = false;
}
```

This is the same destroy+rebuild pattern as the co-located/non-co-located toggle.
