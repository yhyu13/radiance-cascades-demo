# Phase 3 Completion Learnings

**Date:** 2026-04-22
**Branch:** 3d
**Covers:** Cascade merging (C3→C2→C1→C0), merge toggle UI, mode 6 (GI-only), per-cascade probe stats.

---

## What Was Built

### Cascade Merge Pass

Each cascade now reads from the coarser level above it when a ray misses its interval band.
Dispatch order reversed to **coarse→fine** so each level's data is ready before the finer level reads it:

```
C3 dispatched first  (no upper cascade — coarsest)
C2 dispatched second (reads C3 on miss)
C1 dispatched third  (reads C2 on miss, which already merged C3)
C0 dispatched last   (reads C1 on miss — full chain)
```

Merge equation per ray (isotropic approximation, no per-direction storage):
```glsl
if (hit.a > 0.0) {
    totalRadiance += hit.rgb;              // local hit in this interval
} else if (uHasUpperCascade != 0) {
    totalRadiance += texture(uUpperCascade, uvwProbe).rgb;  // far-field from coarser level
}
```

After merging, C0 probes at the room center accumulate light via:
`C0 miss → C1 value = C1 miss → C2 value = C2 hit (wall at 0.5–2m) → wall color`

This is the first true multi-level GI path in the demo.

### Merge Toggle

`disableCascadeMerging` bool exposed as **"Disable Merge"** checkbox in the Cascades panel.
Toggling it sets `cascadeReady = false` via a `lastMergeFlag` sentinel, forcing all 4 levels to recompute on the next frame. Lets the user A/B between:
- **Merge OFF**: each cascade independent → C0 nearly all-zero for interior probes
- **Merge ON**: upper levels fill interior → C0 non-zero% jumps to 60–80%+

### Mode 6 — GI-Only

New render mode showing only the cascade indirect contribution, **no direct shading**:

```glsl
vec3 indirect6 = texture(uRadiance, uvw).rgb;
fragColor = vec4(clamp(indirect6 * 2.0, 0.0, 1.0), 1.0);  // raw linear, ×2
```

Key design choices:
- **No ACES tone mapping, no gamma** — intentionally raw linear
- **×2 scale** — makes subtle color bleed visible without washing out
- Sky pixels (ray misses volume) stay black — shows exactly where GI reaches

### Per-Cascade Probe Stats

Readback loop extended to cover all 4 cascade levels. The Cascades panel now shows a colour-coded table:

```
C0 [0.02, 0.12]:  82.3%   max=0.847   mean=0.0412   ← green = healthy
C1 [0.12, 0.50]:  61.5%   max=0.903   mean=0.0318   ← green
C2 [0.50, 2.00]:  44.1%   max=0.951   mean=0.0271   ← yellow
C3 [2.00, 8.00]:   3.2%   max=0.210   mean=0.0008   ← red = expected (mostly out-of-volume)
```

Colour thresholds: red < 10%, yellow 10–50%, green > 50%.
The C0 non-zero% is the single best merge health indicator.

---

## Bugs Found and Fixed

### Bug 1 — Mode 6 looked identical to Mode 0

**Symptom:** "GI only (6) and Final (0) look exactly the same."

**Root cause:** Mode 6 originally applied `toneMapACES` + gamma, the same pipeline as mode 0.
ACES compresses both `indirect` and `direct + indirect` into a similar output range when both are bright (probe maxLum ≈ 0.95). After ACES, `toneMap(0.6)` ≈ `toneMap(1.2)` — both land near 0.8–0.9.

**Fix:** Raw linear output, no tone map, no gamma, ×2 scale:
```glsl
// Wrong — ACES wipes out the distinction:
fragColor = vec4(toneMapACES(indirect6), 1.0);
fragColor.rgb = pow(fragColor.rgb, vec3(1.0 / 2.2));

// Correct — linear preserves the difference:
fragColor = vec4(clamp(indirect6 * 2.0, 0.0, 1.0), 1.0);
```

**Lesson:** Debug modes that share the production tone-mapping pipeline will look like the production render. Raw-linear debug views must opt OUT of tone mapping explicitly. The contrast between a raw-linear view and the final tone-mapped view is itself a useful diagnostic.

**Mode comparison table (correct intent):**

| Mode | Pipeline | What it shows |
|---|---|---|
| 0 Final | `(direct + indirect) → ACES → γ` | Full render |
| 4 Direct only | `direct → ACES → γ` | No GI |
| 6 GI only | `indirect × 2 → clamp` | Raw cascade contribution |

Modes 4 + 6 should look like mode 0 when mentally added — that's the visual sanity check.

### Bug 2 — Merge toggle did not trigger recompute

**Symptom:** Checking/unchecking "Disable Merge" had no effect until scene switch.

**Root cause:** `cascadeReady` is a `static bool` inside `render()`. Changing `disableCascadeMerging` in `renderUI()` (which runs after `render()`) didn't reset `cascadeReady` because there was no watch on the flag.

**Fix:** Track previous state with a second static and reset on change:
```cpp
static bool lastMergeFlag = false;
if (disableCascadeMerging != lastMergeFlag) {
    lastMergeFlag = disableCascadeMerging;
    cascadeReady  = false;  // forces recompute next frame
}
```

**Lesson:** Any runtime parameter that affects the cascade bake must be tracked with a "previous value" sentinel and compared each frame. The pattern `if (param != lastParam) { lastParam = param; cascadeReady = false; }` is the standard invalidation idiom here.

### Bug 3 — Array members can't use initializer-list syntax

**Symptom:** Compile error when changing `int probeNonZero` → `int probeNonZero[MAX_CASCADES]` while keeping `probeNonZero(0)` in the member initializer list.

**Fix:** Remove array members from the initializer list; zero them in the constructor body:
```cpp
std::memset(probeNonZero, 0, sizeof(probeNonZero));
std::memset(probeMaxLum,  0, sizeof(probeMaxLum));
std::memset(probeMeanLum, 0, sizeof(probeMeanLum));
```

**Lesson:** C++ aggregate arrays can't be value-initialized in an MIL (member initializer list) with parenthesis syntax. Use `memset` or `= {}` brace syntax in the body. Alternatively, use `std::array<float, MAX_CASCADES>` which CAN be MIL-initialized.

---

## Architecture Insights

### Merge is isotropic — directional information is lost

The merge samples the upper cascade at the **probe's own world position** (not the ray endpoint):
```glsl
vec3 uvwProbe = (worldPos - uGridOrigin) / uGridSize;
// ...
totalRadiance += texture(uUpperCascade, uvwProbe).rgb;
```

This means: when a ray in direction D misses its interval, the far-field contribution is the upper cascade's **average over all directions** at the probe center — not the radiance specifically from direction D.

Effect: indirect lighting has no directionality. A surface probe can't tell if the far-field light came from left or right; it contributes the same average to all 8 ray directions. This produces correct total energy but incorrect shadowing (no hard GI shadows, no directional color bleeding per surface normal).

Proper directional merging requires per-direction storage (spherical harmonics, octahedral maps). This is Phase 4+ territory.

### C3 is mostly empty for a 4-unit box

C3 interval [2.0, 8.0] starts at 2.0m. The Cornell Box half-size is also 2.0m, so center probes' C3 rays start right at the walls and immediately overshoot into out-of-volume space (`sampleSDF` returns INF). C3 non-zero% ≈ 3% is expected and correct — it captures diagonal corner paths and edge probes, not interior ones. It becomes meaningful in larger scenes.

### Dispatch order matters, not just existence of upper cascade binding

If cascade levels were dispatched fine→coarse (as the original code did), each level would read from an UNINITIALIZED texture. The coarse cascade must be written before the fine cascade reads from it. The reversed loop is the core of the merge:

```cpp
// Wrong (fine→coarse): C0 reads C1 before C1 is written
for (int i = 0; i < cascadeCount; ++i) { updateSingleCascade(i); }

// Correct (coarse→fine): C1 is ready when C0 reads it
for (int i = cascadeCount - 1; i >= 0; --i) { updateSingleCascade(i); }
```

### The `cascadeReady` static is a shared gate for all levels

A single `static bool cascadeReady` governs all 4 dispatches. Any parameter change that affects ANY level must reset it. Currently tracked:
- SDF changes (`!sdfReady`)
- Merge toggle (`disableCascadeMerging != lastMergeFlag`)

NOT yet tracked (would need their own sentinels if exposed in UI):
- `raysPerProbe` changes
- `selectedCascadeForRender` changes (doesn't need recompute — only affects which texture is bound)

### Probe readback is slow but acceptable for static scenes

Calling `glGetTexImage` 4× per recompute adds a GPU→CPU stall. For 4 × 32³ × 4 channels × 4 bytes = ~2MB total, this is negligible for a static scene that recomputes only on SDF/merge changes. Would need async PBO readback for dynamic scenes (Phase 4+).

---

## Visual Test Results (Acceptance Criteria)

| Test | Expected | Interpretation |
|---|---|---|
| C0 non-zero% with merge ON | > 60% (green) | Upper cascades filling interior probes ✓ |
| C0 non-zero% with merge OFF | < 10% (red) | Only wall-adjacent probes see light ✓ |
| Mode 6 vs Mode 0 | Clearly different appearance | Raw linear vs tone-mapped ✓ |
| Mode 6 vs Mode 4 | Both different; mode 6 = soft fill, mode 4 = sharp Lambertian | ✓ |
| Merge ON vs OFF in mode 0+GI | Visible brightness/fill difference | ✓ |

---

## What Phase 4 Needs

- **Directional probe storage** (SH2 or octahedral): enables proper per-direction merge, removes the isotropic approximation limitation
- **Temporal accumulation**: re-use probe values across frames; only recompute a fraction of probes per frame (jittered update)
- **Async probe readback** (PBO): avoid GPU stall on `glGetTexImage` if scenes become dynamic
- **Adaptive probe resolution**: reduce probe count for upper cascades (e.g., C1=16³, C2=8³) to save memory and compute
