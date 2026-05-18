# Critic 05 — Phase 1 Impl (Mode 4 Depth-Aware Per-Bin Visibility) Review

**Date:** 2026-05-13T19:19+08:00
**Target:** [visibility_unified_plan_phase1_impl.md](../visibility_unified_plan_phase1_impl.md)
**Preceding critics:** [03](03_probe_visibility_acceleration_plan_review.md) (algorithm rewrite), [04](04_visibility_unified_plan_review.md) (plan structural review)
**Verified against:** actual shader code at `raymarch.frag:411-464,513-541`, `temporal_blend.comp:78-92`, `demo3d.h:883-904`, `demo3d.cpp:3632-3645`

---

## Summary

The impl is well-structured: two-commit split (prerequisite + mode 4), signed-projection algorithm correctly implements the reply_03/04 corrections, temporal_blend.comp patch landed and matches the bake-side convention, C++ surface is clean and consistent. The critic chain was followed faithfully. These are genuine strengths.

However, 7 findings below identify gaps in correctness, verification completeness, and documentation precision. One is HIGH severity.

---

## Findings

### F1 — `wvis` is still binary per-bin despite "threshold continuity" claim (HIGH)

The impl doc (line 63) restates critic 04 C2's corrected mechanism: "The per-pixel transition through the `t = hitDist` threshold is therefore gradual, and the trilinear blend across 8 corners has no sharp discontinuity. The actual mechanism eliminating banding is **per-pixel threshold continuity**, not per-bin granularity per se."

This is **partially wrong**. `wvis` at line 455 is `(t <= hitDist + missEps) ? 1.0 : 0.0` — a **binary** per-bin gate. For a single probe corner, the per-bin `wvis` can flip from 1→0 as `surfacePos` moves, and this flip is a **discontinuous** function of position (it's a threshold test, not a smooth transition). The claim "gradual" conflates two things:

- **Within one corner's contribution**: `t = dot(delta, bdir)` varies continuously with `surfacePos`, so the *moment* when `wvis` flips is a smooth spatial boundary (a plane defined by `t = hitDist`). But the *value* of `wvis` at that boundary jumps from 1.0 to 0.0 — it's a step function, not a gradual transition.
- **Across the 8-corner trilinear blend**: When one corner's bins flip to `wvis=0`, the renormalization (`num / wsum`) redistributes weight to the remaining visible corners. The total irradiance *magnitude* doesn't jump discontinuously (renormalization absorbs the change), but the *composition* changes — which corners contribute shifts at the boundary. This is the same mechanism as mode 1 (binary + renormalize), just at finer granularity (per-bin vs per-probe).

The reason mode 4 eliminates visible banding is **not** that `wvis` varies continuously — it doesn't. It's that the per-bin granularity means many bins per corner contribute (D²=64 at D=8), so the renormalization redistribution is smoother (fractional changes in corner composition rather than the whole-corner on/off of mode 1). And for bins where `t` is far from `hitDist` (the majority), `wvis` is stable at 1.0 — only the few bins near the occlusion boundary flip, causing a small fractional change in the weighted sum.

**Fix**: Restate the mechanism more precisely: "Mode 4 eliminates visible banding because the per-bin granularity means only a **small fraction** of bins per corner are near the `t = hitDist` threshold at any given surface position. When those bins flip, the renormalization adjusts smoothly because the majority of bins still contribute. This is fundamentally different from mode 1 where the entire corner (all bins) flips at once, causing a large discontinuity in corner composition."

---

### F2 — `missEps = 0.5 * voxelSize` treats all small `hitDist` as "miss" — may over-occlude (MEDIUM)

The code at `raymarch.frag:435` uses `missEps = 0.5 * voxelSize`. Lines 451-452: `hitDist < missEps → wvis = 1.0` (treat near-zero hitDist as miss/transparent).

But `hitDist` in the atlas is the **distance from the probe center to the hit surface along the bin direction**. A `hitDist` of, say, `0.3 * voxelSize` means the probe hit a surface **very close to itself** (within one voxel). This is a legitimate near-probe surface, not a miss. Treating it as transparent (`wvis=1.0`) means near-probe geometry is invisible — the probe contributes radiance as if nothing is nearby, even when it's right next to a wall.

In the Sponza corridor, probes placed near walls will have small `hitDist` values for bins pointing toward the wall. Setting `wvis=1.0` for these means mode 4 **ignores occlusion from nearby surfaces** — the same class of light leak that H6 was designed to fix.

**Possible fix**: Distinguish between actual miss (ray exited the volume without hitting) and near-probe hit. In `radiance_3d.comp`, misses write `hit.a = 0.0` and sky sentinels write `hit.a = -1.0`. The `hitDist < missEps` condition conflates `hit.a ≈ 0.0` (true miss) with `hit.a = small positive` (near-probe hit). The fix is to use `hitDist <= 0.0 + missEps` (miss and sky only) for the transparent case, and let `0 < hitDist < missEps` (near-probe hit) go through the signed-projection test where `t` is likely small and still passes `t ≤ hitDist + missEps`.

Actually, re-examining: if `hitDist = 0.03` and `t = 0.05` (surface slightly further than the hit), the test `t ≤ hitDist + missEps` = `0.05 ≤ 0.03 + 0.015 = 0.045` → **false** → occluded. This IS correct — the surface IS past the near-probe hit. And if `t = 0.02` (surface closer than the hit), `0.02 ≤ 0.045` → **true** → visible. Also correct. So the `missEps` gate only affects `hitDist` values in the range `[0, missEps]`, which are truly ambiguous (sub-voxel distances that the SDF can't resolve). For SDF voxel resolution 128³ in a 4³ volume, voxel size ≈ 0.03125, so `missEps ≈ 0.016`. A `hitDist = 0.01` means the probe hit a surface ~0.3 voxels away — this is below the SDF's resolution and could be a conservative-band artifact. Treating it as miss is arguably safer than treating it as a real hit.

**However**, the impl doc doesn't acknowledge this tradeoff. It should explicitly note: "`missEps` treats sub-voxel `hitDist` as miss because the conservative SDF band (`sdf_3d.comp` SQRT3_OVER_2 subtraction) makes near-surface distances unreliable. This means very-near-probe geometry (< 0.5 voxels) is treated as transparent. At current SDF resolution (128³, voxel≈0.03125), this affects surfaces within ~0.016 world units of a probe center."

---

### F3 — Default mode is 0, not 4 — unclear justification (MEDIUM)

The impl doc states (line 217): "Default mode unchanged (still 0). Plan deliberately holds the default flip until verification metrics land."

But `demo3d.h:904` shows `int visibilityMode = 0;` — the OFF mode that produces light leaks. The doc's earlier summary (line 26) says "Mode 4 acknowledged at runtime" but the default still leaks through walls unless the user explicitly changes it.

This is a reasonable conservative choice (don't change default until verified), but the doc should acknowledge the user-facing consequence: **any user running the default configuration still gets light-leaking through walls**. If mode 4 is "near-mode-0 cost" and "expected to eliminate banding", the risk of promoting it to default is low. The doc should include a specific timeline: "Default flip to 4 scheduled after Steps 0-5 verification passes. Until then, users must explicitly `--visibility-mode=4` or use the ImGui combo."

---

### F4 — Verification steps are listed but unexecuted — no actual quality data (MEDIUM)

The "Verification — To do" section (lines 187-207) lists Steps 0-7 with decision-gate criteria (FLIP < 0.05, RMSE < 0.02), but **none have been executed**. The doc is an implementation note, not a results report. The only completed verification is "build clean" and "smoke run" (shader compilation + log acknowledgment).

This is fine for an impl doc that documents what was done, but the title says "Implementation Notes" and the status says "Implemented and smoke-verified" — which overstates the verification status. The doc should explicitly note: "Quality verification (Steps 0-7) is pending. Mode 4 is opt-in only until decision-gate metrics are recorded."

---

### F5 — `voxelSize = worldSize.x / float(uVolumeSize.x)` assumes cubic SDF volume (MEDIUM)

`sampleProbeDirDepthAware` computes `voxelSize = worldSize.x / float(uVolumeSize.x)` at line 434, identical to `probeVisibility` at line 322. This assumes the SDF volume is cubic (`worldSize.y == worldSize.x`, `uVolumeSize.y == uVolumeSize.x`). If H5 (anisotropic `volumeSize`) is ever implemented, `voxelSize` will be wrong for Y/Z axes, and `missEps = 0.5 * voxelSize` will be too small or too large depending on which axis the probe-to-surface direction favors.

This is the same issue flagged in critic 02 C3. Mode 4 inherits it. The impl doc should flag this as a known limitation that needs fixing when H5 lands: "voxelSize and missEps assume cubic SDF volume; anisotropic volumeSize (H5) requires per-axis voxel sizes."

---

### F6 — `probeCenter` recomputed per-corner — redundant with `delta` (LOW)

`sampleProbeDirDepthAware` computes `probeCenter` at lines 431-432 and `delta = surfacePos - probeCenter` at line 436. This is called for each of the 8 trilinear corners in `sampleDirectionalGI`, meaning `probeCenter` and `delta` are computed **8 times per pixel**. The `surfacePos` is the same across all 8 calls.

This isn't a bug (the function is pure and doesn't have side effects), but it's redundant work — `probeCenter` involves a division (`uAtlasGridSize / vec3(uAtlasVolumeSize)`) that could be computed once and passed as a parameter. At D=8, the per-corner call executes D²=64 iterations of the inner loop, so the overhead of computing `probeCenter` and `delta` (2 divisions + 1 subtraction) is negligible relative to the 64 texelFetches. Not worth optimizing now, but worth noting if D increases.

---

### F7 — No rollback plan if temporal_blend.comp patch causes regressions (LOW)

The two-commit split (A: temporal_blend, B: mode 4) is good for mode 4 rollback. But commit A (the temporal_blend patch) changes the EMA behavior for **all modes** — not just mode 4. If the patch introduces temporal instability in mode 0/1/2/3 (because the old EMA-blended alpha was accidentally useful for some other purpose), reverting commit A would also revert mode 4.

The impl doc doesn't discuss what happens if commit A causes regressions in modes 0-3. It should note: "Commit A changes temporal_blend.comp's alpha handling for all modes. If this causes regressions (e.g., temporal instability in non-mode-4 paths), the fix is to make the `cur.a` preservation conditional on `uVisibilityMode >= 4`, falling back to the old full-vec4 EMA blend for modes 0-3."

Actually, re-examining the code: the old code did `mix(his, cur, uAlpha)` for the full vec4 and `clamp(his, nMin, nMax)` for the full vec4. The new code does `clamp(his.rgb, nMin.rgb, nMax.rgb)` and `blended.a = cur.a`. For modes 0-3 that don't read alpha, the `blended.a = cur.a` change is invisible — they never read the atlas alpha. For mode 4, it's essential. So the patch is safe for all modes. The doc should just note this: "Commit A is safe for modes 0-3 because those modes never read atlas alpha; the `cur.a` preservation only matters for mode 4."

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| F1 | HIGH | `wvis` is binary per-bin; "threshold continuity" mechanism claim is imprecise |
| F2 | MEDIUM | `missEps` treats sub-voxel hitDist as miss — near-probe geometry invisible, tradeoff undocumented |
| F3 | MEDIUM | Default mode 0 still leaks; no timeline for default flip |
| F4 | MEDIUM | Verification steps 0-7 unexecuted; "smoke-verified" overstates status |
| F5 | MEDIUM | `voxelSize` assumes cubic SDF; anisotropic volumeSize (H5) will break missEps |
| F6 | LOW | `probeCenter`/`delta` recomputed per-corner (negligible at D=8) |
| F7 | LOW | Commit A affects all modes but doc doesn't discuss cross-mode safety |

---

## Top 3 action items for reply

1. **F1**: Restate the banding-elimination mechanism precisely — `wvis` is binary per-bin, but banding is eliminated because only a **small fraction** of bins per corner are near the threshold at any position, so renormalization absorbs the flip smoothly. Drop the "threshold continuity" phrasing; it conflates the *location* of the threshold boundary (continuous) with the *value* of wvis at that boundary (discontinuous step).

2. **F2**: Add a paragraph to the algorithm section explicitly acknowledging the `missEps` tradeoff: sub-voxel `hitDist` is treated as miss because the SDF conservative band makes near-surface distances unreliable. Note the affected range (~0.016 wu at current SDF resolution) and that this means very-near-probe geometry (< 0.5 voxels) is treated as transparent.

3. **F5**: Add a known-limitation note: `voxelSize` and `missEps` assume cubic SDF volume. When H5 (anisotropic `volumeSize`) lands, these need per-axis derivation. This is the same issue as critic 02 C3/C11, now inherited by mode 4.