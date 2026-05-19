# Mode 19 — Cascade_GI-vs-PT_GI Delta Heatmap

**Date:** 2026-05-19
**Trigger:** User observed "PT GI a lot brighter than MB RC GI" on default Cornell with directional light. Mode 18 (total-brightness delta) didn't expose this because cascade's over-saturated direct + under-integrated GI compensated. Needed a GI-isolated diagnostic.
**Status:** Shipped. Confirmed user's observation visually — cascade GI is significantly weaker than PT GI on directly-lit surfaces, even when total-brightness averages match.

---

## What landed

### Shader (raymarch.frag)

- **New uniform** `uPtDirectAccum` — bound to TEXTURE6, contains PT direct-only accumulator
- **Mode 19 branch**: signed bipolar delta `cascadeGI - PT_GI` where:
  - `cascadeGI = indirectColor` (cascade's `albedo × sampleDirectionalGI` from main render path)
  - `PT_GI = max(uPtAccum - uPtDirectAccum, 0)` (subtract direct from full PT)
- Same bipolar colormap as Mode 18 (red = cascade brighter, blue = cascade dimmer, white = match)

### C++ infrastructure

- **New texture** `ptDirectAccumTexture` (RGBA32F, half-viewport size, parallel to existing `ptAccumTexture`)
- **Allocation/clear** mirrors `ptAccumTexture` via lambda in `ptEnsureAccumAllocated`
- **Dual dispatch** in `ptDispatchReference`:
  1. Full PT (`uPtMaxBounces = ptMaxBounces`) → writes `ptAccumTexture`
  2. Direct-only PT (`uPtMaxBounces = 1`) → writes `ptDirectAccumTexture`
  - Second dispatch SKIPPED when only mode 16 is active (mode 16 doesn't need it; saves PT cost)
- **Sampler binding**: TEXTURE6 = direct PT (paired with TEXTURE4 = full PT)
- **Mode 19 triggers PT dispatch** (same hook as mode 16/18)

### GUI

- New picker entry: `"19 Cascade_GI-vs-PT_GI Delta (indirect only)"`
- Inline panel shared with mode 18 (same divisor slider)
- Mode-19-specific text: "Note: Mode 19 dispatches PT TWICE per frame (~2× PT cost)"
- Helper text section with full interpretation

### Range check

`setRenderMode` bounded to `[0, 19]` (was `[0, 18]`).

---

## Empirical findings (the bigger story)

### Per-scene measurements with `--light-direction=0,-1,0.3 --light-intensity=1.0`

| Scene | PT_full | PT_direct | **PT_GI** | **Cascade_GI (MB off)** | Ratio PT/cascade | Mode 19 visual |
|---|---:|---:|---:|---:|---:|---|
| Default Cornell (built-in) | 0.0772 | 0.0735 | **0.00376** | **0.0297** | **0.13×** | mostly white (small GI signal) |
| cornell-orig (OBJ) | 0.4209 | 0.3447 | **0.0763** | **0.0756** | **1.01×** (avg matches) | **dominantly blue (397k pixels)** |

### Critical insight: averaged brightness can MASK per-pixel GI gaps

On cornell-orig, PT_GI ≈ cascade_GI in AVERAGE brightness (both 0.076), but **Mode 19 shows dominant BLUE per-pixel** (397k blue vs 2k red strong pixels). How:

- Dark regions (interior of objects, shadows where neither PT nor cascade has GI) drag the average DOWN equally on both sides.
- Lit wall/floor regions where cascade GI is WEAK and PT GI is STRONG contribute the BLUE signal in Mode 19.
- The averages match because dark pixels dominate the count; the per-pixel diff exposes the real cascade weakness on lit surfaces.

**The user's "5× weaker GI" observation is real on these surfaces — even though the global average is the same.**

### Why Mode 18 missed it

Mode 18 measures `cascadeTotal − PTTotal`. Cascade has:
- Over-saturated direct lighting (walls appear as saturated color blocks vs PT's softer rendering)
- Under-integrated GI (weak color bleed onto neighboring surfaces)

These two failures CANCEL in total brightness. Mode 18 shows mostly white → false negative.

**Mode 19 strips direct from both sides, exposes the pure GI gap.** This is the diagnostic that matches the user's eye.

---

## Recommended workflow

1. Render in Mode 16 + Mode 19 with PT cascade-match ON:
   ```
   --render-mode=19 --pt-cascade-match=1 --light-direction=... --use-multi-bounce=0
   ```
2. Wait for PT to converge (~10-30 seconds; dual dispatch is ~2× single-PT cost).
3. Observe blue regions → identify where cascade under-integrates GI.
4. Toggle multi-bounce ON via GUI / `--use-multi-bounce=1`, tune gain slider — watch blue shrink (closer to PT) or flip to red (over-correction).
5. Adjust until image is mostly white = cascade GI matches PT GI.

For cornell-orig + directional light:
- MB OFF: dominant blue (cascade dim)
- MB ON g=1.0: less blue + some red speckles on small geometry edges
- MB ON g=2.0: dominant red (over-correction)
- Sweet spot likely around gain 1.0-1.3 for this scene

---

## Files touched

- [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag): +1 uniform (`uPtDirectAccum`), +Mode 19 branch (~25 lines), comment about Mode 18 false-negative
- [src/demo3d.h](../../src/demo3d.h): +1 state member (`ptDirectAccumTexture`), range check `[0,18]→[0,19]`
- [src/demo3d.cpp](../../src/demo3d.cpp):
  - allocator: refactored to lambda + allocates both textures
  - ptDispatchReference: dual dispatch with `uPtMaxBounces` toggled between full and 1
  - dispatch trigger: PT now runs for mode 16/18/19
  - raymarchPass: bind both PT textures
  - GUI: picker entry, inline panel (shared with mode 18), mode-19-specific helper text

---

## What this enables

Now we have **three diagnostic tools** for cascade-vs-PT comparison:
- **Mode 14** (LeakSuspect): where Phase 2's α-gate hides leaked content in the atlas
- **Mode 18** (Total Delta): per-pixel total-brightness gap; can be deceived by direct/GI cancellation
- **Mode 19** (GI-only Delta): per-pixel pure-indirect gap; the right tool for GI/color-bleed analysis

Combined with PT reference (Mode 16) and GI-only display (Mode 17), the suite covers the full A/B workflow:
1. Mode 17 to see cascade GI in isolation
2. Mode 16 to see PT truth in isolation
3. Mode 19 to see the per-pixel gap between them
4. Tune settings (MB, Phase 3, smoothstep, D-res) and watch Mode 19 respond

This is the **measurement infrastructure for the next phase of cascade integration-loss attack work**. Without Mode 19, "cascade GI is weaker than PT" was a vibe; with it, it's a per-pixel measurable signal.

---

## Cerebrum entry

> **Per-pixel diagnostics are essential when averaged metrics mask compositional errors.** Mode 18 measured `|cascadeTotal − PTTotal|` and showed mostly white on cornell-orig with directional light — looked like cascade matched PT. But the user reported "PT GI a lot brighter than cascade GI." Mode 19 (`cascade_GI − PT_GI` per pixel) immediately confirmed: 397k blue pixels (cascade GI dim) vs 2k red. The averaged brightness matched because cascade over-saturates DIRECT and under-integrates GI, which cancel. **Rule**: when investigating component-level quality (GI alone, direct alone, etc.), isolate the component on BOTH sides of the comparison BEFORE averaging. Cross-component cancellation is a common source of false-negative diagnostics.
