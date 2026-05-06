# Phase 4b Plan — Per-Cascade Ray Count Scaling

**Date:** 2026-04-24  
**Branch:** 3d  
**Depends on:** Phase 4a complete (committed `72e4f01`)  
**Goal:** Scale ray count geometrically per cascade level (C0=8, C1=16, C2=32, C3=64 at default base=8), with full runtime slider wiring.

---

## Problem

All four cascade levels currently use 8 rays (hardcoded in `initCascades()`). C3 covers [2.0, 8.0m] — a 6m shell — with only 8 rays, giving ~1 sample per 0.5 steradians. Noise in C3 propagates directly down the merge chain into C2 → C1 → C0.

Upper cascades cover a larger solid-angle budget per distance unit and need proportionally more samples to reduce that noise.

---

## Verified Pre-conditions

- `updateSingleCascade()` already sends `uRaysPerProbe` from `c.raysPerProbe` each dispatch — shader wiring is complete, no shader changes needed.
- `RadianceCascade3D::initialize()` allocates only a `resolution^3` texture — independent of `raysPerProbe`. Writing `cascades[i].raysPerProbe` directly at runtime does **not** require texture reallocation or an `initCascades()` rebuild.
- `BASE_RAY_COUNT = 4` constant in `demo3d.h` is declared but never used — safe to remove.

---

## Files Touched

| File | Change |
|---|---|
| `src/demo3d.h` | Add `int baseRaysPerProbe`; remove dead `BASE_RAY_COUNT` |
| `src/demo3d.cpp` constructor | Initialize `baseRaysPerProbe(8)` |
| `src/demo3d.cpp initCascades()` | Replace hardcoded `8` with `baseRaysPerProbe * (1 << i)` |
| `src/demo3d.cpp render()` | Add sentinel block for `baseRaysPerProbe` |
| `src/demo3d.cpp renderCascadePanel()` | Add slider + per-level ray count display |
| `src/demo3d.cpp renderSettingsPanel()` | Fix hardcoded `cascades[0].raysPerProbe` display |
| `src/demo3d.cpp renderTutorialPanel()` | Add 4b status entry |

No shader changes required for radiance integration. No texture reallocations.

> **Note:** Debug stat encoding (packed probe alpha) constrains the slider ceiling to `base <= 8` under the current decode. The `base=32` column in the performance table is for reference only — it exceeds the safe range of the packed hit-count encoding.

---

## Implementation

### `demo3d.h` — new member, remove dead constant

Remove:
```cpp
constexpr int BASE_RAY_COUNT = 4;
```

Add to class (near `useEnvFill`):
```cpp
int baseRaysPerProbe;  // default 8 — scales per level as base * 2^i
```

### Constructor — initialize

```cpp
, baseRaysPerProbe(8)
```

### `initCascades()` — geometric scaling at init time

```cpp
for (int i = 0; i < cascadeCount; ++i)
    cascades[i].initialize(probeRes, cellSz, volumeOrigin, baseRaysPerProbe * (1 << i));
```

Replaces the hardcoded `8`. Default: C0=8, C1=16, C2=32, C3=64.

### `render()` — runtime sentinel

Placed alongside the other sentinels (env fill, merge flag):

```cpp
static int lastBaseRays = -1;
if (baseRaysPerProbe != lastBaseRays) {
    lastBaseRays = baseRaysPerProbe;
    for (int i = 0; i < cascadeCount; ++i)
        cascades[i].raysPerProbe = baseRaysPerProbe * (1 << i);
    cascadeReady = false;
}
```

No `initCascades()` call — only `raysPerProbe` needs updating. The shader reads it as a uniform each dispatch.

### `renderCascadePanel()` — slider with live per-level readout

```cpp
ImGui::SliderInt("Base rays/probe", &baseRaysPerProbe, 4, 8);
ImGui::SameLine();
ImGui::TextDisabled("C0=%d  C1=%d  C2=%d  C3=%d",
    baseRaysPerProbe,     baseRaysPerProbe * 2,
    baseRaysPerProbe * 4, baseRaysPerProbe * 8);
```

Sentinel in `render()` handles cascade invalidation — no dirty flag needed here.

### `renderSettingsPanel()` — fix hardcoded C0-only display (line ~1790)

Current (wrong):
```cpp
ImGui::Text("Probe grid: 32^3, rays/probe: %d", cascades[0].raysPerProbe);
```

Replace with per-level breakdown:
```cpp
ImGui::Text("Probe grid: 32^3  base=%d rays  (C0=%d C1=%d C2=%d C3=%d)",
    baseRaysPerProbe,
    cascades[0].raysPerProbe, cascades[1].raysPerProbe,
    cascades[2].raysPerProbe, cascades[3].raysPerProbe);
```

---

## Performance Note

| Config | Total rays dispatched |
|---|---|
| Before 4b (8 all levels) | 8 × 4 = 32 |
| After 4b (default base=8) | 8+16+32+64 = 120 (3.75×) |
| Slider at base=4 | 4+8+16+32 = 60 |
| Slider at base=32 | 32+64+128+256 = 480 |

Shadow ray cost (`inShadow()` — 32-step march) dominates over ray count. For a static scene that bakes once, 120 rays is acceptable. Base=32 (480 rays) may take a few seconds.

---

## Expected Visual Change

- C3 probe values smoother / less noisy (64 rays vs 8)
- Cleaner merge data flowing C3 → C2 → C1 → C0
- Color bleed from red/green walls more consistent across probes
- `surf%` in probe stats more stable (less variance from sparse sampling)

---

## Acceptance Criteria

| Test | Expected |
|---|---|
| Default base=8: C0/C1/C2/C3 ray counts | 8 / 16 / 32 / 64 |
| Slider to base=4: ray counts update without app restart | 4 / 8 / 16 / 32 |
| C3 probe noise at base=8 vs old flat-8 | Visibly smoother / less speckle |
| Mode 6 GI at base=8 | Less noise in indirect contribution |
| `cascadeReady` invalidates on slider change | Cascade rebakes, stats update |

---

## Relation to 4c

Phase 4c (continuous blend) modifies `raymarchSDF()` to return actual hit distance `t` in `.a` instead of `1.0`. The ray count changes in 4b are purely a uniform change — 4c introduces no conflict. More rays in 4b make the blend in 4c more effective by reducing per-probe variance.
