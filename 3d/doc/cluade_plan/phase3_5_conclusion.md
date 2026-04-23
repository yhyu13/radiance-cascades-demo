# Phase 3.5 — Conclusion & Fixes Applied

**Date:** 2026-04-23  
**Branch:** 3d  
**Purpose:** Close out Phase 3 by fixing all bugs identified in the self-critique.

---

## Fixes Applied

### Fix 1 — Radiance Debug Panel Now Follows Cascade Selector ✓

**File:** `src/demo3d.cpp` — `renderRadianceDebug()`

The top-right 400×400 debug panel was hardcoded to `cascades[0].probeGridTexture`. It now mirrors `selectedCascadeForRender` exactly like `raymarchPass()` does — switching C0/C1/C2/C3 in the UI updates both the main render and the debug panel.

```cpp
// Before
glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);

// After
int selC = std::max(0, std::min(selectedCascadeForRender, cascadeCount - 1));
glBindTexture(GL_TEXTURE_3D, cascades[selC].probeGridTexture);
```

---

### Fix 2 — Probe Stats Refresh After Every Scene Change ✓

**File:** `src/demo3d.cpp` — `render()`

`probeDumped` was a static bool that was never reset after the first readback. The reset line `if (!cascadeReady) probeDumped = false;` was dead code — by that point in the frame, `cascadeReady` was always `true`. Result: switching scenes or toggling merge never refreshed the stats table.

Fix: moved `probeDumped = false` to just before the cascade dispatch, removed the dead line.

```cpp
// Before (dead reset — condition never true after the dispatch sets cascadeReady=true)
if (!cascadeReady) { updateRadianceCascades(); cascadeReady = true; }
// ...
if (!cascadeReady) probeDumped = false;  // never fires

// After
if (!cascadeReady) {
    probeDumped = false;       // reset here, before update completes
    updateRadianceCascades();
    cascadeReady = true;
}
```

---

### Fix 3 — Ambient Term Unified to 0.05 ✓

**File:** `res/shaders/radiance_3d.comp`

Probe computation used `vec3(0.02)` ambient; the primary ray shader used `vec3(0.05)`. Shadowed probe regions were darker than the equivalent direct-only view, making the indirect contribution appear incorrectly dim.

```glsl
// Before
vec3 color = albedo * (diff * uLightColor + vec3(0.02));

// After
vec3 color = albedo * (diff * uLightColor + vec3(0.05));
```

---

### Fix 4 — Tutorial Panel Updated to Phase 3 Status ✓

**File:** `src/demo3d.cpp` — `renderTutorialPanel()`

The "Current Features" list still showed Phase 1 placeholder status (`✗ SDF generation`, `✗ Full raymarching`). Replaced with accurate Phase 3 feature list.

---

### Fix 5 — Dead Code `cascadeDisplayIndex` Removed ✓

**Files:** `src/demo3d.h`, `src/demo3d.cpp`

`cascadeDisplayIndex` was initialized to 0 but never read or written. It predated `selectedCascadeForRender` and was never cleaned up. Removed from the header and the constructor initializer list.

---

## Retracted Fix — Light Position (Do Not Change)

The self-critique wrongly identified `y=0.8` as "too low" and suggested moving to `y=1.6`. This was applied and immediately broke GI entirely.

**Why it broke:** The Cornell Box inner room half-size is `hs=1.0`, so the room spans Y=[-1.0, 1.0]. The ceiling inner face is at Y=1.0, outer face at Y=1.4. Placing the light at Y=1.6 puts it above the ceiling — every probe shadow ray passes through ceiling geometry, `inShadow()` returns `true` for all probes, GI goes completely dark.

**Lesson:** `y=0.8` is correct — it is near the top of the room (80% height within [-1,1]), well within the interior. Do not change it.

**Light position stays at:** `glm::vec3(0.0f, 0.8f, 0.0f)` — in both `updateSingleCascade()` and `raymarchPass()`.

---

## Summary

| # | Bug | Status | Impact |
|---|---|---|---|
| 1 | Radiance debug panel hardcoded to C0 | **Fixed** | Debug panel now tracks cascade selector |
| 2 | `probeDumped` never reset — stats freeze | **Fixed** | Stats refresh on every scene/merge change |
| 3 | Ambient 0.02 vs 0.05 mismatch | **Fixed** | Probe ambient matches direct shading |
| 4 | Tutorial panel showing Phase 1 stubs | **Fixed** | Accurate Phase 3 feature status shown |
| 5 | `cascadeDisplayIndex` dead code | **Fixed** | Removed from header + constructor |
| 6 | Light at y=0.8 "too low" | **Retracted** | y=0.8 is correct; y=1.6 kills all GI |

---

## Phase 3 Is Now Closed

All correctness bugs are fixed. The system correctly:
- Bakes 4 cascade levels with coarse→fine merge
- Refreshes probe stats after any invalidation (scene change, merge toggle)
- Shows the selected cascade in both the main render and the debug panel
- Uses consistent ambient and light parameters between probe and raymarch passes

Next: Phase 4 — directional probe storage, scaled rays per level, continuous distance-blend merge.
