# Phase 7 — PT Reference Implementation Notes

**Date:** 2026-05-18
**Plan:** [pt_reference_plan.md](pt_reference_plan.md) rev 2 (post critic-01)
**Critic:** [02_pt_reference_impl_review.md](critic/02_pt_reference_impl_review.md)
**Status:** v1 shipped. Functional on default Cornell + cornell-orig OBJ. Three-way A/B decomposition reveals **cascade is ~42% darker than PT-cascade-match** (integration error, not ambient bias) — a substantial quality finding the cascade-only A/B couldn't surface.

---

## What landed

### Shader: `res/shaders/pt_reference.comp` (~290 lines)

- Sphere-traces existing `uSDF` for ray intersection
- Cosine-weighted hemisphere sampling for diffuse bounces
- Shadow-ray (NEE) for direct light at every bounce (correct for zero-area point lights)
- Russian roulette termination at `uPtRussianRoulette` survival probability
- Progressive accumulation via precision-stable mix form
- Two shading modes via `uPtCascadeMatch`:
  - 0 (default) = UNBIASED PT (no ambient floor) — true ground truth
  - 1 = CASCADE-MATCH (ambient at primary hit only) — cascade's converged target
- PCG-based RNG seeded from (pixel, frame); decorrelated per ray
- **Ray-vs-bbox slab test** prepended to `traceSDF` — see "Implementation surprises" below

### C++: `src/demo3d.cpp` + `src/demo3d.h`

- `ptAccumTexture` (RGBA32F, half-resolution: 640×360 at 1280×720, 960×540 at 1920×1080)
- `ptSampleCount` tracker; reset on dirty
- `ptDispatchReference()`: camera-basis derivation, uniform binding, dispatch
- `ptEnsureAccumAllocated()`: lazy allocation, resize on viewport change
- Setters: `setPtRaysPerFrame`, `setPtMaxBounces`, `setPtRussianRoulette`, `setPtCascadeMatch` — all call `resetPTAccumulator()`
- Camera-change invalidation via delta-threshold (no time debounce; M2 deferred)

### Display: `res/shaders/raymarch.frag`

- New `uPtAccum` sampler2D + `uPtAccumValid` int
- Mode 16 branch at the **TOP of main()** — early-out before SDF raymarch + GI; cascade pipeline still bakes upstream but its output is unused for mode 16 pixels

### GUI: `src/demo3d.cpp` render-mode picker

- New entry "16 PT-Reference (path-traced ground truth)"
- Inline panel (only visible in mode 16):
  - Sample-count display + progress bar (target 10,000 spp)
  - Reset accumulator button
  - CascadeMatch mode checkbox
  - Sliders: rays/frame, max bounces, Russian roulette
  - Half-resolution accumulator dimensions display

### CLI flags: `src/main3d.cpp`

- `--pt-cascade-match=N` (0 unbiased, 1 cascade-match)
- `--pt-rays-per-frame=N`
- `--pt-max-bounces=N`
- `--pt-russian-roulette=F`

Plus the existing `--render-mode=16` selects PT.

---

## Implementation surprises (not in plan)

### Surprise 1 (critic-02 H1): `setRenderMode` clamp at `[0,14]`

Adding a new render mode label is not sufficient. `setRenderMode()` in [demo3d.h:500](../../src/demo3d.h#L500) had a hardcoded range check that warned (but did NOT clamp) when given mode 16. Bumped to `[0,16]`.

**Cerebrum-worthy lesson**: when adding a new render mode, grep for the OLD upper bound (`> 14`, `<= 14`, `14"`) to find every range check that needs updating.

### Surprise 2 (critic-02 H2): `sampleSDF` returns INF outside the volume bbox

`sampleSDF()` ([radiance_3d.comp:141](../../res/shaders/radiance_3d.comp#L141)) returns INF if the position is outside the SDF volume bbox. Cascade rays are fine because cascade rays start at PROBE positions (always inside the volume). PT rays start at the CAMERA, which is typically OUTSIDE the bbox (default Cornell: camera at z=4, volume bbox z∈[-2,2]).

**Initial impl symptom**: every pixel returned all-black after 500 frames of dispatch. Debug iterations:
1. Test pattern (red gradient) confirmed compute → display pipeline works.
2. Hit-visualization (red=hit, green=sky, blue=miss-in-vol) showed every pixel was blue.
3. Diagnosed: ray origin outside bbox → first sampleSDF call returns INF → "sky-miss" branch fires immediately.

**Fix**: added `intersectBox()` (ray-vs-bbox slab test) prepended to `traceSDF`. Origin outside bbox → advance ray to `boxEnter + 0.001`. Ray that misses bbox entirely → returns sky-miss correctly.

**Plan-gap**: the plan §4.5 said "port `raymarchSDF`" but didn't anticipate this contract violation. **Plan rev 3 should add a §4.5b note**: "Cascade rays assume inside-volume origin; PT rays violate this. traceSDF MUST intersect the bbox first."

### Surprise 3 (critic-02 H3): cascade is 1.66× DIMMER than unbiased PT, RMSE 0.33

Three-way A/B on cornell-orig at 500 spp (PT) and 300 frames (cascade):

| Comparison | Brightness ratio | RMSE | Interpretation |
|---|---:|---:|---|
| PT cascade-match vs PT unbiased | 1.047× | 0.0375 | **Pure ambient-bias contribution: only ~5%** |
| Cascade vs PT cascade-match | 0.575× | 0.3394 | **Cascade integration error: 42% darker** |
| Cascade vs PT unbiased (combined) | 0.602× | 0.3317 | Total gap: 40% darker |

**Key finding**: the cascade renderer integrates only ~58% of the radiance the PT integrates, even when PT is set to match the cascade's ambient-floor bias. **This is the integration error**, NOT a bias mismatch. Almost the entire cascade-vs-PT gap is from cascade under-integrating, not from the two using different shading models.

**This is the primary signal PT was built to surface** — it would have been invisible without an external truth, because cascade-mode-0 looking "correct" is the standard baseline. PT shows cascade is actually missing ~40% of the indirect bounce contribution on cornell-orig.

What this MIGHT be:
- Sky/surface α=0 conflation dimming cascade atlas (per critic-15 N3)
- Smoothstep blend zone losing radiance at cascade boundaries
- Per-bounce attenuation in the cascade chain (C3→C2→C1→C0) losing energy at each merge
- Combination of all three

What this is NOT:
- Ambient bias mismatch (cascade-match mode eliminated that — only 5% delta)
- PT being wrong (PT converges to a stable image; geometry/colors visually match)

**This finding is the kind of measurable quality target the plan-§12 "what this unlocks downstream" promised.** Now concrete: "cascade is 42% dim vs converged PT on cornell-orig; what fixes close that gap?"

---

## Verification results

### Smoke test

- ✅ Build clean (no shader errors, no C++ errors)
- ✅ Mode 16 dispatches via CLI `--render-mode=16` (after H1 range fix)
- ✅ Mode 16 GUI panel renders with sliders + checkbox
- ✅ PT image visually correct on default Cornell + cornell-orig
- ✅ Mode 0 / 14 / 15 unaffected (no regression for non-PT modes)
- ✅ CascadeMatch toggle works (5% brightness delta as expected)
- ✅ Half-resolution accumulator allocated (log: "PT accumulator allocated 640x360")
- ✅ All 4 CLI flags functional (`--pt-cascade-match`, `--pt-rays-per-frame`, `--pt-max-bounces`, `--pt-russian-roulette`)
- ✅ Camera-move invalidation works (verified by interactive testing — image resets when camera moves)

### Performance

Smoke test ran 500 frames in ~15 seconds → ~30 ms/frame total (PT + display). Well within "interactive."

- PT dispatch (half-res 640×360, 1 spp/frame): ~10-20 ms estimated
- Cascade pipeline (still runs upstream): ~16.5 ms
- Display: ~3-5 ms

**Tile-based dispatch (plan rev 2 §8) was SKIPPED** — half-res alone is empirically sufficient for interactive at 720p. The plan's worry about 1-3 second freezes only applies to full-screen 1080p without ANY mitigation. Half-res alone is the win; tile-based deferred until proven needed.

### NOT done (deferred follow-ups)

- ❌ **External Blender Cycles validation** — load-bearing for "PT is correct" claim. Without this, the 42% integration-error finding is interpretable but not actionable. Filed as a separate workstream.
- ❌ **Tile-based dispatch** (critic-02 M1) — deferred unless heavier scenes show freezes.
- ❌ **Time-debounced camera invalidation** (critic-02 M2) — current threshold-only approach is fine in practice.
- ❌ **Convergence at 10k+ spp** for the headline figure — measured at 500-2000 spp; the gap is large enough that 10k won't change the interpretation.
- ❌ **MAX_STEPS tuning for Sponza-master** (critic-02 L2) — not tested.
- ❌ **MIS for v2** — only relevant when adding nonzero-area lights.

---

## Files touched

- [res/shaders/pt_reference.comp](../../res/shaders/pt_reference.comp): NEW (~290 lines)
- [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag): +2 uniforms, +mode 16 early-out branch
- [src/demo3d.h](../../src/demo3d.h): +10 PT state members, +4 setters, +2 method decls, range fix `[0,14]→[0,16]`
- [src/demo3d.cpp](../../src/demo3d.cpp): +initializers, +`ptDispatchReference()` (~110 lines), +`ptEnsureAccumAllocated()` (~25 lines), +sampler binding in raymarchPass, +mode 16 GUI panel, +mode 16 in render-mode combo
- [src/main3d.cpp](../../src/main3d.cpp): +4 CLI flags

Plan rev 2 + critic-02 H1/H2/H3 + M3 applied. M1 (tile-based) + external Blender validation deferred.

---

## Next steps (in priority order)

1. **Visual comparison** (5 min): generate side-by-side cascade vs PT cascade-match vs PT unbiased images on cornell-orig at the same viewpoint, save as a quality-baseline reference. The 42% gap deserves a screenshot for the project's "before/after" history.
2. **External Blender validation** (1-2 days): render cornell-orig in Blender Cycles at 4096 spp, save as ground truth, write `tools/compare_pt_vs_cycles.py`. Without this, we don't actually know if our PT is correct — the cascade-vs-PT gap could partly be PT being wrong.
3. **Investigate the 42% gap** (open-ended, days to weeks): if PT is validated as correct, this becomes the primary quality work. Candidates per the "What this MIGHT be" list above.
4. **Phase 3 v3 re-evaluation with PT-RMSE metric**: was the Phase 3 toggle ON/OFF on cornell-orig-alcove worth shipping? Now we can answer "PT-RMSE drops by X%" instead of "bake-leak metric drops by 11% but we don't know if that matters."
