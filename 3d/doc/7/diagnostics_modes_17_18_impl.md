# Phase 2 — Diagnostic Modes 17 (GI-Only) + 18 (Cascade-vs-PT Delta)

**Date:** 2026-05-19
**Trigger:** user asked: "multi bounce should have its own view mode", "debug options or parameters tunning", "what is our next phase". Phase MB shipped but had no dedicated viz; PT reference showed 42% cascade-vs-PT gap but no per-pixel "where is it wrong" tool.
**Status:** Both modes shipped. Mode 17 makes MB toggle visibly amplify (+53% at gain=1.0, +201% at g=2.0). Mode 18 turns the cascade-vs-PT gap into an interactive diagnostic.

---

## Mode 17 — GI-Only isolated

### Purpose

Display the **pure indirect contribution** from the cascade atlas. Zero direct, zero ambient, zero shadow. Toggling Phase MB visibly brightens this view because MB adds to the atlas.

### Implementation

[raymarch.frag mode-switch addition](../../res/shaders/raymarch.frag):
```glsl
else if (uRenderMode == 17) modeColor = indirectColor;
```

That's it. Reuses the existing `indirectColor = albedo × sampleDirectionalGI(...).irrad` computation; just stops compositing direct + ambient.

### Measured impact (cornell-orig at 500 frames)

| Configuration | Mode 17 brightness | Δ vs OFF |
|---|---:|---:|
| MB OFF | 0.07558 | — |
| MB ON g=1.0 | 0.11582 | **+53.2%** |
| MB ON g=2.0 | 0.22747 | **+201.0%** |

Compare to mode 0 (full composite) at same configs: +6.2% / +28.6%. **Mode 17 amplifies the MB visibility ~10×** because direct light isn't diluting the indirect signal.

### When to use

- A/B comparing Phase MB ON vs OFF
- Tuning MB gain slider — watch the image brightness change in real-time
- Inspecting which scene regions benefit most from multi-bounce (e.g., shadowed corners in Cornell)

---

## Mode 18 — Cascade-vs-PT Delta heatmap

### Purpose

Per-pixel **signed bipolar colormap** showing the difference between cascade output and PT truth. Identifies WHERE cascade integration approximation diverges from physical truth. The diagnostic tool for the 42% cascade-vs-PT gap.

### Formula

```glsl
vec3 cascadeOutput = directColor + indirectColor;
vec3 ptTruth       = texture(uPtAccum, vUV).rgb;
float deltaLum     = dot(cascadeOutput - ptTruth, vec3(0.2126, 0.7152, 0.0722));
float normalized   = clamp(deltaLum / divisor, -1.0, 1.0);
// Bipolar colormap:
//   -1 (cascade dim)  → blue
//    0 (match)        → white
//   +1 (cascade bright) → red
```

Default divisor 0.2 picked for Cornell-scale radiance ~0.3 (so most of the cascade-vs-PT gap fits in the [−1, +1] saturation range).

### Implementation

- New `uDeltaHeatmapDivisor` uniform
- New mode-18 branch in raymarch.frag after the SDF intersection
- C++: mode 18 triggers PT dispatch (same path as mode 16)
- GUI: divisor slider (log scale) when mode 18 is selected; tooltip explains color semantics
- CLI usage: `--render-mode=18 --pt-cascade-match=1` for apples-to-apples comparison

### Measured behavior on cornell-orig (PT cascade-match at 1500 spp)

| Configuration | Blue pixels (cascade dim) | Red pixels (cascade bright) | Interpretation |
|---|---:|---:|---|
| MB OFF | 342,876 | 6,840 | **Dominant blue — confirms 42% cascade dimness** |
| MB ON gain=2.0 | 4,190 | 466,200 | **Dominant red — gain=2.0 over-amplifies; cascade now brighter than PT** |

The flip from blue → red as gain ramps up demonstrates the visual tuning workflow: **the user dials gain until the image goes mostly white**, which means cascade ≈ PT. Likely sweet spot for cornell-orig is around gain=1.5-1.7.

### Visualization examples

- `mode18_mb_off.png`: solid blue scene = cascade ~40% dim vs PT
- `mode18_mb_on.png` (gain=2.0): solid red scene = cascade ~30% bright vs PT (over-corrected)

The diagnostic exposes that:
1. **Single-bounce cascade systematically under-integrates** (uniform blue color across geometry)
2. **MB feedback can over-correct** with high gain (uniform red)
3. **There's a tunable sweet spot** somewhere in between — the empirical mapping from "physics" to "matches PT visually" is now exposed as a knob

---

## Why mode 18 is the right diagnostic tool

Before mode 18:
- "Cascade is 42% darker than PT on cornell-orig" (numeric)
- No way to see WHERE the 42% lives
- No interactive feedback for tuning experiments

After mode 18:
- Per-pixel visualization of the gap
- Real-time response to settings changes (MB gain, Phase 3 toggle, smoothstep parameters)
- Enables targeted investigation: "the blue is concentrated on the floor near the colored walls — color-bleed integration loss"

This is the **measurement infrastructure** for the next phase of quality work: investigating the 38% integration loss. Without mode 18, that work would be guess-and-check; with it, every parameter tweak gives immediate per-pixel feedback.

---

## Files touched

- [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag): +1 uniform, mode 17 branch (1 line), mode 18 branch (~30 lines)
- [src/demo3d.h](../../src/demo3d.h): +1 state member (deltaHeatmapDivisor); range check `[0,17]→[0,18]`
- [src/demo3d.cpp](../../src/demo3d.cpp): +1 initializer, +1 uniform binding, +2 picker labels, +mode 18 inline GUI panel, +mode 17/18 helper text
- Dispatch trigger updated: PT dispatches on mode 16 OR 18

---

## Recommended workflow with these tools

1. **Render in mode 16** to convergence (~10+ seconds at default).
2. **Switch to mode 18** with same camera. PT accumulator stays valid (no reset).
3. **Compare**: blue regions = cascade dim there; red = cascade bright; white = match.
4. **Identify the worst regions** visually (e.g., "the back wall is solid blue").
5. **Toggle cascade settings** (MB on/off, Phase 3 on/off, gain values) and watch how the heatmap responds.
6. **Hypothesize what's causing the gap** in dominant-color regions; investigate the responsible code path.

This is the new quality-investigation loop: **change setting → observe delta heatmap → iterate**. Mode 18 is the missing measurement that makes the 38% integration loss attackable.

---

## Next steps

The diagnostic tools are now in place. Real quality work begins:

1. **Use mode 18 + PT cascade-match to find the integration-loss hotspots** on cornell-orig and sponza-master
2. **Targeted attack on identified hotspots**: e.g., per-cascade D bumping, smoothstep tuning, Phase 3 v3 re-evaluation, per-bin visibility (C4 follow-up from earlier docs)
3. **Each attack** measured by mode 18 RMSE delta + PT-RMSE numeric metric — concrete pass/fail per change
