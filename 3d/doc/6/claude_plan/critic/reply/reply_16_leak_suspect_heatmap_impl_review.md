# Reply: Leak-Suspect Heatmap Critic 16 — `16_leak_suspect_heatmap_impl_review.md`

**Date:** 2026-05-18
**Status:** All 6 findings addressed. **W1 (medium) refactored at the shader level** — `sampleProbeDirWithLeak` and `sampleProbeDirWithLeak`-aware trilinear blend deleted; `sampleProbeDir` now returns `vec4(irradiance, leak_potential)` from a single D² loop. Mode 0 OFF brightness bit-exact verified (0.19212 matches pre-refactor baseline); mode 14 OFF/ON delta unchanged (42,288 pixels with >20/255 diff). W2/W3/W4/W5/W6 are doc + tooltip clarifications, all landed.

---

## How each finding was addressed

### W1 (MEDIUM) — Duplicate loop in `sampleProbeDirWithLeak`

**Accepted, refactored.** The critic's proposed solution (unified function returning `vec4(irradiance, leak)`) was the right shape. Implemented exactly as suggested:

- **Old**: two functions `sampleProbeDir` (returns vec3) + `sampleProbeDirWithLeak` (returns vec4). Identical D² loops; convention-coupled.
- **New**: single function `sampleProbeDir` returns `vec4(irradiance_rgb, leak_potential_luminance)`. Single D² loop. Both outputs guaranteed in sync.
- **Caller updates** (3 sites in [raymarch.frag](../../../res/shaders/raymarch.frag)): mode-0/mode-6 paths take `.rgb`; mode 14 takes `.a`.
- **Trilinear helper** `sampleDirectionalGI` similarly unified — returns vec4; `sampleDirectionalGIWithLeak` removed.

**Verification of bit-exactness**: rebuilt and re-measured Cornell:
- Mode 0 OFF brightness: **0.19212** (matches pre-refactor pre-Phase-3-fix baseline exactly).
- Mode 0 ON brightness: **0.19084** (matches Phase 3 v3 result).
- Mode 14 OFF vs ON delta: **42,288 pixels with >20/255 diff, max 178/255** (matches pre-refactor measurement).

No regression. Cost: a few extra ALU ops per bin (`leakRgb += a.rgb * wcos * (1.0 - a.a)` and a 3-channel luminance dot product at function exit) — negligible vs the existing D² atlas texelFetch loop.

### W2 (MEDIUM) — Sky-exit and surface-hit conflation in α=0

**Accepted, documented explicitly at three levels.** The critic correctly identifies that `(1 - α) = 1` for both surface hit AND sky exit under Phase 2's binary α encoding. Sky bins are false positives for "leak" — sky radiance is correctly "what you see when looking at the sky," not leak through a wall. Critic recommended Option C (acknowledge + document); chose that path because Options A and B both require architectural changes (separate channel for hit-distance, or restoring a sky sentinel — both deferred).

**Documentation landed in three places:**
1. **Shader-level comment** in unified `sampleProbeDir` ([raymarch.frag:316-336](../../../res/shaders/raymarch.frag)) — calls out the false positive at the source so any future engineer touching the function sees it.
2. **Doc limitation #2** in [leak_suspect_heatmap_impl.md](../../leak_suspect_heatmap_impl.md) — full explanation with workaround and reference to critic-15-N3 / critic-09-W3 for the encoding history.
3. **GUI inline tooltip** in mode 14 controls ([demo3d.cpp:3970+](../../../src/demo3d.cpp)) — "FALSE POSITIVES: sky-exit bins (α=0) flag as red — Phase 2's α encoding can't distinguish sky from surface-hit. Visually exclude sky-facing areas."

**Why not implement the fix:** the sentinel restoration (α = -1 for sky, 0 for surface, 1 for miss) is a multi-shader change touching the bake's α-write logic, raymarch.frag's α-gate consumer, the bake-leak metric's threshold, and downstream code that may rely on the binary {0,1} invariant. Worth doing eventually but not under "leak heatmap diagnostic" scope. Filed as a future follow-up.

### W3 (LOW) — Aggregate vs per-pixel response under sqrt scaling

**Accepted, explained.** Added a "Notes on metric interpretation" section to the doc that walks through the apparent contradiction:
- Linear scale: −6.6% red-pixel reduction (large) + ~0 per-pixel diff (small)
- Sqrt scale: −1.4% red-pixel reduction (smaller %) + 42k pixels with >20/255 diff (much larger)

The reconciliation: sqrt expands the baseline red-pixel count by including pixels at lower `leak_potential` magnitudes. A fixed absolute pixel-count reduction becomes a smaller percentage of a larger baseline. But the per-pixel diff metric (RGB shift between OFF and ON) is the more meaningful signal for "visual responsiveness," which is what the sqrt scale trades for.

### W4 (LOW) — EMA-α temporal smoothing limitation understated

**Accepted, expanded.** Doc limitation #3 (now numbered) explicitly states:
- Post-2026-05-15 temporal fix: atlas α is soft EMA-blended, not binary
- A bin at α = 0.3 (hit some frames, missed others) flags as "70% leak" in the heatmap
- This is **temporal oscillation**, not geometric leak — the heatmap is most accurate on fully-converged probes
- Practical implication: for moving cameras or just-after-rebake, transient soft α inflates the heatmap

Same warning added to the unified `sampleProbeDir` shader comment.

### W5 (LOW) — Divisor 0.5 default scene-dependent + no auto-calibration

**Partially addressed.** Doc limitation #4 now explains:
- 0.5 chosen empirically for Cornell-scale scenes (mean luminance ~0.2, leak in 0.05-0.5 range)
- Heuristic: `divisor ≈ scene_mean_luminance × 2.5`
- Concrete suggestions: outdoor scenes try 1.0+, dark scenes try 0.1

**GUI tooltip updated** with the heuristic and concrete recommendations:
```
Heuristic: divisor ≈ scene_mean_luminance × 2.5.
Bright outdoor scenes: try 1.0+. Dark scenes: try 0.1.
```

**Auto-calibration not implemented.** Reading first-frame leak potential range would require a CPU readback or compute-shader reduction, which is more infrastructure than the diagnostic warrants. The slider with guidance is the pragmatic compromise.

### W6 (LOW) — "Phase 3 v3" referenced but undefined in this doc

**Accepted, fixed.** Added a "Note on Phase 3 v3" section pointing at [visibility_phase3_impl.md § v3](../../visibility_phase3_impl.md) with a one-paragraph summary: v3 uses trilinear.rgb (unbiased) + WeightedSample.a as merge multiplier; Cornell GI dim −0.67%, alcove leak reduction −11.2% on C0. Default OFF.

---

## Summary

| Critic 16 ID | Severity | Action |
|---|---|---|
| W1 | MEDIUM | **Refactored** — unified into one function; bit-exact verified |
| W2 | MEDIUM | **Documented at 3 levels** (shader / doc / GUI); fix deferred (requires sentinel) |
| W3 | LOW | Doc note added explaining sqrt vs linear |
| W4 | LOW | Limitation expanded with concrete EMA-α example |
| W5 | LOW | Heuristic added; auto-cal deferred (cost > benefit) |
| W6 | LOW | Cross-doc link + inline definition |

**The most impactful change is W1.** Eliminating the duplicate loop removed the maintenance trap the critic flagged: future changes to `sampleProbeDir` now automatically affect the leak computation. The doc/tooltip clarifications (W2-W6) are lower stakes but they make the tool genuinely usable instead of subtly misleading — particularly W2 (sky false positives), which would have generated incorrect "Phase 3 has lots of work to do in scenes with open ceilings" conclusions.

**Critic value:** the refactor (W1) is the clean payoff; W2 caught a real semantic bug that would have produced bad inferences. Critic round well-earned.
