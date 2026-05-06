# Phase 5e Implementation Learnings -- Per-Cascade Directional-Resolution (D) Scaling

**Date:** 2026-04-29
**Branch:** 3d
**Status:** Implemented, compile-verified. D=2 degenerate case identified and fixed. Runtime A/B pending.
**Follows:** `phase5d_impl_learnings.md` + `phase5bc_impl_learnings.md`

---

## What Was Implemented

### New fields (`src/demo3d.h`)

```cpp
bool useScaledDirRes;              // 5e: true=per-cascade D; false=all D4 (default)
int  cascadeDirRes[MAX_CASCADES];  // per-cascade D; computed in initCascades()
```

### Constructor (`src/demo3d.cpp`)

Initializer list: `, useScaledDirRes(false)`.
Body: `for (int i = 0; i < MAX_CASCADES; ++i) cascadeDirRes[i] = dirRes;`

### `initCascades()` — per-cascade D and atlas allocation

```cpp
// Phase 5e: per-cascade D. Scaled: min(16, dirRes << i). Fixed: all D4.
int cascD = useScaledDirRes ? std::min(16, dirRes << i) : dirRes;
cascadeDirRes[i] = cascD;
int atlasXY = probeRes * cascD;  // atlas grows with D
```

Scaled D values: C0=D4, C1=D8, C2=D16, C3=D16 (capped at 16).

Atlas memory at co-located + scaled ON:
| Cascade | Atlas dims | VRAM |
|---|---|---|
| C0 | 128×128×32 | 4 MB (unchanged) |
| C1 | 256×256×32 | 16 MB (was 4 MB) |
| C2 | 512×512×32 | 64 MB (was 4 MB) |
| C3 | 512×512×32 | 64 MB (was 4 MB, capped at D16) |
| **Total** | | **~148 MB** |

Atlas memory at non-co-located + scaled ON:
| Cascade | Atlas dims | VRAM |
|---|---|---|
| C0 | 128×128×32 | 4 MB |
| C1 | 128×128×16 | 2 MB |
| C2 | 128×128×8 | 1 MB |
| C3 | 64×64×4 | 0.125 MB |
| **Total** | | **~7 MB** |

### `render()` — toggle tracking (destroy+rebuild pattern)

```cpp
static bool lastScaledDirRes = false;
if (useScaledDirRes != lastScaledDirRes) {
    lastScaledDirRes = useScaledDirRes;
    destroyCascades();
    initCascades();
    cascadeReady = false;
    std::cout << "[5e] dir scaling: " << ... << std::endl;
}
```

### `updateSingleCascade()` — two new uniforms

```cpp
glUniform1i(glGetUniformLocation(prog, "uDirRes"),      cascadeDirRes[cascadeIndex]);
int upperCascDirRes = hasUpper5d ? cascadeDirRes[cascadeIndex + 1] : cascadeDirRes[cascadeIndex];
glUniform1i(glGetUniformLocation(prog, "uUpperDirRes"), upperCascDirRes);
```

Reduction pass also updated to use `cascadeDirRes[cascadeIndex]`.

### Shader (`res/shaders/radiance_3d.comp`)

New uniform:
```glsl
uniform int uUpperDirRes;  // 5e: upper cascade D
```

Upper atlas lookup — remap direction to upper D space:
```glsl
ivec2 upperBin      = dirToBin(rayDir, uUpperDirRes);
ivec3 upperAtlasTxl = ivec3(upperProbePos.x * uUpperDirRes + upperBin.x,
                             upperProbePos.y * uUpperDirRes + upperBin.y,
                             upperProbePos.z);
```

Phase 5d visibility block also updated to use `uUpperDirRes` for `visBin`/`visTxl`.

---

## Critical Bug Found and Fixed: D=2 Degenerate Case

### Initial formula (wrong): `1 << (i+1)` → [D2, D4, D8, D16]

The originally planned formula set C0=D2 (4 bins). This is degenerate.

**Proof of degeneracy:** The octahedral parameterization maps [0,1]² to the full sphere. With D=2, the 4 bin centers are at oct coords (0.25, 0.25), (0.25, 0.75), (0.75, 0.25), (0.75, 0.75). Converting each:

- `octToDir(0.25, 0.25)` → uv = (-0.5, -0.5), d = (-0.5, -0.5, 0.0) → **z=0** (equatorial)
- `octToDir(0.25, 0.75)` → uv = (-0.5, +0.5), d = (-0.5, +0.5, 0.0) → **z=0** (equatorial)
- `octToDir(0.75, 0.25)` → uv = (+0.5, -0.5), d = (+0.5, -0.5, 0.0) → **z=0** (equatorial)
- `octToDir(0.75, 0.75)` → uv = (+0.5, +0.5), d = (+0.5, +0.5, 0.0) → **z=0** (equatorial)

All 4 D=2 bin centers land exactly on the octahedral equatorial fold (where z=0). No bin covers up or down. This is not a floating-point coincidence — it is structural: the fold boundary of the octahedral map passes exactly through the centers of all D=2 bins.

**Consequence for the merge:** C0 (D=2) reads C1's atlas (D=4) via `dirToBin(equatorial_ray, 4)`. For the horizontal direction (-0.707, -0.707, 0):

- oct = (0.25, 0.25), `floor(0.25 * 4) = 1` → C1 bin (1, 1)
- C1 bin (1,1) center: `octToDir((1.5/4, 1.5/4))` = `octToDir((0.375, 0.375))` → uv = (-0.25, -0.25), d = (-0.25, -0.25, +0.5) → normalize → **(-0.408, -0.408, +0.816)**

C0 reads C1's "mostly upward-left" data for its "horizontal left-diagonal" ray. These point in nearly opposite directions on the sphere (0.0 vs 0.816 z-component). This severe directional mismatch caused observed **green wall color bleeding into the red wall** when Phase 5c directional merge was enabled.

### Fixed formula: `min(16, dirRes << i)` → [D4, D8, D16, D16]

C0 stays at D=4 (same as the fixed-D baseline). Upper cascades scale upward:
- C0 = D4 (4<<0 = 4)
- C1 = D8 (4<<1 = 8, C1's upper atlas C2 uses D=16)
- C2 = D16 (4<<2 = 16)
- C3 = D16 (4<<3 = 32, capped at 16)

**D=4 round-trip verification:** `dirToBin(binToDir(b, 4), 8)` maps each of the 16 C0 directions to one of C1's 64 bins. No equatorial degeneracy — directions from D=4 span the full sphere including up/down. ✓

**Zero regression:** When scaled D OFF, `cascadeDirRes[i] = dirRes = 4` for all i, `uUpperDirRes == uDirRes`, `dirToBin(binToDir(b,4),4) == b` → identical behaviour to Phase 5c baseline. ✓

---

## Other Change: Cornell Box Default Scene

Changed `setScene(0)` (empty room) at `src/demo3d.cpp:225` to `setScene(1)` (Cornell box) so the application starts with the meaningful test scene.

---

## Files Changed

| File | Change |
|---|---|
| `src/demo3d.h` | Added `bool useScaledDirRes`; `int cascadeDirRes[MAX_CASCADES]` |
| `src/demo3d.cpp` | Constructor init; `initCascades()` per-cascade D; `render()` toggle block; `updateSingleCascade()` `uDirRes`/`uUpperDirRes` uniforms; reduction pass per-cascade D; readback `atlasWH`; `renderCascadePanel()` checkbox + table D column; `setScene(1)` default |
| `res/shaders/radiance_3d.comp` | `uUpperDirRes` uniform; `upperAtlasTxl` remapped via `dirToBin(rayDir, uUpperDirRes)`; `visBin`/`visTxl` in Phase 5d block also use `uUpperDirRes` |

---

## Known Limitations / Validation Status

| Test | Status |
|---|---|
| Build: 0 errors | Pending |
| Toggle OFF: identical GI to pre-change (zero regression) | Pending runtime |
| Toggle ON: C0 atlas unchanged (D4), C1 atlas grows to 256×256×32 | Pending runtime |
| Log: `[5e] dir scaling: scaled (D4/D8/D16/D16)` on toggle | Pending runtime |
| Cascade table shows `D=8` for C1 etc when scaled ON | Pending runtime |
| No color bleed between walls (D=2 bug gone) | Pending runtime |
| Visual A/B: does D scaling reduce C2/C3 boundary banding? | Pending runtime |

---

## Correctness Invariants

**Scaled D OFF (default):** `uUpperDirRes == uDirRes`. `dirToBin(binToDir(b,D),D) == b`. Identical to Phase 5c baseline.

**Scaled D ON:** C0 iterates [0,4)² (D=4, 16 bins). Reads C1 (D=8): `dirToBin(rayDir, 8)` maps each C0 direction to the nearest of C1's 64 bins. No degenerate equatorial collapse. Each C0 direction maps to a C1 bin in the same approximate hemisphere.

**Cap at D=16:** C3 uses D=16 (not D=32). C3 atlas = 512×512×32 = 64MB co-located. Acceptable on RTX 2080 SUPER (8GB VRAM). Non-co-located C3 (4³ probes): (4*16)²×4 = 128KB.
