# 10 — C0 Resolution and System Configuration

**Purpose:** Understand how the C0 probe count slider ties the entire hierarchy together
and what the current configuration options mean in practice.

---

## Everything derives from one number

The probe grid is controlled by a single parameter: **C0 probe resolution** (`cascadeC0Res`).
Default: 32.

From this one number, every spatial parameter in the cascade system is derived:
```
baseInterval = volumeSize / cascadeC0Res = 4.0 / 32 = 0.125 m
```

All cascade intervals use `baseInterval` as their unit:
```
C0: [0.02, 1×d ] = [0.02, 0.125] m
C1: [1×d,  4×d ] = [0.125, 0.5 ] m
C2: [4×d, 16×d ] = [0.5,   2.0 ] m
C3: [16×d,64×d ] = [2.0,   8.0 ] m
```

Changing `cascadeC0Res` from 32 to 16 changes `baseInterval` to 0.25 m:
```
C0: [0.02, 0.250] m — C0 now covers twice as much distance
C3: [4.0,  16.0 ] m — C3 now extends beyond the scene
```

The C0 resolution slider therefore controls:
1. **Spatial probe density** — how finely the probe grid samples the scene
2. **Near-field interval** — how close to surfaces C0 can capture light from
3. **Far-field extent** — how far C3 reaches
4. **Bake time** — 8³=512 probes vs 64³=262,144 probes

---

## Co-located vs non-co-located with the slider

**Co-located:** All 4 cascade levels use `cascadeC0Res³` probes.

**Non-co-located:** Each cascade uses `(cascadeC0Res >> i)³` probes (halving per level):
```
C0: cascadeC0Res³
C1: (cascadeC0Res/2)³
C2: (cascadeC0Res/4)³
C3: (cascadeC0Res/8)³
```

At `cascadeC0Res = 8` with non-co-located: C3 = `(8>>3)³ = 1³` — a single probe.
That is degenerate (one probe covers the entire 4 m scene at 2–8 m range). The minimum
safe C0 resolution for non-co-located is **32** (gives C3 = 4³ = 64 probes).

The UI combo offers {8, 16, 32 (default), 64}. At cascadeC0Res=8 with non-co-located, the
tooltip warns about degenerate C3.

---

## Memory at each setting

**Co-located + D=4 fixed:**
| C0 Res | Atlas per level | 4 levels |
|---|---|---|
| 8 | 32×32×8 = 0.5 MB | 2 MB |
| 16 | 64×64×16 = 4 MB | 16 MB |
| 32 | 128×128×32 = 4 MB | 16 MB |
| 64 | 256×256×64 = 64 MB | 256 MB |

(C0Res=16 and C0Res=32 happen to give the same 4 MB because the atlas for C0Res=32, D=4 is
128×128×32 = same 4 MB as C0Res=16 at larger probeRes but smaller D tiles.)

Wait, let me recalculate:
- C0Res=32, D=4: 128×128×32 × 8 bytes = 4 MB ✓
- C0Res=64, D=4: 256×256×64 × 8 bytes = 32 MB per cascade, 128 MB total

---

## What happens on slider change

Changing `cascadeC0Res` changes atlas texture dimensions. The textures must be destroyed
and re-created from scratch:

```cpp
static int lastC0Res = 32;
if (cascadeC0Res != lastC0Res) {
    lastC0Res = cascadeC0Res;
    destroyCascades();   // free all GPU textures
    initCascades();      // re-derive baseInterval, re-allocate textures
    cascadeReady = false; // trigger full re-bake
}
```

`initCascades()` now does:
```cpp
const int   baseRes = cascadeC0Res;          // no longer hardcoded 32
baseInterval        = volumeSize.x / float(baseRes);  // re-derived
```

The log message confirms: `[C0] probe res: 16^3  baseInterval=0.2500m`.

---

## The full configuration space

Every combination of the three main toggles is valid (some have caveats):

| C0Res | Co-located | D-scaling | Notes |
|---|---|---|---|
| 32 | ON | OFF | Default. 16 MB VRAM. |
| 32 | ON | ON | 148 MB VRAM. C3 has 256 bins. |
| 32 | OFF | OFF | 5 MB VRAM. Non-co-located spatial layout. |
| 32 | OFF | ON | 7 MB VRAM. Best memory + quality balance. |
| 16 | ON | OFF | 4 MB. Coarser spatial. Faster bake. |
| 64 | ON | OFF | 128 MB. Very fine spatial. Slow bake. |
| 8 | OFF | OFF | C3=1³ degenerate. Do not use. |

The Cascade panel's hierarchy table shows the actual dimensions derived from all toggles:
```
C0: 32^3  D=4  cell=0.1250m  [0.020, 0.125]m  D^2=16 rays
C1: 16^3  D=8  cell=0.2500m  [0.125, 0.500]m  D^2=64 rays
C2:  8^3  D=16 cell=0.5000m  [0.500, 2.000]m  D^2=256 rays
C3:  4^3  D=16 cell=1.0000m  [2.000, 8.000]m  D^2=256 rays
```
(example: C0Res=32, non-co-located, D-scaled ON)

---

## Why "bake" not "render"

The cascade computation is explicitly called a "bake" rather than a real-time render pass:
- It only runs when something changes (`cascadeReady == false`).
- It can take 10–100 ms for large configurations (not acceptable per-frame).
- For a static Cornell Box, the bake runs once; the renderer reads the cached result every frame.

The `cascadeReady` flag is set to false by any toggle that changes bake output:
- Scene change (voxel paint)
- Merge mode toggle
- Blend fraction change
- Co-located toggle
- D-scaling toggle
- Bilinear toggle
- C0 resolution change

---

## Summary of all Phase 5 toggles and what they control

| Toggle | What it changes | Visual effect |
|---|---|---|
| Directional merge (5c) | Per-direction vs isotropic upper cascade lookup | Color bleeding accuracy |
| Directional bilinear (5f) | 4-sample blend vs nearest-bin snap | Smoothness at bin boundaries |
| Co-located (5d) | All N³ vs halving-per-level probe layout | Probe spatial density per level |
| D-scaling (5e) | Same D vs growing D per level | Directional resolution for far cascades |
| C0 resolution | Base probe count, sets baseInterval | Spatial density + interval widths |
