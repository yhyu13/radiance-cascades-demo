# Reply to Finding 4 — Fused EMA and EMA Math Error

**Critic finding:** `02_current_codebase_review.md` §4 (Medium)
**Verdict:** Accept in full

---

## Part A: Fused EMA — why our earlier response missed it

When we reviewed whether fused EMA was implemented (see `codex_plan/class/claude_critic/05_fused_atlas_ema_not_implemented.md`),
we searched for class member names:

```
grep: fusedAtlas    → no matches
grep: doFusedEMA    → no matches (searched members only)
```

This was too narrow. `doFusedEMA` is a **local variable** inside `updateSingleCascade()`, not a class member. The codex's line-level citations are correct:

```cpp
// src/demo3d.cpp:1381
const bool doFusedEMA = useTemporalAccum && tb != shaders.end() &&
                        c.probeAtlasHistory != 0 && c.probeGridHistory != 0;
```

With all three conditions true at default settings, `doFusedEMA = true` every time a cascade is updated. The fused path IS the default path.

The dispatch structure is:

```
Normal path (doFusedEMA == true):
  radiance_3d.comp  ← writes EMA-blended result directly into probeAtlasTexture
                       reads probeAtlasHistory as the previous accumulation
  reduction_3d.comp ← averages fused atlas into probeGridTexture
  [handle swap]     ← C++ swaps probeAtlasTexture ↔ probeAtlasHistory
                       and probeGridTexture ↔ probeGridHistory
  temporal_blend.comp is NOT dispatched

Fallback path (doFusedEMA == false, e.g. history textures missing):
  radiance_3d.comp  ← writes fresh bake (no EMA) into probeAtlasTexture
  reduction_3d.comp ← averages into probeGridTexture
  temporal_blend.comp ← blends live into history separately
```

**Correction required in `14_phase9_temporal.md`:**

The "EMA blend" section describes `temporal_blend.comp` as if it runs every frame in the main path. It should instead say:

> The default path ("fused EMA") embeds the EMA blend inside `radiance_3d.comp` itself.
> During the bake dispatch, the shader reads `probeAtlasHistory` (previous accumulation)
> and writes `mix(history, bake, alpha)` directly into `probeAtlasTexture`.
> After the bake, C++ swaps the live and history texture handles. No separate
> `temporal_blend.comp` dispatch is needed.
>
> `temporal_blend.comp` runs only when the history textures are not yet allocated
> (first frame after a cascade resize) — it acts as a warm-start fallback.

The fused path was added in Phase 10 specifically because the separate `temporal_blend.comp`
dispatch was a second full-volume compute pass per cascade per frame.

---

## Part B: EMA convergence math error in `14_phase9_temporal.md`

**What we wrote:**
> At alpha=0.05: after 14 frames the history weight of the initial (stale) value falls
> below `0.05^(1/14) ≈ 50%`, so the history is reasonably converged after ~20-30 frames.

**Why it is wrong:**

The EMA recursion is: `h_n = (1 − α)·h_{n-1} + α·bake_n`

After n bakes starting from `h_0 = stale`:

```
h_n = (1−α)^n · h_0 + α · Σ_{k=1}^{n} (1−α)^{n-k} · bake_k
```

The coefficient on the stale value after n steps is `(1−α)^n`, not `α^(1/n)`.

With `α = 0.05`: `(1 − 0.05)^n = 0.95^n`.

| n | weight on stale value |
|---|---|
| 1 | 0.95 |
| 7 | 0.95^7 ≈ 0.70 |
| 14 | 0.95^14 ≈ 0.49 |
| 28 | 0.95^28 ≈ 0.24 |
| 60 | 0.95^60 ≈ 0.05 |

So the initial stale value is below 50% after 14 updates, but convergence to <5% of stale takes ~60 updates. The 14-frame "reasonably converged" claim in the doc is an overstatement; "halfway converged" is more accurate.

**Correction required in `14_phase9_temporal.md`:**

Replace:
> At alpha=0.05: after 14 frames the history weight of the initial (stale) value falls
> below `0.05^(1/14) ≈ 50%`

With:
> At alpha=0.05, the stale-value coefficient decays as `0.95^n`.
> After 14 frames it is `0.95^14 ≈ 0.49` (half remains). After 60 frames it is `0.95^60 ≈ 0.05`
> (~5% remains). Full convergence takes ~60 frames, not ~14.

---

## Summary of corrections

| Location | Error | Fix |
|---|---|---|
| `14_phase9_temporal.md` EMA blend section | Describes `temporal_blend.comp` as the main path | Describe fused EMA as main path; `temporal_blend.comp` is fallback |
| `14_phase9_temporal.md` convergence paragraph | `0.05^(1/14)` formula is wrong | Use `0.95^n` decay; state convergence takes ~60 frames |
| `15_phase10_staggered.md` | Implies separate `temporal_blend.comp` runs after each staggered update | Clarify: fused path means no separate dispatch; skipped cascade also skips EMA |
| `17_phase12_capture.md` | References `temporal_blend.comp` as the blend path | Add note that fused path embeds EMA into bake shader |
