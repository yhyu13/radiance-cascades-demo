# Critique: Leak-Suspect Heatmap — Render Mode 14

**Document reviewed:** `leak_suspect_heatmap_impl.md`
**Date:** 2026-05-18

---

## Strengths

1. **Directly answers the user's question.** The user asked "how do we see potential leaks? Just by eyes? Can we do leak detection by some pseudo ground truth judgement?" Mode 14 does exactly this — it introspects the atlas and surfaces what α=0 bins carry. This is a practical diagnostic tool, not a research artifact.

2. **Sqrt scale rev is a genuine improvement.** The rev 1 linear scale produced "163k red pixels OFF vs 152k ON = −6.6%" which is hard to see visually. Rev 2's sqrt scale surfaces 42k pixels with >20 diff and max 178/255 per-pixel change — far more visually informative. The decision to revise the scale based on observed saturation is the right call.

3. **Honest about what the heatmap doesn't tell you.** The "Why this is a pseudo-ground-truth signal" section correctly identifies that the heatmap shows leak *existence* in the atlas, not leak *correctness* or *severity relative to a reference*. This prevents over-interpretation.

4. **No new uniforms or CLI flags.** Reusing the existing render-mode system and adding one shader function is minimal footprint. Clean integration.

---

## Weaknesses / Concerns

### W1 (MEDIUM) — The `sampleProbeDirWithLeak` duplicates the entire `sampleProbeDir` loop structure

The document describes `sampleProbeDirWithLeak` as a new shader function. This function presumably iterates over D² bins, reads the atlas, computes `wcos`, and accumulates — which is exactly what `sampleProbeDir` already does, except the leak version adds `(1 - a.a)` as a weight multiplier. This means two nearly-identical D² loops in the same shader: one for normal rendering, one for leak detection.

This is a maintenance problem: if `sampleProbeDir` changes (e.g., new sampling pattern, different bin mapping), `sampleProbeDirWithLeak` must change identically, or the leak visualization diverges from what the renderer actually computes. The two functions are coupled by convention, not by code structure.

**Better approach:** `sampleProbeDir` could return both the normal irradiance AND the leak potential as a `vec4` (`.rgb` = irradiance, `.a` = leak potential). The render-mode dispatch at the call site chooses which component to display. This eliminates the duplicate loop and guarantees the leak computation stays synchronized with the render computation. One function, one loop, two outputs.

### W2 (MEDIUM) — The heatmap conflates two different sources of "α=0 with nonzero RGB"

Phase 2's α encoding: α=0 for surface hit AND sky exit. Both produce `(1 - α) = 1` in the heatmap formula. But the leak semantics differ:
- **Surface hit (α=0):** the bin's ray hit geometry. The `.rgb` it carries is the direct-lit surface radiance from `hit.rgb * l + upperDir.rgb * (1 - l)`. If the smoothstep zone `(1 - l) > 0`, the surface-hit bin carries some upper-cascade radiance that "went through" the hit surface — that's genuine bake-side leak.
- **Sky exit (α=0):** the bin's ray reached the sky (open volume boundary). The `.rgb` it carries is sky radiance. `(1 - α) = 1` flags it as "occluded" in the heatmap, but sky radiance is NOT leak — it's correctly "what you see when looking at the sky." Flagging sky bins as red leak-suspect is a false positive.

The document's limitation #2 mentions EMA-α temporal smoothing, but doesn't address the sky/surface-hit semantic ambiguity. In an open Cornell box, many bins reach sky through the top opening — those will show as red in the heatmap, inflating the "leak" count with non-leak sky radiance.

**Fix:** The heatmap formula should distinguish sky exit from surface hit. Options:
- (A) Use only bins where α≈0 AND `hit.a > 0` (surface hit, excluding sky). But the render shader doesn't have access to `hit.a` — it only sees the final atlas α, which conflates surface and sky.
- (B) Add a per-bin flag in the atlas (e.g., `.a = -1` for sky, 0 for surface, 1 for miss — per critic 09 W3 and critic 15 N3). This would let the heatmap exclude sky bins. But this requires a format change.
- (C) Accept the conflation and document it explicitly: "the heatmap includes sky-exit bins as 'leak suspects.' Sky radiance is not genuine leak but the heatmap cannot distinguish it from surface-hit leak. In scenes with large open areas (like the Cornell box top), the heatmap will over-report leak by including sky contributions."

Option C is pragmatic for now; the document should at least acknowledge this false-positive source.

### W3 (LOW) — The "measured results" section mixes two different scaling schemes without a clear transition narrative

The document shows rev 1 (linear) and rev 2 (sqrt) results side by side. The rev 1 table has "Red pixels: 163,390 → 152,541 = −6.6%." The rev 2 table has "Red pixels: 299,018 → 294,872" — a much larger absolute count, which makes sense because sqrt expands the range (more pixels qualify as "red" at the same divisor). But the rev 2 delta is −4,146 (−1.4%), which is *numerically smaller* than rev 1's −6.6%, despite the document claiming sqrt "surfaces a much bigger per-pixel response." This is confusing: the per-pixel response is bigger (42k pixels with >20 diff), but the aggregate red-pixel count change is smaller. The document explains this ("sqrt mapping spreads the contrast across the perceptually-relevant range"), but the raw numbers look contradictory at first glance.

The document should add a brief note: "the aggregate red-pixel delta is smaller with sqrt because sqrt expands the baseline red-pixel count (from 163k to 299k), making a −10k absolute reduction appear as a smaller percentage. The per-pixel diff metric (42k pixels with >20 change) is the more meaningful signal under sqrt scaling."

### W4 (LOW) — Limitation #2 about EMA-α is understated

"EMA-α temporal smoothing affects α" — the temporal fix blends α over frames with a soft EMA, producing intermediate α values (e.g., α=0.3 for a bin that was hit in some frames and missed in others). The heatmap formula `(1 - α)` then gives 0.7 for such bins — flagging them as "70% leak" even though the α is genuinely reflecting temporal uncertainty, not bake-side leak. In practice, this means the heatmap shows "leak" in bins that are merely temporally unstable, not genuinely leaking radiance through walls. The limitation should state this more explicitly: "bins with soft α (EMA-blended hit/miss) contribute to the heatmap as partial leak, but this reflects temporal oscillation, not geometric leak. The heatmap is most accurate on fully-converged probes with binary α."

### W5 (LOW) — The divisor default (0.5) is scene-dependent with no auto-calibration

Limitation #3 acknowledges this. But the document doesn't explain why 0.5 was chosen as the default. Was it calibrated against Cornell? Sponza? If a user loads a bright outdoor scene, the heatmap saturates to all-red and becomes useless until they manually adjust the divisor. The GUI slider helps, but there's no guidance on what divisor value works for what scene brightness range.

A minor improvement: add a tooltip or note suggesting "start with divisor = scene mean luminance × 2" or similar heuristic. Or add an auto-calibrate option that reads the first frame's leak potential range and sets the divisor accordingly.

### W6 (LOW) — "Phase 3 v3" is referenced but never defined in this document

The results compare "OFF" vs "Phase 3 v3 ON". The Phase 3 impl doc defines v1 and v2 (targeted scope), but v3 is not defined there. What is v3? Is it v2 with epsilon tuning? Per-bin LUT? Something else? The document should either define v3 inline or link to the definition. Without this, the results section references an undefined variant.

---

## Summary

A practical diagnostic tool that directly answers the user's question about leak visualization. The sqrt-scale revision is a real improvement. Main concerns:

1. **W1 (MEDIUM):** `sampleProbeDirWithLeak` duplicates `sampleProbeDir`'s loop — maintenance coupling risk. Better: have `sampleProbeDir` return both irradiance and leak potential as a vec4.
2. **W2 (MEDIUM):** Sky-exit bins (α=0) are false positives in the heatmap — the formula can't distinguish surface-hit leak from sky radiance. Document should acknowledge this conflation explicitly.
3. **W3 (LOW):** Sqrt-scaled results show smaller aggregate red-pixel delta but larger per-pixel diff — the apparent contradiction needs a brief explanatory note.
4. **W6 (LOW):** "Phase 3 v3" is referenced but undefined — link or define inline.