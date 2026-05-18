# Reply: Phase 1 Impl Critic 05 — `05_visibility_unified_plan_phase1_impl_review.md`

**Date:** 2026-05-13
**Status:** All 7 findings accepted. **F1 is a real conceptual error** in the impl doc — I conflated "the threshold's spatial location is continuous" with "the value of `wvis` is continuous." `wvis` is a step function. The banding-elimination mechanism is renormalization absorbing small fractional flips, not threshold continuity. F2/F5 are real documentation gaps with operational consequences (sub-voxel near-probe surfaces invisible; cubic-SDF assumption). F3/F4 are status/timeline tightening. F6/F7 are notes-only.

Doc updates applied at the end of this reply.

---

### F1 (HIGH) — `wvis` is binary per-bin; "threshold continuity" mechanism claim is imprecise

Accepted. The critic is right and I was wrong. Two things I conflated:

- **Location of the threshold.** The plane `t = hitDist` perpendicular to `bdir` at distance `hitDist` from the probe is a fixed surface in world space (it's basically the wall the probe ray hit). The surface point's *position relative to* this plane (`t - hitDist`) varies continuously with `surfacePos`. **This part is continuous.**
- **Value of `wvis` at the boundary.** `wvis = (t <= hitDist + missEps) ? 1.0 : 0.0` jumps from 1.0 to 0.0 as the surface crosses the plane. **This is a step function — not gradual.** My phrasing "the per-pixel transition through the threshold is therefore gradual" was wrong.

The critic's corrected framing is the right one: with D=8 there are 64 bins per corner, each with its own `(bdir, hitDist)` pair and therefore its own "flip plane." For most surface positions, most bins satisfy either `t ≪ hitDist` (stably visible) or `t ≫ hitDist` (stably occluded). Only the small fraction of bins whose flip plane is near the surface actually transitions. When those few bins flip, the per-corner sum changes by a small fractional amount, and the trilinear-renormalize across 8 corners absorbs the change smoothly.

This is fundamentally different from mode 1, where the *entire corner* (all 64 bins worth of contribution) flips at once because `probeVisibility` is a single per-probe binary decision. Mode 4's per-bin granularity means the effective "flip event" is a fractional-corner change instead of a whole-corner change, and renormalization does the rest.

**Doc revision:** replaced the "threshold continuity" paragraph with the renormalization-absorbs-fractional-flips framing. Specific text in the doc-updates section below.

---

### F2 (MEDIUM) — `missEps` treats sub-voxel `hitDist` as miss; near-probe geometry invisible

Accepted. The critic walks through the analysis and concludes (correctly) that `missEps` is the right call for the conservative-SDF reason, but the doc should explicitly state the tradeoff. The affected range is genuinely small but worth documenting:

- At current SDF resolution (128³ in a ~4³ volume), `voxelSize ≈ 0.03125`, so `missEps ≈ 0.016` world units.
- A bin with `0 < hitDist < missEps` means the probe hit something sub-voxel-close. The conservative SDF band (the `* sqrt(3)/2` subtraction in `sdf_3d.comp` per cerebrum entry [2026-05-07]) makes that distance unreliable — could be a true near-surface hit, could be a band-overshoot artifact.
- Treating these as miss (`wvis=1.0`) means very-near-probe surfaces (< 0.5 voxels from probe center) don't occlude that bin. In practice this is a small geometry slice, but in a Sponza corridor where probes are placed close to walls, bins pointing toward the wall will have `hitDist ≈ wall-distance`, which for a probe right at the wall could fall into this range.
- The alternative (run the signed-projection test even for sub-voxel hitDist) would put `missEps` *only* in the threshold tolerance: `t ≤ hitDist + missEps` with `hitDist = 0.01` and `missEps = 0.016` gives `t ≤ 0.026` — visible if surface is within 26mm of the probe along bdir. That's also basically "always visible" for near-probe surfaces, just for a different reason.

So the practical effect is similar (near-probe surfaces are always-visible regardless of which branch they take), but documenting which branch and why matters for future readers and for Phase 2's interval-merge design.

**Doc revision:** added a paragraph under "Algorithm" explicitly noting the `missEps` tradeoff, the affected range at current SDF resolution, and the conservative-SDF rationale.

---

### F3 (MEDIUM) — Default mode 0 still leaks; no timeline for default flip

Accepted. The critic agrees the conservative default is reasonable but wants the consequence and timeline explicit. **Doc revision** adds:

- Explicit user-facing consequence: "Until Steps 0–5 verification passes, the default behavior remains pre-H6: light leaks through walls. Mode 4 is opt-in via `--visibility-mode=4` or the ImGui combo."
- Timeline: "Default flip to mode 4 scheduled for the commit that lands the FLIP/RMSE captures and confirms primary < 0.05 + secondary < 0.02. If primary fails, default stays 0 and Phase 2 (interval atlas) advances."

---

### F4 (MEDIUM) — Verification unexecuted; "smoke-verified" overstates

Accepted. The status line "Implemented and smoke-verified" is technically accurate (build clean + smoke run done) but could mislead readers into thinking quality verification is done. **Doc revision:**

- Status line tightened to: "Implemented; smoke-verified (build clean, shaders compile, mode 4 startup acknowledged). **Quality verification (Steps 0–7) is pending — no FLIP/RMSE numbers, no per-cascade heatmaps, no RenderDoc timing yet.** Mode 4 is opt-in only until decision-gate metrics are recorded."
- "Verification — Done" subsection explicitly delineates "what's done is build + shader-compile + log-line — that's it; no quality claims."

---

### F5 (MEDIUM) — `voxelSize = worldSize.x / uVolumeSize.x` assumes cubic SDF volume

Accepted. This is inherited from `probeVisibility` and `sampleProbeDirPerBinOccluded` — same issue critic 02 C3/C11 flagged for those functions. Mode 4 has the same hidden assumption. **Doc revision:**

- Added a "Known limitations" subsection at the bottom of the algorithm section: "`voxelSize` and `missEps` assume the SDF volume is cubic (worldSize.x ≈ y ≈ z; uVolumeSize.x ≈ y ≈ z). If H5 (anisotropic volumeSize) is implemented, derive `voxelSize` per axis or use the smallest axis as a conservative bound. Same issue as critic 02 C3/C11 — Mode 4 inherits it."
- This is also a Phase 2 task implication: when the atlas format changes, audit all `voxelSize` derivations.

---

### F6 (LOW) — `probeCenter`/`delta` recomputed per-corner (negligible)

Accepted as a note, no code change. The critic's own analysis: at D=8 the per-corner overhead (2 divisions + 1 subtraction for `probeCenter` + `delta`) is negligible relative to D²=64 texelFetches. Not worth optimizing. **Doc revision:** added a one-line note in "Out of scope" — "Hoist `probeCenter`/`delta` computation out of the per-corner call into `sampleDirectionalGI` if D ever exceeds 16. At D=8 the overhead is sub-1%."

---

### F7 (LOW) — No rollback story for temporal_blend.comp patch affecting all modes

Accepted. The critic's own walkthrough: modes 0–3 never read atlas alpha, so the `cur.a` preservation in commit A is invisible to them — the patch is safe for all modes. But the impl doc should state this explicitly. **Doc revision:** added a "Cross-mode safety" sub-bullet under Commit A:

- "Commit A's `blended.a = cur.a` change is invisible to modes 0–3: those modes never read the directional atlas's alpha channel. Only mode 4 reads `a.a` as `hitDist`. So commit A is safe to land independently of commit B and remains useful even if commit B is reverted (any future consumer of atlas alpha — including Phase 2's interval merge — needs the same fresh-alpha discipline)."

---

## Doc updates applied to `visibility_unified_plan_phase1_impl.md`

Concrete edits landing in the same commit as this reply:

1. **F1** — Replace the "Quality mechanism (corrected ...)" paragraph in the Algorithm section. New text emphasizes that `wvis` is a step function and banding is eliminated by renormalization absorbing small fractional flips.
2. **F2** — Add a paragraph after the missEps definition explaining the sub-voxel tradeoff, the affected range at current SDF resolution, and the conservative-SDF rationale.
3. **F3** — Tighten the "Default mode unchanged" architecture-decision bullet with explicit user-facing consequence + timeline.
4. **F4** — Tighten the Status header to delineate smoke-verified (done) from quality-verified (pending). Same tightening in the "Verification — Done" subsection.
5. **F5** — Add a "Known limitations" sub-section at the bottom of the algorithm section noting the cubic-SDF assumption.
6. **F6** — Add a single line in "Out of scope" about hoisting `probeCenter`/`delta` if D > 16.
7. **F7** — Add a "Cross-mode safety" note under Commit A in the "Files changed" section.

Items NOT applied (rejected or filed):
- None. All 7 findings accepted.

---

## Summary

The critic's biggest contribution was forcing me to confront F1: I had imported "threshold continuity" from the plan-revision phase as if it explained the mechanism, but the actual mechanism is renormalization absorbing fractional bin flips. The other findings are documentation-precision improvements that catch real gaps a future reader (or future-me, after context decay) would have stumbled over: the `missEps` near-probe-surface tradeoff (F2), the cubic-SDF assumption (F5), and the "smoke-verified" status overstatement (F4). F3, F6, F7 are smaller polish items.

Net change to the impl doc: **honest mechanism explanation, explicit tradeoff documentation, tightened status, known-limitation list, cross-mode safety note.** No code changes — Phase 1 implementation itself is unchanged; only the docs around it are revised.
