# Reply: critique 08 of v3_m0_stage0_closeout_{plan,impl}.md

**Author:** Claude
**Date:** 2026-05-27.
**Target review:** [08_v3_m0_stage0_closeout_review.md](08_v3_m0_stage0_closeout_review.md).
**Target docs:** [doc/7/v3_m0_stage0_closeout_plan.md](../v3_m0_stage0_closeout_plan.md), [doc/7/v3_m0_stage0_closeout_impl.md](../v3_m0_stage0_closeout_impl.md).

This reply triages each of the 12 issues and lists which patches are applied in this session, which are deferred, and which are rejected.

---

## Triage summary

| Issue | Severity | Verdict | Patch applied |
|-------|----------|---------|---------------|
| I1. "Mechanical" framing understates P3/P8 | MEDIUM | **ACCEPTED** | Plan: split patches into mechanical vs judgment categories |
| I2. SI1 is plan-time, not impl-time | LOW | **ACCEPTED** | Impl: reclassify SI1 to "Plan-time catches confirmed" |
| I3. Pre-patch audit gap (SI2/SI3 root cause) | MEDIUM | **ACCEPTED** | Plan: add pre-patch stale-language audit as Step 0 |
| I4. Cerebrum file existence not pre-checked | LOW | **ACCEPTED** | Plan: add pre-impl file-existence check |
| I5. 10-patch threshold exceeded silently | MEDIUM | **ACCEPTED** | Impl: add threshold-breach decision note |
| I6. SI5 is method preference, not self-critique | LOW | **ACCEPTED** | Impl: move SI5 to Implementation notes |
| I7. End-to-end re-read not explicitly confirmed | LOW | **ACCEPTED** | Impl: add explicit re-read confirmation |
| I8. P8 mechanisms not sourced/tagged | MEDIUM | **ACCEPTED** | Scope doc P8 + both closeout docs: tag mechanisms as HYPOTHESIS with sourcing |
| I9. Self-critique criteria undefined for Step 5 | MEDIUM | **ACCEPTED** | Plan: add structured 5-item checklist to Step 5 |
| I10. P3 strategic significance buried | HIGH | **ACCEPTED** | Impl: elevate to dedicated Strategic Changes section before patches table |
| I11. Housekeeping file-count gap | LOW | **ACCEPTED** | Impl: add auto-tracked clarification line |
| I12. Cerebrum DNR verification not specified | LOW | **PARTIAL-ACCEPTED** | Plan: add cross-check step (scoped to user auto-memory sources, not v25 doc — see I12 detail) |

**12 of 12 accepted (1 partial).** No issues rejected. No issues deferred.

---

## Per-issue detail

### I1 — "Mechanical" framing understates P3/P8 (MEDIUM, ACCEPTED)

**Reviewer's point:** The plan calls closeout "mechanical bookkeeping" but P3 (collapse conditional based on resolved finding) and P8 (synthesize three Path B mechanisms) require judgment, not copy-paste.

**Reply:** Correct. P3 and P8 are qualitatively different from P1/P2/P5/P6/P7. The reviewer's split — "mechanical patches" vs "judgment patches" — is exactly the right axis. Budget impact is real: a future engineer reading "closeout is mechanical" may underbudget a similar-shaped task that includes judgment patches.

**Patch applied:** Added a "Patch categories" subsection to the plan's Scope section distinguishing mechanical (P1, P2, P5, P6, P7) from judgment (P3, P4, P8). Budget table updated: judgment patches assumed 2× mechanical-patch rate.

### I2 — SI1 is plan-time, not impl-time (LOW, ACCEPTED)

**Reviewer's point:** SI1 documents SC3's plan-time catch; it's not an impl-time discovery. Including it inflates the impl self-critique's apparent discovery rate.

**Reply:** Fair. The impl self-critique should be exclusively impl-time discoveries; plan-time catches belong in the plan's self-critique section.

**Patch applied:** Reclassified SI1 in impl doc as "Plan-time catches confirmed in impl" — a different category from genuine impl-time findings SI2/SI3/SI4. Renumbered remaining genuine findings.

### I3 — Pre-patch audit gap (MEDIUM, ACCEPTED)

**Reviewer's point:** SI2 (stale #5 attribution) and SI3 (stale "to be produced" wording) were in unpatched sections. The plan's R2 grep was a *post*-patch verification, not a *pre*-patch audit. The plan assumed unpatched sections were correct — wrong when the scope doc predates the Stage 0 findings.

**Reply:** Correct root cause analysis. The 8 commissioned patches were targeted at sections where the spec knew changes were needed; the assumption that unpatched sections were correct is what allowed SI2/SI3 to escape into impl time. A pre-patch grep audit closes this gap.

**Patch applied:** Added "Step 0 — pre-patch stale-language audit" to the plan with explicit grep patterns ("to be produced", "in progress", "skeleton", "pre-M0", "never written", "if §1.1 estimate", "R-on / R-off") and a discovery-handling rule (add findings to patch list before starting Step 1).

### I4 — Cerebrum file existence not pre-checked (LOW, ACCEPTED)

**Reviewer's point:** Plan assumed "append to cerebrum.md"; file didn't exist. Could have been worse if append had assumed specific section headers.

**Reply:** Real risk. The cost of a 2-minute pre-impl file-existence check is tiny.

**Patch applied:** Added file-existence verification as a sub-step of new Step 0 (pre-patch audit). Verifies all target files (scope doc, anatomy, memory, cerebrum) exist and are writable; if missing, plan specifies create-fresh fallback with required structure.

### I5 — 10-patch threshold exceeded silently (MEDIUM, ACCEPTED)

**Reviewer's point:** Plan said stop at ~10 patches; impl applied 11 without explicit acknowledgment. The 3 sanity fixes were minor, so continuing was correct — but the decision needs to be documented to keep the guard meaningful.

**Reply:** Accepted in full. The reviewer's recommended wording is good; adopting close to verbatim.

**Patch applied:** Added "Threshold-breach decision note" to impl doc explaining: 11 ≠ 10, the 3 sanity fixes are traceability corrections not scope expansion, and what would have triggered a stop (new conditional language or new verdict categories).

### I6 — SI5 is method preference, not self-critique (LOW, ACCEPTED)

**Reviewer's point:** SI5 describes choosing `cat >>` over Edit tool — that's a method observation, not a quality finding.

**Reply:** Agreed. SI5 belongs in Implementation notes, not self-critique.

**Patch applied:** Moved SI5 to a new "Implementation notes" subsection under Housekeeping in impl doc. Self-critique section now contains only quality-affecting findings.

### I7 — End-to-end re-read not explicitly confirmed (LOW, ACCEPTED)

**Reviewer's point:** Plan's acceptance #1 requires a sequential re-read; impl's verification section lists mechanical checks only. A one-line confirmation closes the gap.

**Reply:** Adopting the reviewer's recommended one-line wording.

**Patch applied:** Added explicit re-read confirmation line to impl verification: "End-to-end sequential re-read of §1.1 → §2 → §3 M0/M1/M2/M3 → §4–§8 completed 2026-05-26; no readability issues found."

### I8 — P8 mechanisms not sourced/tagged (MEDIUM, ACCEPTED)

**Reviewer's point:** P8 added three Path B mechanisms (hemisphere-only sampling, per-corner gating under surface-attached, surface-aware probe placement) without explicit sourcing or HYPOTHESIS tag. They originated as hypotheses in critic/07 I5 partial-acceptance; the scope doc should preserve that "not proven" status.

**Reply:** Accepted in full. This is the same class of issue as v2.x's "claim treated as fact" — important to preserve epistemic status when content moves between docs.

**Patches applied:**
1. **Scope doc** §3 M2 trigger: rewrote the three-mechanism list to tag each as "HYPOTHESIS (not proven; magnitude estimate deferred to M2 Stage 0)" with explicit source citation to [critic/07_v20_shadertoy_diff_impl_reply.md §I5](07_v20_shadertoy_diff_impl_reply.md).
2. **Closeout impl** patches table P8 row: updated to note the HYPOTHESIS tagging.
3. **v20 Delta #5** "Path B mechanism candidates (deferred)" subsection: cross-checked, already tagged as "unknown magnitude, requires M2 prototyping." No change needed.

### I9 — Self-critique criteria undefined for Step 5 (MEDIUM, ACCEPTED)

**Reviewer's point:** Step 5's "self-critique pass" is open-ended; SI1–SI5 were found organically. A structured checklist produces more consistent results across closeouts.

**Reply:** Accepted. The reviewer's 5-item checklist is exactly right — each item maps to a real failure mode we've seen (SI1 = item 1, SI2/SI3 = item 2, SI3 = item 3, SI4 = item 4, P3/P8 = item 5).

**Patch applied:** Added the reviewer's 5-item checklist verbatim to Step 5 of the plan.

### I10 — P3 strategic significance buried (HIGH, ACCEPTED)

**Reviewer's point:** P3 fundamentally restructures the pivot's decision tree (M1_PARTIAL_MAGNITUDE no longer auto-triggers M2). It's listed as one row in the patches table, sandwiched between a budget tweak and a ceiling-section update. Strategic significance is invisible at a glance.

**Reply:** Highest-severity issue and accepted in full. P3 is THE strategic output of Stage 0 — Deliverable B's < 3% verdict only matters because P3 makes it decision-tree-load-bearing. Burying it as table row 3 of 11 understates the closeout's actual significance.

**Patch applied:** Added a "Strategic changes" section to the top of the impl doc, before the patches table. P3 gets a dedicated callout: it's the only patch that changes the decision tree; all other patches update numbers/wording/links without altering decision logic. Patches table now references the Strategic Changes section for P3.

### I11 — Housekeeping file-count gap (LOW, ACCEPTED)

**Reviewer's point:** Plan Step 2 mentions "9 files across Stage 0" for anatomy tracking; impl housekeeping lists 4 created + 4 edited (closeout-session only). The Stage 0 files are auto-tracked by anatomy rescan but not noted in impl, creating a 9-vs-8 gap a reader can't resolve.

**Reply:** Easy fix.

**Patch applied:** Added one-line clarification to impl housekeeping: "Stage 0 deliverable files (the original 7) were auto-tracked by anatomy.md rescan at 11:35:04Z; not duplicated in this section."

### I12 — Cerebrum DNR verification (LOW, PARTIAL-ACCEPTED)

**Reviewer's point:** Plan didn't specify verifying cerebrum DNR content against canonical sources. Carried-forward DNRs ("bake-time merging", "asymmetric filters") came from user auto-memory; recommend cross-check against `v25_z_mbrc_correction_failure_learnings.md §5`.

**Reply:** Concern is real but mischaracterized. The two carried-forward DNRs in `.wolf/cerebrum.md` are **workflow/principle DNRs** (when to use which mechanism, how to design filters) sourced from user auto-memory feedback entries, not from the v25 failure-learnings doc. The v25 doc's §5-equivalent (it's actually in [v3_shadertoy_adoption_scope.md §5](../v3_shadertoy_adoption_scope.md)) holds the **hypothesis-tree DNRs** (no symptom clamps, no resolution bumps, etc.) — a different category. The right cross-check is cerebrum DNRs ↔ user auto-memory sources (already linked via memory file pointers), not cerebrum ↔ v25 doc.

That said, the underlying point — "the closeout plan should specify how to verify cerebrum content against authoritative sources" — is sound. Accepting the spirit of the recommendation while correcting the target.

**Patch applied:** Added cerebrum-content verification step to plan Step 4: verify each entry points to its authoritative source (user auto-memory file for workflow DNRs, scope doc §5 for hypothesis-tree DNRs), and confirm wording is consistent with source. Did NOT add a cross-check against v25 failure-learnings doc because it's not the right source for the carried-forward entries.

---

## Patches summary (applied this session)

| # | Target | Change | Issue |
|---|--------|--------|-------|
| 1 | Plan Scope | Add "Patch categories" subsection (mechanical vs judgment); budget table reflects 2× judgment rate | I1 |
| 2 | Plan Step 0 (NEW) | Pre-patch stale-language grep + file-existence check | I3, I4 |
| 3 | Plan Step 4 | Add cerebrum-content verification sub-step (sourced to user auto-memory) | I12 |
| 4 | Plan Step 5 | Add 5-item self-critique checklist | I9 |
| 5 | Impl (new section) | "Strategic changes" callout before patches table, P3 highlighted | I10 |
| 6 | Impl self-critique | Reclassify SI1 as plan-time confirmation; renumber genuine findings | I2 |
| 7 | Impl self-critique | Move SI5 to Implementation notes | I6 |
| 8 | Impl verification | Add threshold-breach decision note | I5 |
| 9 | Impl verification | Add explicit end-to-end re-read confirmation | I7 |
| 10 | Impl patches table | P8 row updated to note HYPOTHESIS tagging | I8 |
| 11 | Impl housekeeping | Add auto-tracked Stage 0 files clarification | I11 |
| 12 | Scope doc §3 M2 trigger | P8 mechanism list rewritten with HYPOTHESIS tags + source citation | I8 |

**12 patches applied (1 per accepted issue, with I8 producing 2 patches across two docs).**

## What this reply does NOT do

- Re-run Stage 0 deliverables — none of these issues require redoing C, C+, A, or B.
- Re-open the M1 work order — P3/P4/P8's strategic content is unchanged, only their presentation.
- Update the v20 diff doc — I8's v20 cross-check confirmed Delta #5's Path B mechanism subsection already carries the HYPOTHESIS framing; no patch needed there.
- Write the M2 Path B mechanism scoping doc — still M2 prep work, not closeout work.

## Scope-doc impact

P8's mechanism list edit (I8 patch #12) is the only scope-doc change this round. All other patches land in the closeout plan + impl docs. No P9 needed; the existing P8 is being refined, not replaced.
