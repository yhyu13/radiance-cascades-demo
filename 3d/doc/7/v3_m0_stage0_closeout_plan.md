# M0 Stage 0 closeout — Plan

**Date:** 2026-05-26.
**Predecessor:** [v3_m0_stage0_impl.md](v3_m0_stage0_impl.md) (Stage 0 deliverables A/B/C/C+ shipped); [critic/07_v20_shadertoy_diff_impl_reply.md](critic/07_v20_shadertoy_diff_impl_reply.md) (critique-07 patches applied to v20).
**Goal:** propagate the Stage 0 findings into the upstream scope doc (`v3_shadertoy_adoption_scope.md`) and into the OpenWolf bookkeeping (`anatomy.md`, `memory.md`, `cerebrum.md`), so M0 Stage 1 (baseline captures) can begin against a coherent scope.

Stage 0 closeout is **mostly mechanical bookkeeping plus a small number of judgment edits**, not new analytical work. The findings are locked; this plan only moves them into their permanent homes — but two patches (P3 and P8) and one cascade (P4) require synthesis, not copy-paste. See §"Patch categories" below.

---

## Scope (in / out)

**In scope:**
- Apply scope-doc patches P1–P7 (per [v3_m0_stage0_impl.md §Scope-doc patches needed](v3_m0_stage0_impl.md)).
- Apply scope-doc patch P8 (per [critic/07_v20_shadertoy_diff_impl_reply.md §Scope-doc impact](critic/07_v20_shadertoy_diff_impl_reply.md)) — M2 prep mechanism scoping as precondition for M2 Stage 0.
- Update `.wolf/anatomy.md` to track the 7 files created across Stage 0.
- Append per-deliverable entries to `.wolf/memory.md`.
- Append cerebrum entries: (a) direction-sign / derivative-sign self-critique lesson (per S2/S3); (b) "literal port" framing was misleading (per F3); (c) M1_PARTIAL_MAGNITUDE has no Path B rescue (per F2).

### Patch categories (added per critique 08 I1)

Patches split along a mechanical-vs-judgment axis. The budget table in §"Risk + budget" reflects this split.

| Category | Patches | Description |
|----------|---------|-------------|
| **Mechanical** | P1, P2, P5, P6, P7 | Copy a known verdict / number / link into a known location. No new content. |
| **Judgment** | P3, P4, P8 | Collapse a now-resolved conditional (P3), restructure cascading sibling sections to agree (P4), or synthesize a new precondition + candidate-mechanism list (P8). Requires reading Stage 0 findings and choosing how to phrase the scope-level consequence. |

**Implication for future closeouts:** scoping a closeout as "mechanical bookkeeping" without flagging judgment patches risks underbudgeting. Judgment patches run ~2× the per-patch rate of mechanical patches in this plan's budget.

**Out of scope (deferred to next phase):**
- M0 Stage 1 baseline captures (cornell cascade/hybrid + Sponza cascade/hybrid + Sponza PT N-ladder).
- The M2 scope doc that P8 will eventually reference (the patch only stubs the precondition; the doc itself is M2-prep work, not M0).
- Per-delta M1 impl docs (M1 work).

---

## Work breakdown

### Step 0 — Pre-patch audit (~10 min, added per critique 08 I3 + I4)

Before applying any patches, run two pre-checks against the targets. Discoveries from this audit are added to the patch list **before** starting Step 1 (this is how SI2/SI3 would have been caught in plan time rather than impl time).

**0a — Stale-language grep on the full scope doc.** The 8 commissioned patches target specific sections; this audit catches stale language in **unpatched** sections that referenced pre-resolution states. Run:

```
Grep -n -E "to be produced|in progress|skeleton|pre-M0|never written|if §1\.1 estimate|R-on / R-off" v3_shadertoy_adoption_scope.md
```

For each match, decide: (a) intentional historical note (leave); (b) stale wording that contradicts the now-locked verdicts (add to patch list as a sanity fix).

**0b — File-existence + writability check.** Verify each target file exists before assuming append-mode patches will work:

| File | If present | If missing |
|------|-----------|------------|
| `doc/7/v3_shadertoy_adoption_scope.md` | Proceed with patches | STOP — surface to user (closeout cannot proceed without source-of-truth scope doc) |
| `.wolf/anatomy.md` | Edit as planned | Create with OpenWolf standard header |
| `.wolf/memory.md` | Append as planned | Create with OpenWolf standard header |
| `.wolf/cerebrum.md` | Append to existing sections | **Create fresh** with required structure: `## User Preferences`, `## Key Learnings`, `## Decision Log`, `## Do-Not-Repeat`; carry forward any DNRs from user auto-memory (`feedback_*` entries) — see Step 4 verification sub-step |
| User auto-memory `MEMORY.md` | Append index entry | Create with `# Memory Index` header |

If Step 0 surfaces ≥3 new patch candidates, re-budget Step 1 (each sanity fix adds ~5 min).

### Step 1 — Apply 8 scope-doc patches (single editing session, ~30 min)

The patches are pre-specified in two existing docs. The work is locating each section and applying the exact change.

| Patch | Target section | Source spec | Risk |
|-------|---------------|-------------|------|
| P1 | §3 M1 work order | [v3_m0_stage0_impl.md](v3_m0_stage0_impl.md) §P1 | Low — replace one bullet, cross-link to v20 |
| P2 | §3 M1 budget header | impl §P2 | Low — single number 5h→6–7h |
| P3 | §3 M1 cumulative gate table | impl §P3 | **Medium** — not "append note." The existing M1_PARTIAL_MAGNITUDE row is conditional on "§1.1 Delta #5 estimate"; that estimate is now resolved (< 3%) by Deliverable B. Collapse the resolved conditional ("if #5 ≫ gap → Path B justified" branches are dead) and replace with the now-known verdict: "Path B is necessary BUT NOT sufficient; pivot must hypothesis-refine #3/#4 before considering M2." |
| P4 | §1.1 Path A ceiling + §3 M0 Stage 0 Deliverable B desc | impl §P4 | **Medium** — cascade edit. §1.1 and §3 M0 Stage 0 both describe Deliverable B's method/result. After P4 collapses §1.1 to "executed verdict + link," §3 M0's "Deliverable B — Path A ceiling estimate" bullet must also drop the "Run ShaderToy reference twice…" method prose (now historical) and link to the actual ceiling doc instead. |
| P5 | §2 deltas table, Delta #3 row | impl §P5 | Low — single cell update |
| P6 | §2 deltas table, Delta #7 row | impl §P6 | Low — single cell update + link |
| P7 | §2.1 canonical pointer table footer | impl §P7 | Low — drop "skeleton" wording |
| P8 | §3 M1 cumulative gate (M1_PARTIAL_MAGNITUDE row) OR new §3 M2 prep subsection | reply §Scope-doc impact | **Medium** — placement decision: as appended note to existing M1_PARTIAL row, or as standalone subsection under M2 trigger. See §"Placement decision for P8" below. |

**Sequencing:** apply P1 first (it changes the M1 work order which other patches reference). Then P2–P7 in any order. P8 last (depends on P1's wording for cross-link).

### Step 2 — Update `.wolf/anatomy.md` (~10 min)

9 files across Stage 0 (the original 7 plus this closeout's plan+impl docs):
1. `doc/7/v3_m0_stage0_plan.md`
2. `doc/7/v3_m0_stage0_impl.md`
3. `doc/7/v3_m0_stage0_closeout_plan.md` (this doc)
4. `doc/7/v3_m0_stage0_closeout_impl.md` (TBD, end of this phase)
5. `doc/7/v20_shadertoy_diff_impl.md`
6. `doc/7/critic/07_v20_shadertoy_diff_impl_reply.md`
7. `tools/v3_baseline/delta3_alpha_audit.md`
8. `tools/v3_baseline/delta5_ceiling_estimate.md`
9. `tools/v3_baseline/delta7_offset_audit.md`

(Anatomy is auto-maintained per OpenWolf, but manually adding entries reduces the lag before next auto-scan.)

### Step 3 — Append per-deliverable entries to `.wolf/memory.md` (~5 min)

One-line entry per file create/edit, per the OpenWolf format `| HH:MM | description | file(s) | outcome | ~tokens |`.

### Step 4 — Append cerebrum entries (~10 min)

Three new entries to capture cross-session lessons from this Stage 0 round. **The two memory stores serve different scopes:**

- **`.wolf/cerebrum.md`** (project-level, checked in) — for project-specific learnings about v3 pivot / cascade impl / topology decisions.
- **User auto-memory at `C:\Users\XINDONG\.claude\projects\d--GitRepo-My-radiance-cascades-demo\memory\`** (user-level, persists across this user's sessions in this project dir) — for user-collaboration/workflow lessons.

Per-entry destination:

| Entry | Lesson type | Destination |
|-------|-------------|-------------|
| 1 — direction/sign + derivative-sign self-critique (S2/S3) | Cross-project analytical-doc QA workflow | **User auto-memory** as `feedback_analytical_doc_qa.md` |
| 2 — "literal port" framing misleading (F3) | v3 pivot scope-doc framing rule | **`.wolf/cerebrum.md`** Key Learnings |
| 3 — M1_PARTIAL_MAGNITUDE has no Path B rescue (F2) | v3 pivot decision-tree fact | **`.wolf/cerebrum.md`** Decision Log |

See §"Cerebrum / memory entries" below for exact wording.

**Step 4 verification sub-step (added per critique 08 I12):** after appending the three new entries, audit every DNR / Key Learning entry in `.wolf/cerebrum.md` against its authoritative source:

| Entry type | Authoritative source | Verification |
|-----------|----------------------|-------------|
| Workflow / collaboration DNRs (e.g., bake-time merging, asymmetric filters) | User auto-memory `feedback_*.md` files | Open the cited `feedback_*.md`; confirm cerebrum wording is consistent with source. Update cerebrum to match source if divergent (source wins). |
| Hypothesis-tree DNRs (e.g., "no symptom clamps", "no resolution bumps") | `doc/7/v3_shadertoy_adoption_scope.md §5` Do-Not-Repeat list | Cross-reference DNR numbers; confirm wording match. |
| Decision-log entries (e.g., M1_PARTIAL_MAGNITUDE no Path B rescue) | Stage 0 deliverable that locked the decision (e.g., Deliverable B for the < 3% verdict) | Confirm the cited deliverable doc supports the entry's claim. |

This guards against cerebrum DNRs drifting from their authoritative source between sessions. **Note:** the v25 failure-learnings doc (`v3_shadertoy_adoption_scope.md` §5, was previously `v25_z_mbrc_correction_failure_learnings.md`) is the right source for hypothesis-tree DNRs only — not for workflow DNRs, which live in user auto-memory.

### Step 5 — Self-critique impl + dump impl summary doc (~15 min)

Mirror the structure of [v3_m0_stage0_impl.md](v3_m0_stage0_impl.md): create `v3_m0_stage0_closeout_impl.md` with patches-applied table + self-critique pass + handoff to Stage 1.

**Self-critique checklist (added verbatim from critique 08 I9):** run all 5 items before declaring closeout complete. Each item maps to a real failure mode observed in this or prior closeouts.

1. Does each patch match its spec (target section, change description)? [maps to SI1-class plan-time catches]
2. Are there any stale references to pre-resolution states in unpatched sections? [maps to SI2/SI3 — should now be caught in Step 0, this is the safety net]
3. Does the patched scope doc read coherently end-to-end? [maps to SI3 — requires sequential re-read, not just grep]
4. Were any file-state assumptions violated (files missing that the plan assumed existed)? [maps to SI4 — should be caught in Step 0b, this is the safety net]
5. Did any patch require judgment beyond "copy known verdict into known location"? [maps to P3/P8 strategic-significance check — flag for highlighting in impl doc Strategic Changes section]

If any item flags an issue, fix in-impl (don't defer) and document in the impl's self-critique section.

---

## Placement decision for P8

Two options for where P8 (M2 prep mechanism scoping) lives in the scope doc:

- **Option A — Append as M1_PARTIAL_MAGNITUDE row note in §3 M1 cumulative gate.** Pro: lives next to the verdict it gates. Con: clutters the verdict table with M2-prep work.
- **Option B — Add a new sub-bullet under §3 M2 "Trigger" line.** Pro: M2-prep work belongs with M2. Con: separates the prep from the verdict that triggers it.

**Decision: Option B.** The M2 trigger line already covers the prerequisites for entering M2; adding "M2 prep precondition: Path B mechanism scoping doc must exist before M2 Stage 0 begins" is the natural location. Cross-link from M1_PARTIAL_MAGNITUDE row to M2 trigger so the reader path is preserved.

---

## Cerebrum / memory entries (exact wording)

### Entry 1 — Direction-sign + derivative-sign self-critique (S2/S3 lesson)

> When writing analytical documents (algebraic bounds, "current vs reference" magnitude comparisons), include two self-critique checks before publish:
> 1. **Direction/sign sanity check** — does my claim about "X is wider/tighter than Y" survive looking up the actual numerical values for both? (S2: claimed current cone "too wide"; actual is 4× tighter at sin=0.248 vs ShaderToy's ≥0.92.)
> 2. **Derivative-sign-doesn't-change-on-interval check** — if my argument relies on cancellation due to sign change of f″ or f′ over the integration domain, plot or evaluate f″ at both endpoints to confirm. (S3: claimed cos's d²/dθ² changes sign on [0, π/2]; it's `-cos` < 0 throughout.)
>
> Both bit me in Stage 0 Deliverable A and B respectively. Both are easy to miss in a single-author analysis and trivial to catch with a 30-second numerical check.

### Entry 2 — "Literal port" framing is misleading for cross-topology adoption (F3 lesson)

> When the source and destination of an algorithmic port differ in **topology** (surface-attached vs volumetric, structured-grid vs scattered, etc.), the elevator pitch "port the line" understates the divergence at the algorithmic-mechanism level. Two of seven deltas (#3, #6) required structural reframing, not "port the line."
>
> Future pivot/adoption scope docs should not promise "drop-in" mechanics; the honest framing is "disciplined re-derivation guided by reference."

### Entry 3 — M1_PARTIAL_MAGNITUDE has no Path B rescue (F2 lesson)

> Discovered post-Stage 0: under the v3 ShaderToy adoption, Delta #5 (the only Path B-exclusive magnitude lever) has < 3% leverage per algebraic bound. Therefore if M1 returns PARTIAL_MAGNITUDE, switching to Path B will NOT close the gap — Path B's value is topology cleanup, not magnitude.
>
> Implication: M1_PARTIAL_MAGNITUDE → Path B was previously auto-justified by the scope doc's verdict bands; it now requires hypothesis-refinement on #3/#4 instead. The locked decision tree: PARTIAL_MAGNITUDE → return to #3/#4 work; do NOT auto-proceed to M2.

---

## Risk + budget

**Budget:** ~90 min total (Step 0: 10 [added per I3+I4], Step 1: 35, Step 2: 10, Step 3: 5, Step 4: 15, Step 5: 15).

**Step 1 budget breakdown by patch category (added per I1):** mechanical patches (P1, P2, P5, P6, P7) at ~3 min each = 15 min; judgment patches (P3, P4, P8) at ~6 min each (2× mechanical rate) = 18 min; rounding/transitions = 2 min. Total ≈ 35 min. If Step 0 surfaces sanity fixes, add ~5 min per fix (sanity fixes are mechanical by definition — the judgment was already done at audit time).

**Risks:**
- **R1 — P8 placement decision wrong** → reader navigates to M2 trigger expecting prep info but finds nothing. **Mitigation:** explicit cross-link from M1_PARTIAL_MAGNITUDE row.
- **R2 — Scope doc edits introduce broken internal links** → reader can't navigate. **Mitigation:** after patches land, run `Grep` on the scope doc for two patterns: (a) `\]\(\)` (empty-target links) and (b) `\]\([^)]*v20_shadertoy[^)]*\)` (verify the v20 ref in P1/P7 cross-links resolves to the actual file path). Confirm no stale "skeleton" / "in progress" / "pre-M0 prework" wording remains in the patched sections.
- **R3 — Memory/anatomy entries diverge from actual file state** → next session misnavigates. **Mitigation:** anatomy auto-maintains; memory entries are append-only, low-risk.
- **R4 — P3/P4 cascade edits cause inconsistency between §1.1 and §3 M0** (added per SC5) → reader sees two different descriptions of Deliverable B. **Mitigation:** treat P3+P4 as a single editing unit; both land together and a re-read of §1.1 + §3 M0 Stage 0 happens immediately after.

**Stop conditions:**
- If a scope patch reveals a logical inconsistency with a Stage 0 deliverable (e.g., P5 wording contradicts the C+ audit verdict), STOP and surface to user before continuing.
- If patches accumulate beyond ~10 (because new follow-up edits emerge), STOP and re-scope rather than absorb silently.

---

## Acceptance

Stage 0 closeout is complete when:
1. All 8 scope-doc patches applied and the scope doc reads coherently end-to-end (re-read §1.1 + §2 + §3 M0/M1/M2 sequentially after patches land).
2. `.wolf/anatomy.md` + `.wolf/memory.md` + `.wolf/cerebrum.md` reflect the Stage 0 outputs; user auto-memory has the analytical-doc QA feedback entry.
3. `v3_m0_stage0_closeout_impl.md` summarizes the patches with a self-critique pass.
4. Link-verification (per R2) passes: no `\]\(\)` matches; v20 cross-link path resolves; no stale "skeleton" / "pre-M0 prework" wording in patched sections.

After acceptance, M0 Stage 1 (baseline captures, ~3h per scope §3) can begin.

---

## Self-critique pass on this plan (SC1–SC5)

Reviewed the initial draft and found:

- **SC1 — Cerebrum split.** Original draft said "append cerebrum entries" generically; project `.wolf/cerebrum.md` and user auto-memory are two separate stores with different scopes. **Fixed in Step 4** with a per-entry destination table.
- **SC2 — File-count error.** Step 2 said "7 files" but listed 9. **Fixed in Step 2** with correct "9 files (original 7 + 2 closeout docs)" framing.
- **SC3 — P3 risk underestimated.** "Append note" is wrong; the existing M1_PARTIAL_MAGNITUDE row's §1.1-conditional language is now resolved (#5 < 3%) and needs collapse, not augmentation. **Fixed in Step 1 patch table** with explicit rewrite instructions.
- **SC4 — Acceptance #4 had no verification method.** **Fixed in R2 + Acceptance** with concrete grep patterns + stale-wording check.
- **SC5 — P4 cascade-edit risk missed.** §1.1 and §3 M0 Stage 0 both describe Deliverable B; P4 alone leaves a stale duplicate. **Fixed in Step 1 patch table** (P4 row expanded to cover both sites) and **R4 added** as explicit risk.

No issues found that would require restructuring the plan or expanding scope beyond the original 5-step breakdown.
