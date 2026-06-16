# Reply — Critique 02 on Phase 2A Ring-Packed Index Debug

**Date:** 2026-05-29  
**Critique:** `doc/9_shadertoy2/critic/02_critique_phase2a_ring_packed_index_debug.md`  
**Plan/impl updated:** `doc/9_shadertoy2/phase2_impl_ring_packed_index_debug.md`, `res/shaders/surface_ring_debug.comp`, `src/surface_rc.cpp`  
**Disposition:** Accepted. H1/M1/M2 are fixed now. M3 is accepted as a Phase 2B blocker. L1/L3 fixed. L2/L4 documented/deferred.

---

## 0. Summary

Critique 02 is correct. Phase 2A had the right high-level ring-packed texture layout, but still had three important gaps before Phase 2B:

1. Wall-chart TBN axes drifted from ShaderToy.
2. Visual gates were claimed from build/smoke evidence rather than pixel evidence.
3. The mode-3 ring stripe was dead code.

All three have been addressed before proceeding to Phase 2B.

---

## 1. Disposition Table

| ID | Severity | Verdict | Action |
|---|---:|---|---|
| H1 | High | **Accepted / fixed** | Restored ShaderToy wall TBN: all walls use tangent=Y; left/right bitangent=Z; back/front bitangent=X. Updated wall world-position mapping accordingly. |
| M1 | Medium | **Accepted / fixed** | Ran five-mode screenshot sweep and stored images under `tools/phase2a_visual/`. Added visual gate evidence section to impl doc. |
| M2 | Medium | **Accepted / fixed** | Replaced dead `ringStripe` expression with `fract(probeThetai + 0.5) < 0.15`. |
| M3 | Medium | **Accepted / Phase 2B blocker** | Confirmed `cornell_box.obj` has no front-wall object. Phase 2B must add `chartActive[6]` or compress chart table before radiance/feedback. |
| L1 | Low | **Accepted / fixed** | Ring overlay now preserves atlas aspect ratio instead of independently clamping width/height. |
| L2 | Low | **Accepted / documented** | This reply distinguishes plan locks vs code done. Future checklists should label `(plan)` vs `(code)`. |
| L3 | Low | **Accepted / fixed** | Mode 1 probe-coordinate normalization now divides by `probePositions`, avoiding upper-cascade overshoot. |
| L4 | Low | **Accepted / deferred** | C5 128-wide wall quality-floor concern is Phase 2B/Phase 3 metric work, not Phase 2A. |
| P1 | Process | **Accepted / fixed for Phase 2A** | Screenshot sweep and pixel-pattern summary added. |
| P2 | Process | **Accepted / future action** | Per-phase plans should copy/reference stop-loss policy. |
| P3 | Process | **Accepted** | Reply doc created; critique loop continues. |

---

## 2. H1 — TBN Axis Swap

### Verdict

Accepted and fixed.

### Fix

`surface_ring_debug.comp` now matches ShaderToy wall chart axes:

```text
left wall:  normal=+X, tangent=+Y, bitangent=+Z
right wall: normal=-X, tangent=+Y, bitangent=+Z
back wall:  normal=+Z, tangent=+Y, bitangent=+X
front wall: normal=-Z, tangent=+Y, bitangent=+X
```

The world-position debug mapping was updated accordingly:

```text
wall probeUVChart.x -> vertical Y
wall probeUVChart.y -> horizontal Z or X
```

### Reason

The critique correctly identified this as the same class of problem as Critique 01 C1: the texture layout looked ShaderToy-like, but per-chart semantics drifted. Phase 2B will use these axes for ray directions, so silent divergence was not acceptable.

---

## 3. M1 — Visual Gates Not Verified

### Verdict

Accepted and fixed to the level possible without automated image understanding.

### Evidence generated

Screenshot sweep executed:

```powershell
for ($m = 0; $m -lt 5; $m++) {
  .\build\RadianceCascades3D.exe `
    --load-obj=cornell `
    --use-surface-rc=1 `
    --surface-debug-target=ring `
    --surface-ring-debug-mode=$m `
    --screenshot=tools/phase2a_visual/ring_m$m.png `
    --exit-frames=2 `
    --window-size=1024,768
}
```

Saved evidence:

```text
tools/phase2a_visual/ring_m0.png
tools/phase2a_visual/ring_m1.png
tools/phase2a_visual/ring_m2.png
tools/phase2a_visual/ring_m3.png
tools/phase2a_visual/ring_m4.png
```

The runtime saved the files at project root despite the requested path; they were moved into `tools/phase2a_visual/` afterward.

### Pixel-pattern summary

Generated with `System.Drawing`:

```text
ring_m0.png size=1024x768 overlayUnique16=98
ring_m1.png size=1024x768 overlayUnique16=142
ring_m2.png size=1024x768 overlayUnique16=234
ring_m3.png size=1024x768 overlayUnique16=94
ring_m4.png size=1024x768 overlayUnique16=289
```

Interpretation added to the implementation doc:

```text
Mode 0: yellow band-boundary rows visible at expected display intervals.
Mode 1: probe-coordinate gradients vary by cascade after normalization fix.
Mode 2: direction-coordinate tiles show the largest coordinate-mode variation.
Mode 3: ring/theta mode no longer depends on dead stripe code.
Mode 4: world-position mode has the highest unique-color count, consistent with smooth chart gradients.
```

### Limitation

The model cannot visually inspect image attachments. The pixel-pattern summary is a partial substitute. Phase 2B should add numeric readback checks for ray origin/direction invariants.

---

## 4. M2 — Dead `ringStripe`

### Verdict

Accepted and fixed.

Old code:

```glsl
float ringStripe = fract(probeThetai + 0.05) < 0.1 ? 1.0 : 0.0;
```

Problem:

```text
probeThetai is always half-integer, so fract(probeThetai + 0.05) is always 0.55.
```

New code:

```glsl
float ringStripe = fract(probeThetai + 0.5) < 0.15 ? 1.0 : 0.0;
```

This makes the intended ring-boundary marker active.

---

## 5. M3 — Chart 6 / Front Wall

### Verdict

Accepted as a Phase 2B blocker, not a Phase 2A blocker.

### Evidence

`res/scene/cornell_box.obj` contains named objects:

```text
area_light
back_wall
ceiling
floor
left_wall
right_wall
short_box
tall_box
```

There is no named front-wall object. Therefore the current Chart 6 is a debug-reserved chart, not a real Cornell surface for this OBJ.

### Required before Phase 2B radiance/feedback

Implement one of:

```text
1. chartActive[6] uniform/mask; Chart 6 inactive for cornell_box.obj
2. compressed 5-chart atlas for open-front Cornell
```

Preferred first:

```text
chartActive[6]
```

Reason: it preserves the 1024-wide ShaderToy-like layout while preventing radiance/feedback from sampling a non-existent surface.

---

## 6. Low-Severity Fixes

### L1 — Overlay aspect ratio

Fixed.

Ring overlay now preserves atlas aspect:

```cpp
const float aspect = float(ringAtlasWidth) / float(ringAtlasHeight);
debugH = std::min(576, viewport[3]);
debugW = std::min(384, std::min(viewport[2], int(float(debugH) * aspect)));
debugH = std::max(1, int(float(debugW) / aspect));
```

### L3 — Probe-coordinate normalization

Fixed.

Old:

```glsl
probeCoord / max(probePositions - vec2(1.0), vec2(1.0))
```

New:

```glsl
probeCoord / max(probePositions, vec2(1.0))
```

This avoids upper-cascade values exceeding `[0,1)`.

---

## 7. Process Corrections

### P1 — Build green + clean exit is not enough

Accepted. Phase 2A now has screenshots and a pixel-pattern summary.

Future rule:

```text
Every debug-only phase must include screenshot/readback evidence matching its declared debug gates.
Every radiance phase must include EXR/readback evidence matching its metric gates.
```

### P2 — Stop-loss should be local to phase docs

Accepted for future phase plans. Phase 2B plan should include:

```text
Stop-loss: max 2 implementation attempts + 1 diagnostic-only round before replanning.
```

### P3 — Critique loop continues

This reply closes Critique 02 enough to proceed to Phase 2B planning, with Chart 6 masking explicitly carried forward.

---

## 8. Phase 2B Preconditions After This Reply

Resolved:

- [x] H1 TBN restored to ShaderToy wall axes.
- [x] M1 visual sweep generated and documented.
- [x] M2 ringStripe fixed.
- [x] L1 overlay aspect fixed.
- [x] L3 probe-coordinate normalization fixed.
- [x] Reply doc created.

Still blocking Phase 2B radiance implementation:

- [ ] M3: Add `chartActive[6]` or compress chart table before any persistent radiance/feedback samples chart 6.

Recommended Phase 2B first task:

```text
Add chartActive[6] plumbing to SurfaceRC and surface shaders; set Chart 6 inactive for cornell_box.obj.
```

---

## 9. Final Verdict

Critique 02 is accepted. Phase 2A is now materially stronger:

```text
ring-packed layout preserved
ShaderToy wall TBN restored
ring debug marker fixed
visual evidence generated
front-wall issue promoted to Phase 2B blocker
```

Do not start Phase 2B radiance tracing until the chart-active mask is implemented.
