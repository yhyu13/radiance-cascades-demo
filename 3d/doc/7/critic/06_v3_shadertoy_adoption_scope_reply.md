# Reply: critique of v3_shadertoy_adoption_scope.md

**Author:** Claude
**Date:** 2026-05-26
**Target review:** [06_v3_shadertoy_adoption_scope_review.md](06_v3_shadertoy_adoption_scope_review.md)
**Scope doc:** [../v3_shadertoy_adoption_scope.md](../v3_shadertoy_adoption_scope.md)

---

## Summary

All 12 issues accepted. Triage below records the per-issue verdict and what changed in the scope doc. Three issues (I1, I2, I11) escalate to **pre-M0 work** — the scope cannot enter M0 until they're addressed because they bear on whether M0/M1 verdicts will be reliable. The remaining nine are inline scope edits.

Net result: M0 grows a Stage 0 pre-work block (3 deliverables) before its 4 capture deliverables, M1 splits PARTIAL into two verdicts and moves Delta #7 audit to M0, M2 gains a M2_ITERATION verdict and a metric-semantics note, M3 gains a maintenance-cost acknowledgment, and §5 gains a DNR exemption clause for Path A deltas.

---

## Per-issue triage

### I1 — Missing cross-references — ACCEPT (HIGH)

**Verified:** `v20_shadertoy_diff_impl.md`, `v20_shadertoy_diff_diagrams.md`, `v20_postfix_cv1_impl.md`, `engine_default_validation_impl.md` are all not on disk. Grep for "Delta #5" or "surface-attached" across `doc/` returns only v3_shadertoy_adoption_scope.md and v25_z_mbrc_correction_failure_learnings.md — the seven-delta content lives nowhere else.

**Action:**
- Removed dangling refs from §8.
- Added §2.1 "Delta sources" that points each delta to its canonical ShaderToy code location (`shader_toy/CubeA.glsl` / `Image.glsl` / `Common.glsl` with line ranges) AND the current-impl mirror location (`radiance_3d.comp` / consumer shader). The diff descriptions in §2 are kept short; §2.1 is the structured pointer table that each per-delta impl doc can extend with semantic analysis.
- **Pre-M0 deliverable A: write `v20_shadertoy_diff_impl.md` skeleton** — for each of 7 deltas: (a) ShaderToy code location + paste, (b) current code location + paste, (c) one-paragraph semantic diff. ~60 min. M0 cannot start until this exists.

### I2 — Path A ceiling unquantified — ACCEPT (HIGH)

The "ceiling: unknown" framing is honest but operationally inert. Reviewer's proposed ShaderToy-side experiment (toggle #5 in the reference, measure bright%/|p95| delta) is cheap and produces a numeric prior for the A→B decision.

**Action:**
- Added §1.1 "Path A ceiling estimate (pre-M0 prework)".
- **Pre-M0 deliverable B: ShaderToy #5-ablation experiment.** Run reference with `cos(θ)=1` hardcoded in bake vs reference unchanged. Measure bright%/|p95|/ratio delta on a cornell analog inside the ShaderToy. Report as a single numeric estimate of #5's contribution. ~60 min.
- This estimate is used at the M1 cumulative gate: if M1 returns M1_PARTIAL, the §1.1 numeric prior informs whether the gap is "closable by #5 alone" (commit to Path B) or "larger than #5 can account for" (Path B is necessary but not sufficient — re-scope).

### I3 — Metric bands semantic shift under Path B — ACCEPT (MEDIUM)

Path B's consumer reads pre-integrated irradiance from a cubemap; Path A's consumer integrates raw radiance. The numerator of "ratio" is mathematically the same quantity (irradiance estimate), but the distribution of per-pixel errors has a different shape — Path A's errors are dominated by under-sampled bins, Path B's by interpolation artifacts.

**Action:**
- Added §3 M2 prefix note: "Path B's irradiance estimator is bake-time; M2 metrics are computed against the same PT reference as M0, so 'ratio' remains comparable in expectation. However, the per-pixel error distribution shifts shape — bright% and |p95| may not be directly comparable to M0 numbers; treat M2 metrics as 'better/worse than M0' qualitatively, not as drop-in numeric replacements."
- M2_CLOSES_GAP bands stay numerically identical (|p95| ≤ 0.50, bright% ≤ 5%) — they are absolute quality targets against PT reference, which doesn't care about consumer topology. The distribution-shape caveat affects interpretation, not the bar.

### I4 — M1_PARTIAL ambiguity — ACCEPT (MEDIUM)

Splitting is clean and informative. Reviewer's framing maps directly onto the Path B trigger: PARTIAL_GEOMETRY → Path B likely closes gap; PARTIAL_MAGNITUDE → Path B necessary but uncertain ceiling.

**Action:**
- §3 M1 cumulative gate table now lists M1_CLOSES_GAP, M1_PARTIAL_GEOMETRY, M1_PARTIAL_MAGNITUDE, M1_DEAD with differentiated Path B expectations and (for PARTIAL_MAGNITUDE) a check against §1.1's #5 estimate.

### I5 — Delta #7 audit timing — ACCEPT (LOW)

The -0.5 offset is plumbing, not a mechanism. If it's already consistently applied, gating a full A/B cycle on it is theater.

**Action:**
- Removed Delta #7 from M1 ordering.
- **Pre-M0 deliverable C: -0.5 offset audit.** Grep all probe-sampling sites in `radiance_3d.comp`, consumer shaders, hybrid correction shaders. Tabulate which use -0.5, which don't. If uniform → mark "already conformant" in baseline_lock.json. If non-uniform → reopen as a separate M1 delta with a specific patch list. ~30 min.

### I6 — Delta #6 ordering rationale — ACCEPT (LOW)

The "smallest leverage suspect" label has no evidence. Adding a conditional re-ordering note is cheap.

**Action:**
- §3 M1 work-order list now flags #6 as "low-confidence ordering; if #3 produces STRONG, #6 stays last; if #3 produces MARGINAL/DEAD, re-order #6 next."

### I7 — M0 time budget underestimated — ACCEPT (MEDIUM)

Reviewer's breakdown (Sponza harness fork, Sponza PT convergence verification, harness extension) is accurate. With the new pre-M0 work (I1+I2+I5 = ~2.5h), the realistic M0 total is now ~5h.

**Action:**
- §3 M0 budget revised to "Stage 0 (pre-work) ~2.5 h + Stage 1 (captures) ~3 h = ~5 h total." Sub-stages are explicitly listed so partial progress is visible.

### I8 — No Sponza PT convergence fallback — ACCEPT (MEDIUM)

The "blocker without bypass" critique is exactly right. The v2.x program had an unbounded-stall failure mode (kept hunting hypotheses without a tripwire); not repeating it here.

**Action:**
- §3 M0 adds contingency ladder:
  1. Sponza PT at N=2048: if converged (variance across re-runs < 5% on tracked metrics), proceed.
  2. If unstable: escalate to N=4096 with a 30-min budget.
  3. If still unstable: ship provisional Sponza bands (e.g., |p95| ≤ 0.70 instead of 0.50) and mark `baseline_lock.json` entry as `provisional: true`. The strict |p95| ≤ 0.50 retirement criterion (per §7 lock) still applies to cornell; Sponza uses provisional band for M1/M2 evaluation only.
  4. M3 hybrid retirement requires Sponza moved off provisional status — i.e., if Sponza PT was provisional, we must reach N where it's stable before M3 can flip the default.

### I9 — Path B single-shot — ACCEPT (MEDIUM)

A 3–6 session rewrite that produces MARGINAL deserves at least one debug cycle before declaring the pivot failed. Reviewer's M2_ITERATION proposal is consistent with the per-delta iteration discipline Path A enjoys.

**Action:**
- §3 M2 gate table adds M2_ITERATION between M2_PARTIAL and M2_DEAD: "metrics improved over M1 but did not close the gap; one 1–2 session debug iteration permitted before re-evaluating. Iteration is constrained to: probe-placement tuning, atlas layout adjustment, merge-logic debugging within the surface-attached topology. No new architectural axes — if iteration also fails to close, escalate to M2_DEAD."

### I10 — DNR #6 conflicts with Delta #6 — ACCEPT (LOW)

Real risk. A future session reading both docs could read the DNR literally and refuse to implement #6.

**Action:**
- §5 adds: "Exemption: Path A deltas (#3, #4, #6, #7) are exempt from v2.x DNR #6 by construction — they port specific ShaderToy mechanisms into the current pipeline. They are not 'new consume-side fixes invented within v2.0-postfix.' Each delta's impl doc must cite the ShaderToy source location it mirrors; the citation discharges the DNR check."

### I11 — Delta #3 α=0 semantics — ACCEPT (HIGH)

Sharpest technical point in the review. The volumetric α=0 / surface-attached α=0 conflation is exactly the kind of port error that produces a DEAD verdict on a delta that should have been STRONG.

**Action:**
- §2 Delta #3 row description expanded from one sentence to a paragraph that explicitly distinguishes the two α=0 conditions.
- **Pre-M0 deliverable C+:** the -0.5 audit (I5) is widened to also audit "what does `upperDir.rgb` contain when α=0 in the current bake?" — if it's already zero (no merge contribution from missed rays), Delta #3 is a no-op and can be skipped from M1. If it's nonzero (carries something — debug-default, prior frame, sky term), Delta #3's port semantics must be defined before the impl doc is written.
- The Delta #3 impl doc (when M1 reaches it) must lead with an α=0 semantics section before any code change.

### I12 — Hybrid permanent retention cost — ACCEPT (LOW)

Reviewer is right that "kept as safety net" sounds free but isn't.

**Action:**
- §3 M3 failure path appended: "Cost of permanent hybrid: every future pipeline change (Path A deltas, Path B rewrite, hybrid bug fixes) must validate against both paths. Debugging requires dual-path bisection on regressions. This cost is accepted under the strict retirement criterion and is not grounds for relaxing the criterion mid-program."

---

## Scope changes summary

| Section | Change |
|---------|--------|
| §1 | (unchanged structurally; existing §1 lock notice remains) |
| §1.1 (NEW) | Path A ceiling estimate via pre-M0 ShaderToy #5-ablation experiment (I2) |
| §2 | Delta #3 row expanded with α=0 semantic clarification (I11); Delta #7 moved to M0 audit (I5) |
| §2.1 (NEW) | Per-delta source-location table (I1) |
| §3 M0 | Stage 0 pre-work block added (I1 doc skeleton + I2 ablation + I5 -0.5 audit + I11 α=0 audit); time budget revised to ~5h (I7); Sponza PT contingency ladder added (I8) |
| §3 M1 | M1_PARTIAL split into _GEOMETRY and _MAGNITUDE (I4); Delta #7 removed from work order (I5); Delta #6 ordering flagged conditional (I6) |
| §3 M2 | Metric semantic-shift prefix note (I3); M2_ITERATION verdict added (I9) |
| §3 M3 | Hybrid maintenance cost acknowledgment (I12) |
| §5 | Path A DNR #6 exemption clause (I10) |
| §8 | Dangling refs removed (I1) |

---

## What this does NOT address

- **No pushback on any issue.** Every critique is substantive; none warrant rejection. If anything, I'd argue I11 deserves HIGH+ rating — α=0 semantic conflation is the kind of error that produces a DEAD M1 verdict and triggers a Path B commit on false information.
- **No new architectural axes.** All accepted changes are documentation precision improvements or pre-work to make existing milestones more reliable. The Path A → conditional B pivot direction is unchanged.
- **The Sponza scope decision is unchanged.** Sponza remains first-class from M0 per the 2026-05-26 lock; I8's provisional-band contingency is a stall-avoidance mechanism, not a relaxation of Sponza's status.

---

## Cross-references

- Original critique: [06_v3_shadertoy_adoption_scope_review.md](06_v3_shadertoy_adoption_scope_review.md)
- Patched scope: [../v3_shadertoy_adoption_scope.md](../v3_shadertoy_adoption_scope.md)
- Failure-learnings (DNR carryover source): [../v25_z_mbrc_correction_failure_learnings.md](../v25_z_mbrc_correction_failure_learnings.md)
