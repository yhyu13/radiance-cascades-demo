# Phase 3.5 — Self-Critique & Debugging Checklist

**Date:** 2026-04-23  
**Branch:** 3d  
**Purpose:** Exhaustive review of Phase 3 implementation before closing it out.  
**Files reviewed:** `radiance_3d.comp`, `raymarch.frag`, `demo3d.cpp`, `demo3d.h`

---

## Bug 1 — Radiance Debug Panel Always Shows Cascade 0 (Correctness)

**Location:** [demo3d.cpp:598](../../../src/demo3d.cpp#L598)

**Symptom:** You can switch to C1/C2/C3 via the "Render using cascade" radio buttons and the main render updates correctly. But the radiance debug panel (top-right 400×400) always shows C0's probe data regardless of selection.

**Root cause:**
```cpp
// renderRadianceDebug() — hardcoded to cascades[0]
glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);  // WRONG
```

`raymarchPass()` correctly uses `selectedCascadeForRender`, but `renderRadianceDebug()` was never updated to match.

**Fix:**
```cpp
int selC = std::max(0, std::min(selectedCascadeForRender, cascadeCount - 1));
if (!cascades[selC].active || cascades[selC].probeGridTexture == 0) return;
// ...
glBindTexture(GL_TEXTURE_3D, cascades[selC].probeGridTexture);
// also update the resolution uniform if needed:
int res = cascades[selC].resolution;
```

**Impact:** The radiance debug panel is useless for inspecting C1/C2/C3 until this is fixed. The C0-only view was misleading — the panel appeared to respond to cascade-level switching but wasn't.

---

## Bug 2 — Probe Stats Never Refresh After Scene Changes (Dead Code)

**Location:** [demo3d.cpp:385](../../../src/demo3d.cpp#L385)

**Symptom:** The per-cascade probe stats table (non-zero%, maxLum, meanLum) shows correct values after the first cascade bake but never updates when the scene changes, the merge toggle is flipped, or any other invalidation occurs.

**Root cause:** The `probeDumped` reset is dead code — the condition is structurally impossible to reach:

```cpp
// Flow within a single frame:
// 1. cascadeReady = false  (SDF or merge toggle change)
// 2. updateRadianceCascades() runs
// 3. cascadeReady = true   ← set here
// 4. if (!probeDumped && cascadeReady) { probeDumped = true; ... readback ... }
// 5. if (!cascadeReady) probeDumped = false;  ← NEVER fires, cascadeReady is true
```

`probeDumped` stays `true` after the first readback and is never reset.

**Fix:** Move the reset to immediately before the cascade dispatch:
```cpp
if (!cascadeReady) {
    probeDumped = false;          // ← reset here so readback triggers after update
    double t0 = GetTime();
    updateRadianceCascades();
    cascadeTimeMs = (GetTime() - t0) * 1000.0;
    cascadeReady = true;
}
// Remove the dead `if (!cascadeReady) probeDumped = false;` line entirely
```

**Impact:** After switching scenes or toggling "Disable Merge", the stats table shows stale values from the previous bake. The merge toggle A/B test (non-zero% jumps from ~3% to ~80%) cannot be verified without this fix.

---

## Bug 3 — Ambient Term Inconsistency (Minor Correctness)

**Location:** [radiance_3d.comp:108](../../../res/shaders/radiance_3d.comp#L108) vs [raymarch.frag:287](../../../res/shaders/raymarch.frag#L287)

**Symptom:** The probe computation and the primary ray direct shading use different ambient floors, causing indirect lighting stored in probes to be darker than the direct shading on surfaces.

```glsl
// radiance_3d.comp — probe computation
vec3 color = albedo * (diff * uLightColor + vec3(0.02));  // ambient = 0.02

// raymarch.frag — primary ray direct shading
vec3 surfaceColor = albedo * (diff * uLightColor + vec3(0.05));  // ambient = 0.05
```

**Fix:** Use the same ambient value in both. `0.05` is the better choice — it matches what the viewer sees in direct-only mode (mode 4).

```glsl
// radiance_3d.comp: change 0.02 → 0.05
vec3 color = albedo * (diff * uLightColor + vec3(0.05));
```

**Impact:** Shadowed regions in the cascade are darker than they appear under direct lighting, making the indirect contribution look slightly "off" when comparing mode 4 vs mode 0.

---

## Missing Feature 1 — Tutorial Panel Shows Phase 1 Stub Status

**Location:** [demo3d.cpp:1950-1956](../../../src/demo3d.cpp#L1950-L1956)

**Symptom:** The "3D Radiance Cascades - Quick Start" panel still displays:
```
✗ SDF generation (placeholder)
✗ Full raymarching (placeholder)
```
Both are fully implemented as of Phase 1. By Phase 3, this text actively misleads any user who opens the panel.

**Fix:** Replace the "Current Features" block with an accurate Phase 3 status list:
```cpp
ImGui::Text("Phase 3 Status:");
ImGui::BulletText("✓ Analytic SDF + albedo volume (64^3)");
ImGui::BulletText("✓ SDF-guided primary raymarching");
ImGui::BulletText("✓ 4-level radiance cascade (C0-C3)");
ImGui::BulletText("✓ Cascade merge (C3->C2->C1->C0)");
ImGui::BulletText("✓ Merge toggle + per-level probe stats");
ImGui::BulletText("✓ 7 render modes (0-6) incl. GI-only");
```

---

## ~~Missing Feature 2 — Light Position~~ (RETRACTED — original was correct)

**Original claim:** Light at y=0.8 was "too low", should move to y=1.6.

**Why it was wrong:** The Cornell Box inner room half-size is `hs=1.0`, so the room spans Y=[-1.0, 1.0]. The ceiling inner face is at Y=1.0. y=0.8 is already near the top of the room (80% height). Moving to y=1.6 places the light ABOVE the ceiling outer face (y=1.4), causing every probe shadow ray to hit the ceiling — `inShadow()` returns true for all probes, GI goes completely dark.

**Correct value:** `y=0.8` — do not change it.

---

## Dead Code — `cascadeDisplayIndex` Member

**Location:** `demo3d.h`, `demo3d.cpp:98`

`int cascadeDisplayIndex` is initialized to `0` but never read or written after initialization. It predates `selectedCascadeForRender` and was superseded by it. Can be removed from both `demo3d.h` and the constructor initializer list.

---

## Summary and Fix Priority

| # | Severity | Description | File | Fix effort |
|---|---|---|---|---|
| 1 | **Bug** | Radiance debug panel hardcoded to `cascades[0]` | demo3d.cpp:598 | 3 lines |
| 2 | **Bug** | `probeDumped` reset is dead code; stats freeze | demo3d.cpp:339,385 | 2 lines |
| 3 | Medium | Ambient 0.02 vs 0.05 inconsistency | radiance_3d.comp:108 | 1 line |
| 4 | Medium | Tutorial panel shows Phase 1 stub status | demo3d.cpp:1950 | ~10 lines |
| 5 | Low | Light at y=0.8 should be y=1.6 for ceiling feel | demo3d.cpp:834,976 | 2 lines (both sites) |
| 6 | Low | `cascadeDisplayIndex` is dead code | demo3d.h / demo3d.cpp | 2 lines |

**Fix order:** Do 1+2 together (they interact with the cascade selector), then 3+5 together (both affect probe energy), then 4, then 6.

---

## Acceptance Criteria for Phase 3.5 Done

- [ ] Radiance debug panel shows the currently selected cascade (C0–C3 radio drives both panels)
- [ ] Toggling merge and switching scenes both refresh the probe stats table
- [ ] Mode 4 (direct) and probe ambient term match: both use `vec3(0.05)`
- [ ] Tutorial panel reflects actual Phase 3 feature status
- [ ] Light at y=0.8 in both probe and raymarch pass (inside the room, not above ceiling)
- [ ] `cascadeDisplayIndex` removed
