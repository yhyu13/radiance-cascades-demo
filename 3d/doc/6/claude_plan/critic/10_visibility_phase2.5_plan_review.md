# Critic Review 10 — `visibility_phase2.5_plan.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-14
**Verdict:** Plan is structurally sound (the three-sub-phase split is genuinely the right framing) but has **four HIGH-severity issues** — most importantly, **the proposed bake-side leak fix formula in §3.1 is a hand-wave that won't actually work**, and §2.5a.1's bake-leak metric proposal is internally contradicted (the doc itself flags the contradiction in §6 risks but proposes the broken metric anyway). Plan should not be executed in its current form for §2.5b without substantive rework.

---

## HIGH severity

### H1 — The "geometry-aware merge formula" in §3.1 is a hand-wave that doesn't work

The plan proposes:

> If `offset > hit.a` (upper probe is BEYOND the wall this ray hit), the upper cascade is on the opposite side of the wall — its radiance is from a region this surface can also see indirectly via wall-bounce. **Use upper contribution.**
> If `offset < hit.a` (upper probe is BEFORE the wall this ray hit), the upper cascade is on the same side as this probe — its `bdir` ray would also hit the wall. **Block upper contribution.**

This is wrong in three ways:

**1. The "offset along bdir" calculation is meaningless for spatial probe positions.** The plan computes:
```glsl
vec3 toUpper = vec3(upperProbePos) - vec3(probePos);  // probe-grid offset
float offsetAlongBdir = dot(toUpper, bdir);
```

But `upperProbePos` and `probePos` are integer probe indices in different cascades' grids, with different cell sizes (C0 = 32³, C1 = 16³, etc.). The upper probe's "world position" differs from its grid index by a per-cascade `cellSize × index + gridOrigin` mapping. Computing `vec3(upperProbePos) - vec3(probePos)` mixes coordinate systems — the result has no consistent meaning.

The correct calculation needs `upperProbeWorldPos - thisProbeWorldPos` where each is computed via the cascade's `gridOrigin + (probeIdx + 0.5) * cellSize`. The plan glosses over this.

**2. Even with the right world-space calculation, the test is wrong for the wall topology.** Consider: this probe at `p`, ray `bdir`, hit at distance `hit.a`. Upper probe at `p_u`. The plan asks "is `p_u` beyond `p + bdir × hit.a`?" — but the wall isn't a point at `p + bdir × hit.a`; it's a SURFACE that extends laterally. The upper probe could be on the same side of the wall as `p` (no leak) or on the opposite side (could legitimately see past) regardless of axial offset along `bdir`. A vertical wall and an upper probe directly above this probe — `offsetAlongBdir` is 0, but the upper probe is on the SAME side of the wall (above it but not past it laterally).

**3. Even with both fixed, this doesn't address what the upper cascade ACTUALLY contains.** The upper cascade's `upperDir.rgb` for this `bdir` is the radiance the upper probe's OWN `bdir` ray captured. That ray went from the upper probe's world position in direction `bdir`. Whether that ray hit something on the same side or opposite side of the wall depends on the upper ray's geometry, not on the probe-position relationship. The plan's offset test is checking the wrong thing entirely.

**The honest answer:** correctly fixing bake-side leaks with non-co-located cascades is a real research problem. The plan should NOT pretend a one-day shader patch can solve it. Recommended revision: §3.1 should be marked "research-level — predict failure within iteration budget" with a fallback that ships only soft α + v4 normalization (which DON'T require the bake fix to work).

### H2 — §2.5a.1 proposes a metric that the plan itself acknowledges is broken

The plan proposes:
> Sum `bin.rgb × bin.a` across [occluded] bins.

Then immediately:
> **The metric needs revision.** Better: just inspect `bin.rgb` (without multiplying by α) for those occluded bins.

So the plan describes the broken metric in §2.5a.1, then in §6 risks notes it doesn't work. **Why is the broken version in the plan at all?** A reader would implement it before noticing the §6 contradiction.

This is a documentation pathology — the plan should propose the CORRECT metric (`sum bin.rgb without alpha multiply, restricted to bins where alpha=0 in the current bake`) and not waste reader time with the wrong one.

Also, the corrected metric still has issues:
- "Bins where alpha=0" includes both surface hits AND sky exits (per Phase 2 sky α=0 encoding). For a probe inside an alcove, sky-bin RGB might be high (env fill) and that's correct; we don't want to flag it as "leak."
- The right filter is "bins where the geometric direction would be blocked by the partition AND the probe position is in the occluded region." That requires per-test scene-geometry knowledge, not a generic atlas-walking script.

The metric in §2.5a.1 should be re-derived to handle these cases.

### H3 — Phase 2.5a.3's sentinel-α encoding (Option A) silently breaks current consumers

The plan recommends sentinel α=-1 for sky. But **the current `sampleProbeDir`** uses `w = wcos × a.a`, which would give `w = wcos × -1 = -wcos` for sky bins — **negative weights**. This would SUBTRACT sky-bin radiance from the irradiance integral, which is geometrically nonsense.

The plan acknowledges this implicitly by proposing the helper `visibilityWeight(a) = max(a, 0.0)`. But:
- The helper hasn't been added yet.
- The plan doesn't enumerate ALL consumers of atlas alpha that need updating to use the helper.
- The plan claims commit #2 (sentinel encoding) is "bit-exact match" to pre-encoding — but that's only true if every consumer is updated simultaneously. If the renderer uses `a.a` directly anywhere, the sentinel breaks behavior.

Per Phase 2 audit (`uDirectionalAtlas` fetch sites), the renderer reads alpha at `sampleProbeDir` (line 379). After Phase 2 2C, `sampleProbeDirPerBinOccluded` and `sampleProbeDirDepthAware` are deleted, so only that one site exists. But the BAKE shader (`radiance_3d.comp`) also reads upper-cascade alpha via `sampleUpperDir`/`sampleUpperDirTrilinear` (which return vec4). These compute `alpha = thisAlpha × upperDir.a` where `upperDir.a` could be -1 for sky — `thisAlpha × -1 = -thisAlpha` propagating negativity through cascade chains.

The plan's "bit-exact match" claim for commit #2 is wrong. **Either it's not bit-exact (because of the sentinel encoding), or it requires also updating the bake shader's α propagation, which isn't acknowledged.**

### H4 — §3.4 verification "Pass criterion: Sponza RMSE < 0.05" assumes the bake fix is correct without falsifying

The plan's quality A/B verification sets a tighter pass criterion than Phase 2 (0.05 vs Phase 2's 0.064). **But this assumes the bake fix actually preserves multi-bounce energy correctly.** Per H1, the bake fix formula is hand-waved and probably wrong. If the formula doesn't preserve energy, the RMSE will get WORSE not better — possibly hitting Phase 1 v1's 0.10 territory or beyond.

The plan acknowledges this in §6 ("the criterion may need relaxing during iteration"), but the verification step doesn't have a clear fail-and-abort flow. If RMSE comes in at 0.08 (worse than Phase 2's 0.064 but better than v1's 0.10), is that a pass or fail? The plan doesn't say.

**Better verification structure:** binary tiers.
- Tier 1 pass: RMSE ≤ 0.05 AND bake-leak metric ≤ 20% of baseline → ship 2.5b
- Tier 2 acceptable: RMSE ≤ Phase 2's 0.064 AND bake-leak metric ≤ 50% of baseline → ship 2.5b but document the partial improvement
- Fail: RMSE > 0.064 OR bake-leak metric ≥ 80% of baseline → revert, keep Phase 2 default

---

## MEDIUM severity

### M1 — Plan estimates "3–4 days total" but commit-shape table lists 5 commits with no per-commit time

The TL;DR says "3–4 days." The commit-shape table (§5) lists 5 commits. The §2 / §3 / §4 sub-phase headers say "~1 day," "~2 days," "~0.5 day" = 3.5 days. **The numbers are consistent on aggregate but the commit-shape table gives no per-commit time, so a reader can't budget per-commit.**

Minor fix: add per-commit time estimates to the table.

### M2 — Soft α (§3.2) requires sentinel-α encoding (§2.5a.3) but plan implements them in opposite order

§5 lists the commit order:
1. Investigations
2. Sentinel-α encoding
3. **Bake-side leak fix + v4 normalization**
4. Soft α

But §3.2 (soft α) NEEDS the sentinel-α encoding to distinguish surface from sky. Step 2 establishes the encoding; step 4 uses it. ✅ Good.

But step 3 (bake fix) is between them. Does the bake fix REQUIRE sentinel α? Per §3.1, the bake-side leak fix doesn't directly use α encoding; it uses geometric tests. So step 3 should work with EITHER encoding. ✅ OK.

But — and this is the real issue — **what if the bake fix in step 3 changes the alpha values that the bake writes?** Per §3.1's pseudo-code:
```glsl
float contributedAlpha = upperPastWall ? upperDir.a : 0.0;
```

This treats upperDir.a as a propagatable transparency. If sentinel α=-1 for sky is in upperDir, this propagation breaks (per H3). The bake fix and the sentinel encoding are coupled — neither can land cleanly without the other being correctly handled.

The commit ordering is OK in principle, but step 2 (sentinel encoding) MUST update the bake shader's α propagation to handle sentinels (not just the render shader's `sampleProbeDir`). The plan doesn't say this. Per H3, a "bit-exact match" claim for commit #2 would silently fail if it doesn't update the bake.

### M3 — `reduction_3d` audit (§2.5a.2) doesn't say what fixing it would look like

The plan describes the investigation but doesn't propose the fix shape if a real bug is found. "If real: PR to either fix the reduction shader's α-handling or document the cost" is too vague. If the reduction is SUMMING atlas alpha for some statistic, the fix is non-trivial — the new α semantics (transparency, not hit-distance) means the sum has different physical meaning. **What does the user want the reduction pass to compute now?** The plan doesn't answer.

### M4 — §6 risks list "Cornell-orig-alcove auto-fit camera may not show alcove well" but doesn't propose a viewpoint

If the auto-fit doesn't show the alcove, the bake-leak test's visual A/B is useless. Plan should pre-commit a viewpoint (similar to cam.md for Sponza) — e.g., "alcove-cam: pos `(0.6, 1.0, 0.5)` target `(0.6, 0.0, -0.5)`" — and verify it visualizes the alcove before running the test.

### M5 — Plan doesn't address the existing "atlas debug viewer" mode

§7 out-of-scope mentions "atlas debug viewer α-respect" but Phase 2.5b might worsen the disconnect (if soft α adds new α values 0.0001..1.0, the existing viewer's "raw RGB" display becomes even more misleading). At minimum the plan should LABEL the existing viewer as "raw atlas including bake-time content" if 2.5b doesn't fix the viewer itself.

---

## LOW severity

### L1 — §1 framing claims Phase 2.5 is "the chance to fix [the bake leak] before it ossifies" but provides no evidence ossification is imminent

This is rhetorical. Phase 2 has been in for one commit; no other code depends on the bake leak yet. "Ossification" implies systemic — premature framing.

### L2 — Recommendation in §9 contradicts the plan's own structure

§9 recommends "2.5a + 2.5c is a clear win regardless." But §5's commit shape lists 2.5b's encoding/normalization changes (commit #2) as a prerequisite for 2.5b's bake fix (commit #3). Can 2.5a + 2.5c land WITHOUT commit #2 (the encoding change)? If commit #2 is part of 2.5b, then "2.5a + 2.5c" doesn't include the encoding work that soft-α needs. The recommendation is internally inconsistent.

### L3 — Plan filename says "phase2.5" but conventional naming might prefer "phase2_5"

Minor convention issue. Existing docs use `phase1.5_and_phase2_plan.md` (with `.5`), so phase2.5 is fine. No change needed but worth flagging that some tools may not handle dot-suffixes consistently in filenames.

### L4 — §3.5 decision-gate row "Cost regresses > 5% over Phase 2 → optimize before shipping" lacks a concrete optimization plan

If the decision gate triggers, the plan should propose what to optimize (e.g., "hoist `toUpper` calculation per probe rather than per bin" or "early-exit on `wcos == 0`"). Without specifics, the gate is "do something" handwave.

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | §3.1 bake-fix formula is wrong: coordinate-system mixed up; offset test doesn't address wall topology; doesn't account for what upper cascade actually contains |
| H2 | HIGH | §2.5a.1 proposes a broken bake-leak metric, then acknowledges in §6 it's broken — should propose the correct metric in §2.5a.1 |
| H3 | HIGH | §2.5a.3 sentinel-α encoding silently breaks `sampleProbeDir` and bake's `sampleUpperDir` α propagation; "bit-exact" claim for commit #2 is wrong |
| H4 | HIGH | §3.4 verification has no clear fail-and-abort criteria for partial-pass cases (RMSE between 0.05 and Phase 2's 0.064) |
| M1 | MEDIUM | Per-commit time estimates missing in §5 commit-shape table |
| M2 | MEDIUM | Sentinel-α encoding must update bake shader's α propagation (not just render shader); plan doesn't acknowledge |
| M3 | MEDIUM | `reduction_3d` audit doesn't propose what fix would look like if a bug is found |
| M4 | MEDIUM | Cornell-orig-alcove camera viewpoint not specified — auto-fit may not show alcove |
| M5 | MEDIUM | Plan doesn't address atlas debug viewer mismatch with new α semantics |
| L1 | LOW | "Ossification" rhetorical framing |
| L2 | LOW | §9 recommendation contradicts §5 commit shape |
| L3 | LOW | Filename convention note (no change needed) |
| L4 | LOW | §3.5 cost-regress gate lacks concrete optimization plan |

---

## Top 4 actions for plan revision

1. **Fix H1** — Replace §3.1 hand-waved bake-fix formula with either (a) a derivation from a real reference paper that handles non-co-located probes, or (b) explicit "bake fix is research-level, not attempted in 2.5b; ship soft α + v4 only." Option (b) is honest given the formula isn't actually correct.
2. **Fix H3 + M2** — Either the sentinel-α encoding updates the bake shader's α propagation (acknowledging the bake-side α-propagation isn't bit-exact), or pick Option B (reserved range) which doesn't have negative-value propagation issues.
3. **Fix H2** — Replace §2.5a.1's broken metric with the corrected one upfront. Don't make readers chase contradiction notes.
4. **Fix H4** — Add tiered pass/fail criteria to §3.4 verification with concrete RMSE thresholds and bake-leak-metric thresholds for each tier.
