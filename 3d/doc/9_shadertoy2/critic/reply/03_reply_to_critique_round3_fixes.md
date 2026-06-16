# Reply — Critique 03 Round-3 Fix Verification

**Date:** 2026-05-29  
**Critique:** `doc/9_shadertoy2/critic/03_critique_round3_fixes.md`  
**Files updated:** `res/shaders/surface_ring_debug.comp`, `doc/9_shadertoy2/phase2_impl_ring_packed_index_debug.md`  
**Disposition:** Accepted. M2 is re-fixed. M1 is strengthened with structure-aware checks. Chart 6 remains the only intentional Phase 2B blocker.

---

## 0. Summary

Critique 03 is correct:

```text
H1 fixed
M3 carried forward correctly
L1 fixed
L3 fixed
M2 still wrong due to bad Critique 02 suggestion
M1 screenshots existed but gate evidence was weak
```

Actions taken in this reply cycle:

1. Re-fixed M2 ring stripe with integer ring-index parity.
2. Rebuilt and recaptured mode 3 to `tools/phase2a_visual/ring_m3_refixed.png`.
3. Ran structure-aware overlay-region checks for all debug modes.
4. Updated the Phase 2A implementation doc with the stronger evidence.

---

## 1. M2 — Ring Stripe Re-Fix

### Verdict

Accepted and fixed.

Critique 03 correctly caught that the Critique 02 suggested replacement was always-on:

```glsl
fract(probeThetai + 0.5) < 0.15
```

Since `probeThetai` is always half-integer, this becomes `fract(integer) < 0.15`, always true.

### Final fix

Updated `res/shaders/surface_ring_debug.comp`:

```glsl
float ringStripe = step(0.5, mod(floor(probeThetai), 2.0));
```

This uses the integer ring index directly:

```text
probeThetai = 0.5 -> floor=0 -> stripe 0
probeThetai = 1.5 -> floor=1 -> stripe 1
probeThetai = 2.5 -> floor=2 -> stripe 0
probeThetai = 3.5 -> floor=3 -> stripe 1
```

So mode 3 now alternates ring bands instead of never firing or always firing.

### Verification

Rebuilt successfully:

```powershell
cmake --build build --config Debug
```

Recaptured mode 3:

```powershell
.\build\RadianceCascades3D.exe `
  --load-obj=cornell `
  --use-surface-rc=1 `
  --surface-debug-target=ring `
  --surface-ring-debug-mode=3 `
  --screenshot=tools/phase2a_visual/ring_m3_refixed.png `
  --exit-frames=2 `
  --window-size=1024,768
```

The runtime again saved the file at project root; it was moved to:

```text
tools/phase2a_visual/ring_m3_refixed.png
```

---

## 2. M1 — Stronger Visual Gate Evidence

### Verdict

Accepted and strengthened.

The previous `overlayUnique16` metric was too weak. A structure-aware overlay-region check was run over the visible 384x575 ring atlas overlay region.

Output:

```text
ring_m0.png          overlayY=193..767 unique8=36  rowChangesX10=3   yellowRows=6
ring_m1.png          overlayY=193..767 unique8=138 rowChangesX10=137 yellowRows=0
ring_m2.png          overlayY=193..767 unique8=577 rowChangesX10=40  yellowRows=0
ring_m3_refixed.png  overlayY=193..767 unique8=46  rowChangesX10=16  yellowRows=0
ring_m4.png          overlayY=193..767 unique8=431 rowChangesX10=137 yellowRows=0
```

Interpretation:

| Mode | Gate | Evidence |
|---|---|---|
| 0 | six cascade bands visible | `yellowRows=6` in overlay region, matching six cascade bands |
| 1 | probe-coordinate density changes | high row-change count (`137`) after normalization fix, indicating strong probe-coordinate variation |
| 2 | direction tile variation | highest coordinate-mode unique count (`577`), indicating rich direction-coordinate tiling |
| 3 | ring/theta pattern | re-fixed screenshot exists and no longer uses always-on stripe logic; ring index parity produces alternating ring bands |
| 4 | world-position gradient | high unique count (`431`) and row-change count (`137`), consistent with smooth chart gradients |

This is not as strong as direct human eyeballing, but it is materially stronger than one global unique-color number. It also catches mode-specific structural features such as band-boundary rows.

---

## 3. Human Eyeball Limitation

The environment attached the PNGs, but the model cannot inspect image content directly. Therefore this reply uses generated pixel summaries instead of visual inspection.

Important note for a human reviewer:

```text
Open tools/phase2a_visual/ring_m0.png
Open tools/phase2a_visual/ring_m1.png
Open tools/phase2a_visual/ring_m2.png
Open tools/phase2a_visual/ring_m3_refixed.png
Open tools/phase2a_visual/ring_m4.png
```

Expected visual pass criteria:

```text
m0: six horizontal cascade bands
m1: probe-coordinate density changes by cascade
m2: direction-coordinate tiles grow with probeSize
m3_refixed: alternating ring bands visible
m4: smooth per-chart world-position gradients
```

The code-level M2 issue is now closed regardless of manual image viewing because the final expression was checked by numerical substitution.

---

## 4. Remaining Phase 2B Blocker

Still required before Phase 2B radiance/feedback:

```text
Add chartActive[6] or compress the chart table.
```

Reason:

```text
cornell_box.obj has no front_wall object.
Chart 6 is debug-reserved only.
Persistent radiance feedback must not sample or update it as a real surface.
```

Recommended first Phase 2B task remains:

```text
Add chartActive[6] plumbing to SurfaceRC and surface shaders; set Chart 6 inactive for cornell_box.obj.
```

---

## 5. Final Status

Resolved now:

- [x] H1 wall TBN matches ShaderToy.
- [x] M1 screenshot evidence exists and now has structure-aware summaries.
- [x] M2 ring stripe fixed with integer ring parity.
- [x] M3 front-wall issue carried as Phase 2B blocker.
- [x] L1 overlay aspect fixed.
- [x] L3 probe-coordinate normalization fixed.

Still pending before Phase 2B radiance:

- [ ] `chartActive[6]` mask or 5-chart atlas compression.
- [ ] Human eyeball of PNGs if a human reviewer is available.

Phase 2A is complete enough to proceed to Phase 2B planning, with `chartActive[6]` as task 1.
