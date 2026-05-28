# Critique: v3_m0_stage0_closeout_plan.md + v3_m0_stage0_closeout_impl.md

**Reviewer:** Kilo (automated)
**Date:** 2026-05-27T09:54+08:00
**Targets:** `doc/7/v3_m0_stage0_closeout_plan.md` (plan), `doc/7/v3_m0_stage0_closeout_impl.md` (impl)

---

## Structural Strengths

1. **Scope boundary is explicit and correct.** The plan states "Stage 0 closeout is mechanical bookkeeping, not new analytical work" and enumerates what's in/out of scope. The impl stays within these bounds — no analytical work leaked in.

2. **Patches are pre-specified with traceable sources.** Each patch (P1–P8) has a target section, a source document where the spec was defined, and a risk rating. This makes the closeout reproducible and auditable.

3. **Self-critique caught real issues before execution.** SC3 (P3 scope wrong — "append" vs "rewrite") was the most impactful plan-time catch. SC5 (P4 cascade-edit risk) correctly anticipated the §1.1/§3 M0 twin-site problem. Both materialized during impl and were handled as planned.

4. **Impl self-critique caught 3 issues beyond the commissioned patches.** SI2 (stale #5 attribution in PARTIAL_GEOMETRY row) and SI3 (stale "was never written" / "to be produced" wording) were genuine impl-time discoveries not covered by the 8-patch spec. SI4 (cerebrum file didn't exist) was a file-state deviation from plan assumptions.

5. **Verification is concrete.** Grep patterns for broken links and stale wording, cross-section consistency checks across §1.1/§2/§3. These are executable criteria, not subjective "looks good."

6. **Budget tracking is honest.** All steps within budget; cascade-edit risk (R4) materialized as anticipated; 3 sanity fixes added but acknowledged as minor wording corrections.

7. **Stage 1 handoff is well-defined.** 7 numbered items with clear ownership and pre-committed M1 gates carried forward. The handoff is actionable without ambiguity.

---

## Issues

### I1. "Mechanical bookkeeping" framing understates P3 and P8

The plan says closeout is "mechanical bookkeeping, not new analytical work." But:
- **P3** required collapsing conditional language based on a resolved finding (Deliverable B's <3% verdict). This requires judgment: what language to replace the conditional with, how to phrase "Path B has no rescue" as a scope-level constraint. It's not a copy-paste from a deliverable doc.
- **P8** required adding new content: three candidate Path B mechanisms (hemisphere-only sampling, per-corner gating under surface-attached, surface-aware probe placement) and a precondition stub. This is analytical work — synthesizing mechanisms from Stage 0 findings.

**Risk:** If a future closeout is scoped as "mechanical," the engineer may underbudget patches that require judgment or synthesis, leading to time overruns or superficial edits that don't capture the actual strategic change.

**Recommendation:** Separate the plan's framing into two categories: "mechanical patches" (P1, P2, P5, P6, P7 — copy known verdicts/numbers into known locations) and "judgment patches" (P3, P4, P8 — collapse conditional language or add new content based on findings). Budget judgment patches at 2× the mechanical rate.

### I2. SI1 is a plan-time finding, not an impl-time discovery

SI1 says: "Fixed in plan before impl started, so P3 landed correctly as a rewrite. No impl-time rework needed." This is documenting a plan self-critique result (SC3) in the impl's self-critique section. SC3 was caught and fixed in the plan; SI1 is just confirming it landed correctly. The impl self-critique section should be for impl-time discoveries only — issues that arose during execution that the plan didn't anticipate. SI1 doesn't meet that criterion.

**Risk:** Inflating the impl self-critique with plan-time findings makes the impl doc harder to parse: which issues were genuinely unexpected (SI2, SI3, SI4) vs which were already caught (SI1)? Future readers may overestimate the impl's discovery rate.

**Recommendation:** Remove SI1 from the impl self-critique or reclassify it as "Plan-time catch confirmed in impl" (a different category from genuine impl-time discoveries SI2–SI5).

### I3. SI2 and SI3 reveal gaps in the plan's pre-patch audit scope

SI2 (stale #5 attribution in PARTIAL_GEOMETRY) and SI3 (stale "was never written" / "to be produced" wording) were in sections the plan's patches didn't target. P1–P8 specified exact target sections; the plan assumed unpatched sections were correct. R2's grep verification was designed as a *post*-patch check for broken links and stale wording, not a *pre*-patch audit of the full scope doc.

The plan's assumption that only patched sections need verification is wrong when the scope doc was written before Stage 0 findings resolved — unpatched sections may contain stale language that referred to unresolved states (e.g., §2.1 intro referencing the pre-Stage-0 state of v20, §8 referencing v20 as "to be produced").

**Risk:** Future closeouts that specify patches for specific sections may miss stale language in adjacent or unrelated sections that also referenced the pre-resolution state. The grep verification is reactive, not proactive.

**Recommendation:** Add a pre-patch audit step to the closeout plan: "Before applying patches, grep the full scope doc for patterns that reference pre-resolution states: 'to be produced', 'in progress', 'skeleton', 'pre-M0', 'never written', conditional language referencing unresolved §1.1 estimates." This turns R2 into a two-pass check: pre-patch audit of unpatched sections + post-patch verification of patched sections.

### I4. Cerebrum file existence wasn't verified before execution

SI4 discovered `.wolf/cerebrum.md` didn't exist. The plan assumed "append to cerebrum.md" in Step 4. The impl created it fresh, which is fine, but it's a deviation from plan assumptions that could have caused issues if the plan specified "append after line X" or relied on existing section headers.

**Risk:** For closeouts that assume append-only operations on existing files, a missing file forces a mode switch from "append" to "create" — which may need different formatting (section headers, existing DNR entries to carry forward, etc.). SI4's creation included carrying forward prior DNRs from user auto-memory, which required cross-store knowledge the plan didn't specify.

**Recommendation:** Add a 2-minute pre-impl file-existence check to the closeout plan: verify all target files (scope doc, anatomy.md, memory.md, cerebrum.md) exist and are writable before starting Step 1. If any file is missing, the plan should specify what to do (create fresh with expected structure, or surface to user).

### I5. Stop condition threshold (10 patches) was exceeded but not explicitly acknowledged

The plan says: "If patches accumulate beyond ~10 (because new follow-up edits emerge), STOP and re-scope rather than absorb silently." The impl applied 11 patches (8 commissioned + 3 sanity fixes). This exceeds the threshold. The impl doesn't explicitly acknowledge this threshold breach or document the decision to continue rather than re-scope.

**Risk:** The stop condition exists to prevent scope creep through patch accumulation. Bypassing it without documentation undermines the guard. In this case, the 3 sanity fixes are genuinely minor (wording changes, not structural edits), so continuing was correct — but the decision should be explicit.

**Recommendation:** Add a threshold-breach note to the impl doc: "11 patches applied (threshold: 10). 3 sanity fixes beyond commissioned patches are minor wording corrections (stale #5 attribution, stale "was never written" / "to be produced" wording, §2.1/§8 cross-refs). Decision to continue rather than re-scope: these fixes are traceability corrections, not scope expansion. If sanity fixes had introduced new conditional language or new verdict categories, the stop condition would have triggered."

### I6. SI5 is a method preference, not a self-critique finding

SI5 notes that memory append used `cat >>` (Bash) instead of Edit tool, because the session-summary lines aren't auto-generated by OpenWolf hooks. This is an implementation method observation, not a quality issue discovered through self-critique. Including it in the self-critique section inflates the section with a low-signal entry.

**Recommendation:** Move SI5 from the self-critique section to a "Implementation notes" subsection (or the housekeeping section), reserving the self-critique section for quality-affecting discoveries (SI1–SI4).

### I7. End-to-end coherence re-read not explicitly confirmed in impl

The plan's acceptance criterion #1 says: "the scope doc reads coherently end-to-end (re-read §1.1 + §2 + §3 M0/M1/M2 sequentially after patches land)." The impl's verification section lists specific checks (link validity, stale wording grep, cross-section consistency) but doesn't explicitly state "I re-read the entire scope doc sequentially and it reads coherently." The verification section reads like a checklist of mechanical checks, not a holistic readability assessment.

**Risk:** Mechanical checks can pass while the doc still reads poorly (e.g., redundant phrasing, inconsistent terminology between sections, abrupt transitions after patches). The end-to-end re-read is meant to catch these.

**Recommendation:** Add an explicit confirmation line to the impl's verification section: "End-to-end sequential re-read of §1.1→§2→§3 M0→§3 M1→§3 M2→§3 M3→§4→§5→§6→§7→§8 completed; no readability issues found." Even a one-line confirmation suffices.

### I8. P8 candidate mechanisms are not sourced

P8 added "three candidate mechanisms surfaced during Stage 0" as a precondition for M2. The impl confirms this was applied. But neither the plan nor the impl specifies where these three candidates came from — are they from the critic/07 reply (I5 partial-acceptance), from Deliverable B's analysis, or from the Stage 0 impl's F2/F3 findings? The closeout plan says "per critic/07 reply" but doesn't list the three mechanisms explicitly. The impl says "listed three candidate mechanisms surfaced during Stage 0" but doesn't identify them.

**Risk:** The three mechanisms are a new analytical input to the scope doc that wasn't in the original v3 scope. Without explicit sourcing, a future reader can't verify whether these mechanisms are grounded in evidence or speculative. The critic/07 reply's I5 partial-acceptance listed them as "three hypotheses, deferred magnitude estimates to M2" — but the scope doc's P8 patch should preserve this "hypothesis, not proven" status explicitly.

**Recommendation:** The P8 patch in the scope doc should tag the three mechanisms as "HYPOTHESIS (not proven)" with explicit sourcing: "Source: critic/07 I5 partial-acceptance; magnitude estimates deferred to M2 Stage 0 per reply triage." This prevents the mechanisms from being read as established facts when they're actually unverified hypotheses.

### I9. Plan doesn't define self-critique criteria for Step 5

Step 5 says: "create v3_m0_stage0_closeout_impl.md with patches-applied table + self-critique pass + handoff to Stage 1." The "self-critique pass" is open-ended — no defined criteria for what to check. The impl's SI1–SI5 were found organically, which is fine for an experienced engineer, but a structured checklist would produce more consistent results.

**Recommendation:** Add a self-critique checklist to Step 5:
1. Does each patch match its spec (target section, change description)?
2. Are there any stale references to pre-resolution states in unpatched sections?
3. Does the patched scope doc read coherently end-to-end?
4. Were any file-state assumptions violated (files missing that the plan assumed existed)?
5. Did any patch require judgment beyond "copy known verdict into known location"?

### I10. M1_PARTIAL_MAGNITUDE strategic change is the most significant output, but it's buried in a patch table

P3 rewrote the M1_PARTIAL_MAGNITUDE row from conditional (check §1.1) to definitive ("Path B has no rescue — return to #3/#4; do NOT auto-proceed to M2"). This is the single most important strategic change in the closeout: it fundamentally alters the pivot's decision tree. But it's listed as one row in a patches table, sandwiched between P2 (budget number change) and P4 (ceiling section update).

**Risk:** A reader scanning the impl doc might treat P3 as equivalent in significance to P2 or P6. The closeout's most important output is a decision-tree restructuring, not a number update.

**Recommendation:** Elevate P3's description to a dedicated "Strategic changes" section (or a highlighted callout) in the impl doc, separate from the mechanical patches table. State explicitly: "P3 is the only patch that changes the pivot's decision tree (M1_PARTIAL_MAGNITUDE → no M2 auto-trigger). All other patches update numbers, wording, or links without altering the decision logic."

### I11. Housekeeping section tracks closeout-session files but not Stage 0 deliverable file state

The plan's Step 2 says "9 files across Stage 0" for anatomy tracking. The impl's housekeeping section lists 4 files created + 4 files edited during the closeout session. The 9 Stage 0 files are tracked via anatomy auto-rescan, not manually listed in the impl's housekeeping section. This is correct (auto-rescan is sufficient) but the gap between "9 files in plan" and "8 files in impl housekeeping" could confuse a reader who doesn't know anatomy auto-tracked the Stage 0 files.

**Recommendation:** Add a one-line clarification to the impl's housekeeping section: "Stage 0 deliverable files (v3_m0_stage0_plan.md, v3_m0_stage0_impl.md, v20_shadertoy_diff_impl.md, delta3/5/7 audit docs, critic/07 reply) were auto-tracked by anatomy.md rescan at 11:35:04Z; not duplicated here."

### I12. Closeout plan doesn't specify verification of cerebrum/memory content correctness

The plan says "append cerebrum entries" and "append memory entries" but doesn't specify verifying the appended content against the actual findings. The impl created cerebrum.md from scratch (SI4) with carried-forward DNRs. These DNRs ("bake-time merging", "asymmetric filters") were sourced from user auto-memory, not from a project-level canonical source. If user auto-memory and project cerebrum diverge on DNR wording, future sessions may follow inconsistent guidance.

**Risk:** DNR entries carried forward from user auto-memory may differ from the original session's wording. Without verification, the cerebrum's DNR section could drift from the authoritative source (the v25 failure-learnings doc).

**Recommendation:** Add a verification step after Step 4: "Read `.wolf/cerebrum.md` DNR entries and cross-check against `doc/7/v25_z_mbrc_correction_failure_learnings.md §5` for wording consistency. If wording diverges, align cerebrum to the canonical source."

---

## Summary Table

| Issue | Severity | Document | Action Required |
|-------|----------|----------|-----------------|
| I1. "Mechanical" framing understates P3/P8 | MEDIUM | Plan | Split patches into mechanical vs judgment categories |
| I2. SI1 is plan-time, not impl-time | LOW | Impl | Reclassify or remove from impl self-critique |
| I3. Pre-patch audit gap (SI2/SI3 root cause) | MEDIUM | Plan | Add pre-patch stale-language grep step |
| I4. Cerebrum file existence not pre-checked | LOW | Plan | Add pre-impl file-existence check step |
| I5. 10-patch threshold exceeded without acknowledgment | MEDIUM | Impl | Add threshold-breach decision note |
| I6. SI5 is method preference, not self-critique | LOW | Impl | Move to implementation notes section |
| I7. End-to-end re-read not explicitly confirmed | LOW | Impl | Add explicit re-read confirmation line |
| I8. P8 candidate mechanisms not sourced/tagged | MEDIUM | Both | Tag mechanisms as HYPOTHESIS with explicit sourcing |
| I9. Self-critique criteria undefined for Step 5 | MEDIUM | Plan | Add structured checklist to Step 5 |
| I10. P3 strategic significance buried in patch table | HIGH | Impl | Elevate to dedicated strategic-changes section |
| I11. Housekeeping/Step 2 file-count gap unexplained | LOW | Impl | Add one-line auto-tracked clarification |
| I12. Cerebrum DNR verification not specified | LOW | Plan | Add cross-check step against canonical source |

---

## Overall Assessment

Both documents are well-executed closeout artifacts. The plan correctly bounded scope, pre-specified patches, and caught 5 self-critique issues (SC1–SC5) before execution. The impl stayed within scope, applied all patches, discovered 3 genuine issues beyond the commissioned spec (SI2, SI3, SI4), and verified the patched scope doc. The primary weakness is that the **most significant strategic change (P3 — M1_PARTIAL_MAGNITUDE decision-tree restructuring) is buried as one row in a mechanical patch table** rather than highlighted as a decision-level output. Secondary weaknesses: the "mechanical bookkeeping" framing understates two judgment patches (P3, P8), and the plan lacked a pre-patch stale-language audit step (which is why SI2/SI3 were discovered at impl time rather than caught in the plan). These are process improvements, not correctness issues — the closeout's outputs are correct and traceable.