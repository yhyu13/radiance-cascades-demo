# Reply: critique 07 of v20_shadertoy_diff_impl.md

**Author:** Claude
**Date:** 2026-05-26.
**Target review:** [07_v20_shadertoy_diff_impl_review.md](07_v20_shadertoy_diff_impl_review.md).
**Target doc:** [doc/7/v20_shadertoy_diff_impl.md](../v20_shadertoy_diff_impl.md).

This reply triages each of the 12 issues and lists which patches are applied to v20 in this session, which are deferred, and which are rejected.

---

## Triage summary

| Issue | Severity | Verdict | Patch applied this session |
|-------|----------|---------|-----------------------------|
| I1. Delta #3 volumetric analog missing | HIGH | **ACCEPTED** | Yes — add Volumetric analog sketch subsection |
| I2. #3/#6 bundling vs isolation trade-off | MEDIUM | **ACCEPTED** | Yes — add trade-off note to Delta #6 + summary table |
| I3. #4 baseline contamination from reorder | MEDIUM | **ACCEPTED** | Yes — add conditional interpretation note |
| I4. #4 is comparative experiment, not port | MEDIUM | **ACCEPTED** | Yes — add formulation-comparative note |
| I5. Path B leverage undersynthesized | HIGH | **PARTIAL-ACCEPTED** | Yes — add Path B mechanism pointer (full analysis = M2 doc) |
| I6. Delta #3 Cases A/C not referenced | LOW | **ACCEPTED** | Yes — add audit context note |
| I7. Topology tag lacks sequencing dimension | LOW | **ACCEPTED** | Yes — add sequencing notation to legend + table |
| I8. Delta #6 semantic diff too dense | LOW | **ACCEPTED** | Yes — split into Structural / Numerical / Port |
| I9. #6 correctness vs metric effectiveness | MEDIUM | **ACCEPTED** | Yes — add principled-justification criterion |
| I10. #4 post-A/B cleanup not addressed | LOW | **ACCEPTED** | Yes — add post-A/B disposition |
| I11. #5 same-point insight underemphasized | LOW | **ACCEPTED** | Yes — add one-line math note |
| I12. Snapshot vs work-order table inconsistency | LOW | **ACCEPTED** | Yes — synchronize bundling info |

**12 of 12 accepted (1 partial).** No issues rejected. No issues deferred beyond this session except I5's "full Path B mechanism analysis" which belongs in a future M2 doc, not in the M0 Stage 0 diff doc.

---

## Per-issue detail

### I1 — Delta #3 volumetric analog formulation missing (HIGH, ACCEPTED)

**Reviewer's point:** v20 identifies the structural gap but doesn't define the 8-corner volumetric formulation; each M1 impl doc would have to re-derive it.

**Reply:** Correct. The C+ audit identifies "per-corner gated trilinear" as the mechanism but doesn't write the 8-corner formula either. Putting it in v20 is the right place — v20 is the source-of-truth diff doc.

**Patch applied:** Added a "Volumetric analog sketch" subsection to Delta #3 with the 8-corner formula, the two return-shape options (per-corner-gated trilinear vs separate-rgb-and-weight returns), and the two code-site identifications (`sampleUpperDirTrilinear` lines 229-266 + merge call site lines 660-687).

### I2 — #3/#6 bundling vs per-delta isolation discipline (MEDIUM, ACCEPTED)

**Reviewer's point:** The 2×2 matrix creates attribution contamination; scope rule #8 (each delta gets its own gate) is violated.

**Reply:** Trade-off is real. The pragmatic case for bundling — #6 alone is likely DEAD without #3, sharing the A/B harness saves a session — is strong, but it should be stated as a trade-off not a free win. The reviewer's specific recommendation (add explicit note + drop-#6-if-alone-DEAD rule) is sound and actionable.

**Patch applied:** Added a "Bundling trade-off" note under Delta #6's port disposition with the 2×2 matrix table, the contamination acknowledgment, and the "drop #6 if alone-DEAD regardless of #3 outcome" rule. Synchronized with summary table (covers I12 too).

### I3 — #4 baseline contamination from reorder (MEDIUM, ACCEPTED)

**Reviewer's point:** Under the original {#3, #4, #6} order, #4's baseline includes only #3 (since #7 is conformant). Under {#3, #6, #4}, #4's baseline includes #3+#6, making MARGINAL ambiguous (weak lever vs gap-already-closed).

**Reply:** Real concern. The reordering rationale (share A/B harness with #3) is still sound, but #4's verdict interpretation needs the conditional note. Specifically, "MARGINAL #4 after STRONG #3+#6" should NOT auto-promote to "MB formulation doesn't matter" if the bright%/|p95| residual is already small.

**Patch applied:** Added a conditional interpretation note to the M1 work order table for Delta #4: "If #3+#6 cumulative reduced bright% to <7% AND |p95| to <0.60, a MARGINAL #4 verdict is expected and does not mean 'MB formulation is a weak lever' — the remaining gap is too small for #4 to leverage. Re-evaluate against the residual headroom, not the absolute scale."

### I4 — Delta #4 is comparative, not a definitive port (MEDIUM, ACCEPTED)

**Reviewer's point:** Current already has an MB formulation (MC); the A/B asks "which alternative is better" not "does the missing mechanism close the gap." Different verdict semantics than STRONG/MARGINAL/DEAD assume.

**Reply:** Correct distinction and well-framed. The STRONG/MARGINAL/DEAD bands were designed for "additive" deltas; #4 is "substitutive." The reviewer's wording for the disposition note is good — adopting close to verbatim.

**Patch applied:** Added a "formulation-comparative" note to Delta #4's port disposition. STRONG: replace MC default. MC-wins: remove flag, mark #4 as "verified-equivalent-or-better" not "ported." Updated summary table notes column accordingly.

### I5 — Path B leverage analysis undersynthesized (HIGH, PARTIAL-ACCEPTED)

**Reviewer's point:** v20 says "#5 is small lever; Path B's value is topology" but doesn't identify which mechanisms under Path B actually close the gap. An engineer evaluating M1_PARTIAL_MAGNITUDE needs to know what Path B brings beyond #5.

**Reply:** Half-accept. The reviewer is right that this question is unanswered, but the answer is more than v20 can carry — it's a Path B mechanism analysis that belongs in an M2 scope doc, not in the M0 Stage 0 per-delta diff. v20 is the per-delta diff; v20's responsibility ends at "what's the delta and how big is it." The strategic-question "what closes the gap under Path B" needs its own doc.

That said, leaving v20 silent on the question creates the risk the reviewer flagged (M1_PARTIAL_MAGNITUDE evaluation has no anchor). The compromise: add a "Path B mechanism candidates (deferred to M2 scope)" note to Delta #5 listing the three mechanisms the reviewer identified (hemisphere-only sampling, per-corner gating under surface-attached, surface-aware probe placement) **without** committing to magnitude estimates. Magnitude estimates require Path B prototyping work that's out of scope for Stage 0.

**Patch applied:** Added a "Path B mechanism candidates (deferred)" subsection to Delta #5 listing the three mechanisms as hypotheses, with explicit "unknown magnitude, requires M2 prototyping" tags. Cross-linked to a TODO for an M2 scope doc.

**Deferred:** The full per-mechanism magnitude estimate. This is M2 work, not Stage 0 work. v20's pointer is sufficient to unblock M1_PARTIAL_MAGNITUDE evaluation (the evaluator now knows the candidates exist, even without numbers).

### I6 — Delta #3 Cases A/C not referenced (LOW, ACCEPTED)

**Reviewer's point:** v20 only documents Case B (the actual finding) without noting Cases A or C existed and what they'd have implied.

**Reply:** Easy fix. Reading v20 standalone without the C+ audit context, a reader wouldn't know why Case B was the relevant outcome.

**Patch applied:** Added a one-paragraph "Audit context" note to Delta #3 listing the three cases and the implications of each.

### I7 — Topology tag lacks sequencing dimension (LOW, ACCEPTED)

**Reviewer's point:** ⚠ applied to both #3 and #6 obscures that #6 has a sequencing dependency on #3.

**Reply:** Good catch. The proposed "[→#3]" annotation is clean.

**Patch applied:** Extended the topology tag legend with the "[→#N]" sequencing notation. Updated Delta #6's topology tag to "⚠ [→#3]" in both the status snapshot table and the per-delta entry.

### I8 — Delta #6 semantic diff too dense (LOW, ACCEPTED)

**Reviewer's point:** ~15 lines mixing 5 distinct information types.

**Reply:** Splitting helps. The reviewer's three-bucket split (Structural / Numerical / Port considerations) maps cleanly to the existing content.

**Patch applied:** Split Delta #6's semantic-diff paragraph into three subsections (Structural diff / Numerical diff / Port considerations), mapping existing content to each. No content lost.

### I9 — Delta #6 "correctness unclear" creates evaluation problem (MEDIUM, ACCEPTED)

**Reviewer's point:** A wider cone could pass STRONG by smearing energy without being geometrically correct — same class of problem as v2.x output-side symptom clamps.

**Reply:** Important connection to DNR #4 (no merge-formula reshapes targeting bright-tail isolation). The principled-justification criterion is the right gate.

**Patch applied:** Added a "Correctness criterion (DNR #4 analog)" line to Delta #6's port disposition. STRONG requires both metric improvement AND a principled geometric justification for the chosen cone size; "we widened until metrics improved" disqualifies.

### I10 — Delta #4 post-A/B cleanup not addressed (LOW, ACCEPTED)

**Reviewer's point:** v20 recommends adding a flag for the deterministic mode but doesn't specify what happens to the flag after the A/B verdict.

**Reply:** Should be explicit; flags accumulate quickly if not retired.

**Patch applied:** Added a "Post-A/B disposition" line to Delta #4 with three branches (deterministic STRONG → MC removed; MC wins → flag removed; MARGINAL → one session investigation, default to MC if unresolved).

### I11 — Delta #5 same-point evaluation insight underemphasized (LOW, ACCEPTED)

**Reviewer's point:** The ceiling estimate's key insight ("both formulations evaluate cos(θ) at the SAME point (bin center)") is the strongest mathematical claim supporting #5's small-leverage verdict and should be front-and-center in v20's Delta #5 entry.

**Reply:** Agreed. v20's "both are O(angular_width²)-bounded discretizations" understates the result — the same-point insight makes it a uniform scale shift, not a shape change.

**Patch applied:** Added a one-line "Key insight" note to Delta #5's semantic diff with the same-point evaluation finding and the <3% uniform-scale framing.

### I12 — Status snapshot vs work-order table inconsistency (LOW, ACCEPTED)

**Reviewer's point:** The two tables present different information about the same deltas; bundling info appears in one but not the other.

**Reply:** Fix by synchronization. The bundling note belongs in both for self-containment.

**Patch applied:** Synchronized status snapshot and summary work-order table. Both now show bundling status, sequencing tags, and conditional notes consistently. (Covers I12 fully; also touched while applying I7's sequencing notation.)

---

## Patches summary (applied to v20 this session)

| # | Section | Change |
|---|---------|--------|
| 1 | Topology tag legend | Added "[→#N]" sequencing notation |
| 2 | Status snapshot table | Updated Delta #6 tag to "⚠ [→#3]"; added bundling/sequencing info |
| 3 | Delta #3 | Added Audit context note (I6); added Volumetric analog sketch subsection (I1) |
| 4 | Delta #4 | Added formulation-comparative note (I4); added Post-A/B disposition (I10) |
| 5 | Delta #5 | Added Key insight one-line math note (I11); added Path B mechanism candidates (I5 partial) |
| 6 | Delta #6 | Split semantic diff into Structural/Numerical/Port (I8); added bundling trade-off note (I2); added Correctness criterion (I9); updated topology tag (I7) |
| 7 | Summary table | Added #4 conditional interpretation note (I3); synchronized with status snapshot (I12) |

## What this reply does NOT do

- Apply the patches to v20 — done **after** this reply is written (next step).
- Re-execute Stage 0 deliverables — none of these issues require re-doing C, C+, or B.
- Write the M2 Path B scope doc — that's I5's deferred half, owned by a future M2 Stage 0.
- Update the M0 Stage 0 impl summary doc — `v3_m0_stage0_impl.md` doesn't reference v20's internals, so no propagation needed. Its scope-patch list (P1-P7) is unaffected by these v20-internal changes.

## Scope-doc impact

None of these issues require changes to `v3_shadertoy_adoption_scope.md`. The scope-doc patches P1-P7 from `v3_m0_stage0_impl.md` §"Scope-doc patches needed" remain the canonical pending edits.

The only question is whether **scope §3 should pick up I5's deferred mechanism analysis** as an explicit M2-prep item. Recommendation: yes — add an "M2 prep: Path B mechanism scoping" entry as a precondition for M2 Stage 0, so that M1_PARTIAL_MAGNITUDE evaluation has a defined next action. This becomes scope-doc patch **P8** to track alongside P1-P7.
