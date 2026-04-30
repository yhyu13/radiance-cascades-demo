# Cascade C0 Probe Resolution Slider — Implementation Learnings

**Date:** 2026-04-29
**Branch:** 3d
**Status:** Implemented, diff-verified. Build and runtime pending.
**Follows:** `phase5f_impl_learnings.md`

---

## Problem: Why a C0 Resolution Slider?

### Context

The 3D cascade hierarchy derives all its spatial parameters from a single base quantity:

```
baseInterval = volumeSize.x / baseRes   (e.g. 4.0 / 32 = 0.125m)
```

This is simultaneously C0's cell size and the near-field ray interval cap (`tMax` for C0).
All cascade intervals scale from it:

```
C0: [0.02,   d   ]  = [0.02,  0.125]m
C1: [d,      4d  ]  = [0.125, 0.5  ]m
C2: [4d,     16d ]  = [0.5,   2.0  ]m
C3: [16d,    64d ]  = [2.0,   8.0  ]m
```

With `baseRes` hardcoded to 32, the only way to trade probe density for bake speed was to
add more cascades — but that widens the intervals, not the spatial grid. Adding a C0
resolution slider lets the user directly control:

1. **Spatial probe density** — how finely the probe grid samples the world volume.
2. **Near-field interval** — `baseInterval` shrinks when probes are denser; C0 covers
   shorter distances but more accurately.
3. **Bake time** — 8^3 = 512 probes vs 64^3 = 262 144 probes; ~500× fewer dispatches.

---

## What Was Implemented

### New member (`src/demo3d.h`)

```cpp
/** C0 probe grid resolution (powers of 2: 8/16/32/64). All other cascades derived from this.
 *  co-located:     all cascades use cascadeC0Res^3.
 *  non-co-located: Ci uses (cascadeC0Res >> i)^3, halving per level.
 *  Also sets baseInterval = volumeSize / cascadeC0Res (C0 cell size = tMax_C0). */
int cascadeC0Res;
```

Added after `useDirBilinear` in the Phase 5f block.

### Constructor initializer (`src/demo3d.cpp`)

```cpp
, useDirBilinear(true)
, cascadeC0Res(32)     // default — preserves prior behaviour
```

Default 32 keeps all pre-existing behaviour identical.

### `initCascades()` — derive everything from `cascadeC0Res`

Before:
```cpp
const int   baseRes    = 32;
const float baseCellSz = volumeSize.x / float(baseRes);  // 0.125
```

After:
```cpp
const int   baseRes    = cascadeC0Res;
baseInterval           = volumeSize.x / float(baseRes);  // re-derived each rebuild
const float baseCellSz = baseInterval;
```

`baseInterval` is a member, so its new value is available in `render()` immediately after
`initCascades()` returns (the tracking block logs it for confirmation).

The rest of `initCascades()` already used `baseRes` and `baseCellSz` — no further changes
were needed in the loop body.

### `render()` tracking block

```cpp
// C0 probe resolution slider — changes interval and atlas dimensions
static int lastC0Res = 32;
if (cascadeC0Res != lastC0Res) {
    lastC0Res = cascadeC0Res;
    destroyCascades();
    initCascades();
    cascadeReady = false;
    std::cout << "[C0] probe res: " << cascadeC0Res
              << "^3  baseInterval=" << baseInterval << "m" << std::endl;
}
```

Uses the same destroy+rebuild pattern as the Phase 5d co-located toggle and Phase 5e
D-scaling toggle — necessary because atlas texture dimensions change with resolution.

Placed immediately after the `lastScaledDirRes` block so all three structural rebuilds
are contiguous and readable.

### `renderCascadePanel()` — ImGui combo

```cpp
// C0 probe resolution slider
{
    static const int kC0Options[] = { 8, 16, 32, 64 };
    static const char* kC0Labels[] = { "8^3  (fast, coarse)", "16^3", "32^3 (default)", "64^3  (slow)" };
    int curIdx = 2;
    for (int k = 0; k < 4; ++k) if (kC0Options[k] == cascadeC0Res) { curIdx = k; break; }
    ImGui::Text("C0 probe resolution:");
    HelpMarker(...);
    if (ImGui::Combo("##C0Res", &curIdx, kC0Labels, 4))
        cascadeC0Res = kC0Options[curIdx];
    ImGui::SameLine();
    ImGui::TextDisabled("baseInterval=%.4fm", volumeSize.x / float(cascadeC0Res));
}
```

Placed between the Phase 5d co-located checkbox and the Phase 5e D-scaling checkbox,
because co-located/non-co-located determines how `cascadeC0Res` propagates:
- co-located: all cascades use `cascadeC0Res^3`.
- non-co-located: cascade `i` uses `(cascadeC0Res >> i)^3`.

The `TextDisabled` line shows the derived `baseInterval` **before** the rebuild fires
(computed live from `volumeSize.x / cascadeC0Res`), so the user can preview the new
interval before it takes effect.

---

## Cascade Header — Dynamic Layout Text

The cascade hierarchy header and status line previously hardcoded "32^3" and "32/16/8/4":

```cpp
// Before
ImGui::Text("Cascade Count: %d  [co-located, all 32^3]", cascadeCount);
ImGui::TextDisabled("(all 32^3)");
ImGui::TextDisabled("(32/16/8/4)");
```

These were replaced with runtime format strings driven by `cascadeC0Res`:

```cpp
// After
char layoutDesc[64];
if (useColocatedCascades)
    snprintf(layoutDesc, sizeof(layoutDesc), "co-located, all %d^3", cascadeC0Res);
else
    snprintf(layoutDesc, sizeof(layoutDesc), "non-co-located, %d/%d/%d/%d",
        cascadeC0Res, cascadeC0Res>>1, cascadeC0Res>>2, cascadeC0Res>>3);
ImGui::Text("Cascade Count: %d  [%s]", cascadeCount, layoutDesc);
```

The per-cascade table line (`C%d: %d^3 D=%d ...`) already read from `cascades[i].resolution`
which is set in `initCascades()` — it required no change.

---

## Memory Budget at Each Option

| C0 Res | Mode | Atlas dimensions | VRAM (all D4) | VRAM (D scaled) |
|--------|------|-----------------|---------------|-----------------|
| 8^3    | co-located | 32²×8 × 4 cascades | ~0.5 MB | ~6 MB |
| 16^3   | co-located | 64²×16 × 4 | ~4 MB | ~50 MB |
| 32^3   | co-located | 128²×32 × 4 | ~32 MB | ~148 MB |
| 64^3   | co-located | 256²×64 × 4 | ~256 MB | ~1.2 GB |
| 32^3   | non-co-located | 128²×32 + 64²×16 + 32²×8 + 16²×4 | ~10 MB | ~7 MB |
| 8^3    | non-co-located | gives C3=1^3 | degenerate | — |

`64^3 + D-scaled` exceeds typical 8 GB VRAM. The combo label says "(slow)" to discourage it.
Non-co-located `8^3` gives C3 = `(8>>3)^3 = 1^3` — a single probe covering the whole 4m
volume with 64m interval; the label should warn against this combination.

---

## Why Discrete Powers of 2?

Continuous probe count would create non-power-of-2 atlas dimensions. Atlas tiles are
`probeRes × D` — the bilinear clamp within `[0, D-1]` still holds for any res, but
GL_NEAREST correctness depends on exact integer tile boundaries. Non-power-of-2 probeRes
creates no alignment issue (GL doesn't require it), but conventionally cascade grids are
chosen as powers of 2 for:
1. Non-co-located halving: `baseRes >> i` only gives integer results if baseRes is a power of 2.
2. Mental model: "doubling/halving resolution" is the natural unit.

For non-co-located mode, `cascadeC0Res = 8` gives C3 = `8>>3 = 1` (degenerate) and
`cascadeC0Res = 16` gives C3 = `16>>3 = 2` (2^3 = 8 probes, technically valid but
coarse). The UI tooltip documents this limitation.

---

## No Shader Changes Required

All cascade geometry parameters (`uVolumeSize`, `uBaseInterval`, `uProbeCellSize`) are
pushed as uniforms per-dispatch from `updateSingleCascade()`, which reads from the
`cascades[i]` struct populated by `initCascades()`. No shader uniform names changed.
The compute shader uses `uBaseInterval` for tMin/tMax and `uVolumeSize` for the probe
grid — both are already runtime uniforms.

---

## Correctness Invariants

**cascadeC0Res = 32 (default):** `baseInterval = 4.0 / 32 = 0.125`. Identical to prior hardcoded behaviour. Zero regression.

**cascadeC0Res = 16:** `baseInterval = 0.25m`. C0 covers [0.02, 0.25]m, C3 covers [4.0, 16.0]m — the cascade stack extends beyond the 4m volume. C3 rays exit the SDF early; `useEnvFill` OFF means those bins return black.

**cascadeC0Res = 64:** `baseInterval = 0.0625m`. C0 covers [0.02, 0.0625]m — very fine near-field. C3 covers [1.0, 4.0]m (half the prior range). Far-field coverage shrinks; may need more cascades for full [0–8m] coverage.

**Non-co-located + cascadeC0Res = 8:** C3 probeRes = `8 >> 3 = 1`. Single probe, degenerate. User must avoid this combination.

---

## Validation Status

| Test | Status |
|---|---|
| Build: 0 errors | Pending |
| Default 32^3: identical GI to prior hardcoded behaviour | Pending runtime |
| Switch 32→16: log shows `[C0] probe res: 16^3 baseInterval=0.2500m` | Pending runtime |
| Switch 32→64: atlas visibly finer in debug mode 3 | Pending runtime |
| Non-co-located + 32: C3 = 4^3 as before | Pending runtime |
| Non-co-located + 16: C3 = 2^3 (8 probes), visible in cascade table | Pending runtime |
