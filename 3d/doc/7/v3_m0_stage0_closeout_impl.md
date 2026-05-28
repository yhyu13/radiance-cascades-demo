# M0 Stage 0 closeout — Impl summary

**Date:** 2026-05-26.
**Predecessor plan:** [v3_m0_stage0_closeout_plan.md](v3_m0_stage0_closeout_plan.md).
**Scope executed:** 8 scope-doc patches (P1–P8) + OpenWolf bookkeeping (anatomy.md, memory.md, cerebrum.md, user auto-memory).

This doc closes Stage 0 closeout. Next phase: M0 Stage 1 (baseline captures).

---

## Strategic changes (added per critique 08 I10)

**Only one patch in this closeout alters the project's decision tree: P3.** All other patches update numbers, wording, links, or table cells without changing what happens next.

### P3 — M1_PARTIAL_MAGNITUDE no longer auto-triggers M2

**Before Stage 0:** the scope doc's M1 cumulative gate said roughly "if M1 returns PARTIAL_MAGNITUDE and §1.1's Delta #5 estimate shows #5 ≫ remaining gap, switch to Path B (M2)." The estimate was unresolved.

**After Stage 0 Deliverable B:** Delta #5 algebraic Jensen bound returned < 3% irradiance leverage — definitively too small to close a multi-pp gap. The conditional collapses to "Path B has no magnitude rescue." M1_PARTIAL_MAGNITUDE now routes back to #3/#4 hypothesis refinement, NOT to M2 Stage 0.

**Why this matters:** PARTIAL_MAGNITUDE was the verdict band most likely to push the pivot toward Path B (deltas #3, #4 already had path-agnostic mechanisms identified, and PARTIAL_GEOMETRY explicitly triggers M2 by topology rules). Locking PARTIAL_MAGNITUDE out of the M2 auto-trigger means the v3 pivot now requires a **non-magnitude** failure mode (topology, geometry, or DEAD) before Path B is on the table.

**Implication for M1 work-order owners:** if the M1 measurements return PARTIAL_MAGNITUDE, **stop** before reaching for Path B. Re-open delta #3 (per-corner visibility-weighted bilinear merge) and #4 (formulation-comparative cone math) hypothesis space first.

### All other patches are presentation-layer or traceability

| Patch | Touches | Decision-tree impact |
|-------|---------|---------------------|
| P1 | M1 work order | None — same deltas, updated dispositions |
| P2 | Budget header | None — time estimate refresh |
| P4 + cascade | §1.1 + §3 M0 Deliverable B | None — replaces "to be produced" prose with shipped-status links |
| P5, P6 | Deltas table cells | None — verdict tags + cross-refs |
| P7 | §2.1 footer | None — drop "skeleton" wording |
| P8 | §3 M2 trigger | **Prerequisite-only** — adds a precondition doc M2 must produce *before* Stage 0; does not change M2 trigger logic itself. Three candidate mechanisms are tagged HYPOTHESIS (not decisions). |
| SI2, SI3 sanity | M1_PARTIAL_GEOMETRY row, §2.1/§8 cross-refs | None — stale-language cleanup |

**Bottom line: P3 is the single strategic output of this closeout.** A reader scanning the patches table below should read P3 with that weight.

---

## Patches applied

| Patch | Target | Change | Status |
|-------|--------|--------|--------|
| P1 | §3 M1 work order | Replaced "Deltas #3, #4, #6" intro + per-delta work list with post-Stage-0 dispositions: #7 removed (CONFORMANT); #3 redefined (Case B); #6 bundled with #3; #4 formulation-comparative. Cross-link to v20 summary table. | ✓ |
| P2 | §3 M1 budget header | Revised "(~2-3 sessions)" → "(~6-7 h)" to accommodate #3's 1-2 session expansion. | ✓ |
| P3 | §3 M1 cumulative gate, M1_PARTIAL_MAGNITUDE row | **Rewrote** (not "append note") to collapse the §1.1-conditional language. Now reads: "Path B has no rescue per Stage 0 Deliverable B — return to #3/#4 refinement; do NOT auto-proceed to M2." | ✓ |
| P4 | §1.1 Path A ceiling | Replaced pre-M0 prework prose with the locked verdict (< 3% magnitude leverage), method tag (algebraic Jensen), and link to [delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md). | ✓ |
| P4-cascade | §3 M0 Stage 0 Deliverable B description | Replaced "Run ShaderToy reference twice…" method prose with "SHIPPED" + algebraic-bound result. Also marked Deliverables A/C/C+ as SHIPPED with their respective links/verdicts. | ✓ |
| P5 | §2 deltas table, Delta #3 row | Replaced "Bake's smoothstep merge feeds dead `.rgb`…" with "Per-corner visibility-weighted bilinear merge" + Case B citation + 8-corner gated trilinear cross-reference. | ✓ |
| P6 | §2 deltas table, Delta #7 row | Status column → "CONFORMANT (no work)" with [delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md) link. | ✓ |
| P7 | §2.1 canonical pointer table footer | Replaced "Pre-M0 deliverable A: write skeleton…" with "Canonical source-of-truth diff doc: [v20…] (shipped)…". | ✓ |
| P8 | §3 M2 trigger | Added Path B mechanism scoping precondition (`doc/7/v3_m2_pathb_mechanism_scope.md` TBD) before M2 Stage 0 can begin; listed three candidate mechanisms (hemisphere-only sampling, per-corner gating under surface-attached, surface-aware probe placement) **tagged as HYPOTHESIS (not proven; magnitude estimates deferred to M2 Stage 0)** with explicit source citation to [critic/07_v20_shadertoy_diff_impl_reply.md §I5](critic/07_v20_shadertoy_diff_impl_reply.md). Tagging added 2026-05-27 per critique 08 I8. | ✓ |
| Sanity | §3 M1_PARTIAL_GEOMETRY row | **Self-critique discovered inconsistency:** row still claimed "#5 (surface-aware cosine) is the blocker," contradicted by Deliverable B. Rewrote to attribute Sponza miss to topology/geometry mechanisms generally, not #5 specifically. | ✓ |
| Sanity | §2.1 intro + §8 cross-refs | **Verification grep found stale wording:** "The original v20…was never written" + "To be produced (M0 Stage 0)" both contradicted by current state. Replaced with shipped-references. | ✓ |

**Net: 8 commissioned patches + 3 self-critique-discovered fixes applied.**

---

## OpenWolf bookkeeping

| Store | Update |
|-------|--------|
| `.wolf/anatomy.md` | Auto-rescan picked up the new Stage 0 files (last scan 11:35:04Z); manually removed stale `v20_shadertoy_diff_diagrams.md` entry (file was never written; cited in `v3_shadertoy_adoption_scope.md §8` as "previously cited but never written — removed"). |
| `.wolf/memory.md` | Appended 2 session lines covering scope-doc patches + anatomy cleanup. (Per-edit lines were already auto-appended by OpenWolf hooks during patch application.) |
| `.wolf/cerebrum.md` | **Created** (was missing). Populated with: (a) Key Learning: "literal-port framing misleading for cross-topology adoption" (F3); (b) Decision Log: "M1_PARTIAL_MAGNITUDE has no Path B rescue" (F2); (c) Do-Not-Repeat: direction-sign + derivative-sign sanity checks (S2/S3). Also carried prior session DNRs forward (bake-time merging, asymmetric filters). |
| User auto-memory `feedback_analytical_doc_qa.md` | **Created.** Cross-project lesson: self-critique pass on any analytical doc MUST include direction-sign + derivative-sign-on-interval checks. Indexed in `MEMORY.md`. |

---

## Self-critique pass on closeout impl

### Plan-time catches confirmed in impl (reclassified per critique 08 I2)

Reviewer flagged that the original SI1 ("P3 was wrong-scope") documented a plan-time finding (SC3) rather than an impl-time discovery — including it in the impl self-critique inflated the impl's apparent discovery rate. Reclassified to its own category:

- **SC3 confirmation (was SI1) — P3 landed correctly as rewrite.** Plan caught the "append" misclassification before impl started. The rewrite specified in the plan applied cleanly at impl time with no rework. No new finding here; this is plan QA paying off.

### Genuine impl-time discoveries (renumbered)

Issues found during the in-progress impl and corrections applied (only items that the plan did NOT anticipate):

#### SI1 (was SI2) — M1_PARTIAL_GEOMETRY row contained stale #5 attribution

The M1 cumulative gate's PARTIAL_GEOMETRY row attributed the Sponza miss to "#5 (surface-aware cosine) is the blocker for complex occlusion." Stage 0 Deliverable B bounds #5 at < 3% irradiance leverage — it cannot be the blocker for a multi-pp Sponza gap. **Discovered during impl-time verification re-read** (SI/sanity check pass); rewrote to attribute the residual to topology/geometry mechanisms generally with cross-link to the P8 mechanism scoping precondition. This is a sanity-fix on top of the commissioned 8 patches — added as a row to the patches table for traceability.

#### SI2 (was SI3) — §2.1 intro and §8 cross-refs contained "was never written" / "to be produced" wording

Grep-verification for stale wording (per plan R2) found two occurrences: §2.1 intro ("The original v20_shadertoy_diff_impl.md was never written") and §8 cross-refs ("To be produced (M0 Stage 0): … v20_shadertoy_diff_impl.md"). Both contradicted by the now-shipped state. Rewrote both to shipped-references, expanded §8 to list all Stage 0 outputs (8 files).

#### SI3 (was SI4) — Cerebrum file didn't exist; plan assumed append

Plan said "append to .wolf/cerebrum.md." File didn't exist on disk. **Fixed at impl time** by creating the file fresh with the three required entries plus carried-forward DNRs from prior session lessons (bake-time merging, asymmetric filters). No regression; the prior DNRs were already in user auto-memory and remain authoritative there.

(Both SI1 and SI3 motivated the new plan Step 0 added in the critique-08 reply patches — pre-patch stale-language grep + file-existence check would have caught these in plan time.)

### Implementation notes (moved from self-critique per critique 08 I6)

- **Memory append method.** OpenWolf hooks auto-append per-edit lines to memory.md during file operations; the closeout's session-summary lines were appended via Bash `cat >> …` since I was adding two new lines that the hooks wouldn't generate (they describe the patch-batch as a whole, not individual edits). No issue with the result; noted as a method observation for next session — session-summary memory lines need manual append; per-edit lines are automatic. (Previously listed as SI5; reclassified as a method observation, not a quality finding.)

---

## Verification

### Threshold-breach decision note (added per critique 08 I5)

Plan stop-condition: "If patches accumulate beyond ~10 (because new follow-up edits emerge), STOP and re-scope rather than absorb silently." Impl applied **11 patches** (8 commissioned + 3 sanity fixes). Decision: continue rather than re-scope, on the grounds that the 3 sanity fixes are minor wording corrections (stale `#5` attribution, stale "was never written" / "to be produced" wording, §2.1/§8 cross-refs) — they are **traceability corrections, not scope expansion**.

What WOULD have triggered the stop condition: any sanity fix that introduced new conditional language, new verdict categories, or new decision-tree branches. None of the 3 fixes did that.

Documenting this explicitly so the guard remains meaningful for future closeouts: 11 patches is **on the boundary**, accepted because the deltas are wording-only.

### End-to-end coherence re-read (added per critique 08 I7)

End-to-end sequential re-read of §1.1 → §2 → §3 M0 → §3 M1 → §3 M2 → §3 M3 → §4 → §5 → §6 → §7 → §8 completed 2026-05-26 by author after patches landed. No readability issues found; the §1.1 verdict (< 3%) is consistently echoed at §3 M0 Deliverable B (algebraic Jensen), §3 M1_PARTIAL_MAGNITUDE row (Path B no rescue), and §3 M2 trigger (PARTIAL_MAGNITUDE excluded). Transitions between patched and unpatched sections read smoothly.

### Scope doc internal-link check (per plan R2)

- `Grep` for `\]\(\)` (empty link targets) in `v3_shadertoy_adoption_scope.md` → **no matches**. ✓
- `Grep` for stale wording (`skeleton|pre-M0 prework|R-on|R-off`) → only the historical-note line in §1.1 remains (intentional, marked "Method used (historical)"). ✓
- `Grep` for `v20_shadertoy_diff_impl` → all references resolve to the actual file path; no broken markup. ✓

### Cross-section consistency

- §1.1 verdict (< 3%) ↔ §3 M0 Deliverable B (< 3% algebraic) ↔ §3 M1_PARTIAL_MAGNITUDE row (< 3% Path B no-rescue) — all three agree. ✓
- §3 M1_PARTIAL_GEOMETRY row → §M2 trigger (PARTIAL_GEOMETRY triggers M2) — consistent. ✓
- §3 M1_PARTIAL_MAGNITUDE row → §M2 trigger (PARTIAL_MAGNITUDE excluded from M2 triggers) — consistent. ✓
- §M2 trigger references P8 precondition; P8 mechanism scoping doc TBD before M2 Stage 0 — coherent decision path. ✓

---

## Budget actual vs plan

| Step | Plan estimate | Actual |
|------|---------------|--------|
| Step 1 (8 patches + cascades) | 35 min | within budget |
| Step 2 (anatomy) | 10 min | within budget (auto-rescan did most of the work) |
| Step 3 (memory) | 5 min | within budget |
| Step 4 (cerebrum × 2 stores) | 15 min | within budget |
| Step 5 (self-critique + impl doc) | 15 min | within budget |
| **Total** | **80 min** | **within budget** |

The cascade-edit risk (R4) materialized — P4 did force a sibling edit at §3 M0 Stage 0 Deliverable B description. Plan correctly anticipated this; no overrun.

---

## Stage 0 → Stage 1 handoff (final, post-closeout)

**Locked at end of Stage 0:**
- All 8 scope-doc patches applied + 3 self-critique-discovered consistency fixes.
- Scope doc reads coherently end-to-end; no broken links; no stale wording.
- M1 work order canonically lives in [v20_shadertoy_diff_impl.md §Summary table — M1 work order](v20_shadertoy_diff_impl.md).
- M1_PARTIAL_MAGNITUDE → no auto-M2; return to #3/#4 hypothesis refinement.
- M2 trigger excludes PARTIAL_MAGNITUDE; M2 prep precondition (Path B mechanism scoping doc) added.
- `.wolf/cerebrum.md` created; user auto-memory has analytical-doc QA feedback entry.

**Ready to begin: M0 Stage 1 — Baseline captures (~3h per scope §3 Stage 1).**

Stage 1's pre-committed work list:
1. Verify/re-capture cornell cascade baseline at N=2048 (cornell/cam0/MB-ON g=1.0/hybrid-OFF/mode-17).
2. Capture cornell hybrid-ON baseline at the same config.
3. Fork `cv1_capture.ps1` to `tools/v3_baseline/sponza_capture.ps1` (Sponza measurement harness, ~60 min).
4. Run Sponza PT convergence verification per contingency ladder (N=2048 → N=4096 → provisional bands).
5. Capture Sponza cascade + Sponza hybrid-ON baselines.
6. Snapshot to `tools/v3_baseline/baseline_lock.json` (4 capture entries + 3 audit entries already produced by Stage 0).
7. Sign off Stage 1 → M0 closed → M1 Stage 0 (per-delta #3+#6 bundled impl doc) begins.

**Pre-committed M1 gates (carried forward):**
- M1 impl doc for #3+#6 bundle MUST front-load [delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md) verdict and define the 8-corner gated trilinear formula before any code lands.
- M1 impl doc for #4 MUST cite v20's formulation-comparative framing; use post-A/B disposition rules from v20 summary table.
- Every M1 delta evaluated on BOTH cornell AND sponza (Sponza-veto in force).

---

## Housekeeping (post-closeout)

Files created this closeout session:
- `doc/7/v3_m0_stage0_closeout_plan.md`
- `doc/7/v3_m0_stage0_closeout_impl.md` (this file)
- `.wolf/cerebrum.md` (project cerebrum, was missing)
- `C:/Users/XINDONG/.claude/projects/.../memory/feedback_analytical_doc_qa.md` (user auto-memory)

Files edited:
- `doc/7/v3_shadertoy_adoption_scope.md` (11 patches: P1, P2, P3, P4, P4-cascade, P5, P6, P7, P8, SI2 sanity, SI3 sanity)
- `.wolf/anatomy.md` (removed stale entry)
- `.wolf/memory.md` (appended session-summary lines)
- `C:/Users/XINDONG/.claude/projects/.../memory/MEMORY.md` (added index entry)

No code changes (closeout is doc-only).

**File-count clarification (added per critique 08 I11):** The plan's Step 2 mentions "9 files across Stage 0" for anatomy tracking; this section lists 4 created + 4 edited within the closeout session. The gap is bridged by the Stage 0 deliverable files (the original 7: `v3_m0_stage0_plan.md`, `v3_m0_stage0_impl.md`, `v20_shadertoy_diff_impl.md`, `delta3_alpha_audit.md`, `delta5_ceiling_estimate.md`, `delta7_offset_audit.md`, `critic/07_v20_shadertoy_diff_impl_reply.md`), which were auto-tracked by `.wolf/anatomy.md` rescan at 11:35:04Z and not duplicated in this section.

## Open questions deferred to Stage 1 or later

- **Path B mechanism scoping doc (`doc/7/v3_m2_pathb_mechanism_scope.md`):** stubbed as P8 precondition; only written if M1 returns PARTIAL_GEOMETRY or DEAD. Not Stage 1 work.
- **Sponza PT reference reachability** — owned by Stage 1 contingency ladder.
- **Whether #3's expanded scope (1-2 sessions per Case B redefinition) is actually 1-2 sessions** — depends on coupling between `sampleUpperDirTrilinear` and `sampleUpperDirWeighted` not yet depth-estimated. Hand off to M1 Stage 0 impl-doc time.
