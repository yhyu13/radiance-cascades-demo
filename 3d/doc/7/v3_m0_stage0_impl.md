# M0 Stage 0 — Impl summary

**Date:** 2026-05-26.
**Predecessor plan:** [v3_m0_stage0_plan.md](v3_m0_stage0_plan.md).
**Scope:** completed the four pre-M0 deliverables (A/B/C/C+) defined in [v3_shadertoy_adoption_scope.md §3 Stage 0](v3_shadertoy_adoption_scope.md).

This doc is the final summary that closes M0 Stage 0. Stage 1 (baseline captures) can begin after sign-off.

---

## Deliverables — outputs

| Letter | Goal | Output | Verdict |
|--------|------|--------|---------|
| C  | Probe-position -0.5 / +0.5 audit | [tools/v3_baseline/delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md) | **Delta #7 CONFORMANT.** Removed from M1. |
| C+ | Delta #3 α=0 semantics | [tools/v3_baseline/delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md) | **Case B (stronger).** α conventions inverted; literal port discards radiance. Delta #3 requires structural redefinition. |
| A  | Per-delta source-of-truth diff | [doc/7/v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md) | All 7 deltas documented with code paste + diff + topology tag. |
| B  | Path A ceiling estimate for #5 | [tools/v3_baseline/delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md) | **Algebraic. < 3% magnitude leverage** (below 10% threshold). #5 is performance/topology, not magnitude. |

**Execution order followed:** C → C+ → A → B (as planned per §7 of plan doc, after self-critique C1 reordered from the original A-first sequence).

**Plan time vs actual:**

| Deliverable | Plan estimate | Actual session | Notes |
|-------------|---------------|----------------|-------|
| C | ~30 min | within budget | Audit table mechanical; +0.5/-0.5 convention turned out fully uniform. |
| C+ | ~30 min | within budget | But finding was stronger than expected — Case B not Case A/C, and the recommendation expanded scope (1-2 sessions for redefined #3, not the original < 1 session). |
| A | ~90 min | within budget | Self-critique surfaced two bugs (broken link + reversed cone-width claim) corrected in revision pass. |
| B | ~30-45 min | within budget | Self-critique caught wrong cancellation argument; replaced with clean Jensen-based bound (tighter result, 0.64% instead of 2-3%). |

---

## Cross-deliverable findings

These emerged only from looking at C, C+, A, B together — they are not visible in any single deliverable.

### F1 — The bright tail is structural, not from any of the "obvious" deltas

- C: #7 was conformant → not a contributor.
- C+: #3's literal mechanism (skip rgb on α=0) is wrong; the actual mechanism (per-corner gating) requires structural work.
- B: #5 has < 3% magnitude leverage → not a contributor.

**The remaining magnitude levers are #3 (redefined) and #4 (MB-feedback formulation).** Everything else is either landed, conformant, performance-only, or geometry-only.

### F2 — Path A's reachable verdicts are narrower than scope §3 implied

The original scope §3 verdict bands (M1_CLOSES_GAP / M1_PARTIAL_GEOMETRY / M1_PARTIAL_MAGNITUDE / M1_DEAD) assume all 4 work items (#3, #4, #6, #7) have meaningful magnitude potential. Post-Stage-0:
- #7 contributes 0 (conformant).
- #5 contributes < 3% (excluded from Path A anyway).
- #6 alone is marginal-without-#3 (see A Delta #6 entry).
- **#3 + #4 are the only real Path A magnitude levers.**

If #3 + #4 together fall short, Path B will NOT close the gap (per B's verdict on #5 + the topology argument). **M1_PARTIAL_MAGNITUDE has no Path B rescue.** This was not visible in the original scope; need a scope-doc patch.

### F3 — The "literal port" framing was misleading throughout the pivot rationale

Two of seven deltas (#3, #6) require structural reframing — not "port the line." The pivot's elevator pitch ("port ShaderToy's known-working algorithm into volumetric") understated the divergence between surface-attached and volumetric topology at the algorithmic-mechanism level (not just at the bake/consume-side cosine level). This is per the I11 issue from the critic review but is now confirmed concrete.

**Implication for cerebrum:** the v3 pivot is a **disciplined re-derivation guided by ShaderToy**, not a port. Future scope docs should not promise "drop-in" mechanics.

---

## Self-critique pass on the impl

Issues found during self-critique and the corrections applied:

### S1 — Deliverable A had a broken placeholder link (FIXED)

Original Delta #1 entry referenced `[project_mbrc_v20_postfix_landed.md](../../../.claude/projects/...)` — a placeholder, not a real path. **Fixed:** removed the parenthetical reference; the LANDED status doesn't require citing the memory file by URL.

### S2 — Deliverable A Delta #6 had the cone-width direction REVERSED (FIXED)

Original wording: "Current impl's cone is therefore generally too wide, accepting visibility samples that ShaderToy would reject."

**Reality:** Current's `uUpperBinConeSin` = 0.248 (D=8, from [demo3d.cpp:2461-2463](../../src/demo3d.cpp#L2461-L2463)). ShaderToy's `sin(theta)` for probeSize=4 (cascade 1) = sin(3π/8) ≈ 0.92, → 1.0 for larger cascades. **Current is much TIGHTER (more rejective) than ShaderToy.**

Worse, the two cone quantities measure fundamentally different things — current's is bin-width (discretization-derived), ShaderToy's is probe-apparent-extent (geometry-derived). They aren't directly comparable as "wider vs tighter" without unpacking the semantics.

**Fixed:** rewrote Delta #6's semantic-diff paragraph to (a) cite the actual numerical values, (b) state the framings differ, (c) note current is tighter, (d) reframe the work as "cone-derivation refactor + tuning sweep" rather than a one-line code change.

### S3 — Deliverable B used an incorrect cancellation argument (FIXED)

Original claim: "per-bin first-moment errors cancel because cos's second derivative changes sign (concave near θ=0, convex near θ=π/2)."

**Reality:** cos's second derivative is `-cos` which is **negative throughout [0, π/2]** — no sign change, no cancellation. The original "2-3% bound after cancellation" rested on wrong reasoning.

**Fixed:** replaced with an exact Jensen-style bound using `(sin(c+h) - sin(c-h))/(2h) = cos(c) · sin(h)/h`. For D=8, sin(π/16)/(π/16) ≈ 0.9936, so bin-center over-estimates by 0.64% **systematically** (not cancellable). The total irradiance inherits this as a uniform scale, so the bias is bounded by 0.64% — TIGHTER than the original (wrong) 2-3% argument. The qualitative conclusion ("#5 is small lever") is unchanged; the rigor is now correct.

### S4 — Combined-bound number in Deliverable B was opaque (FIXED)

Original: "Combined upper bound from D1+D2+D3: < 3%."

**Fix:** added breakdown showing D1 dominates at ~2% (octahedral solid-angle non-uniformity), D3 < 1% (Jensen bin-center bias), D2 = 0 (performance-only). And added the "uniform scale shift" caveat — it changes brightness level but not bright%/dim% distribution shape.

### S5 — Deliverable A's M1 work order conflicts with scope §3 (FIXED in A; scope §3 needs patch)

Original scope §3 M1 work order: {#3, #4, #6, #7}. Post-Stage-0 actual: {#3, #6, #4} (#7 removed; #6 reordered to follow #3).

**Fix in A:** the summary table at the bottom of A reflects the actual order with rationale. **Still needed: a scope §3 patch to match.** Listed in §6 below.

### S6 — Plan/impl drift on Deliverable scope (NOTED, not fixed)

Plan said C+ was "30 min" — actual was within budget, but the finding (Case B + recommendation to expand #3 to 1-2 sessions) materially changes M1 budget. Plan budgeted M1 at "≤ 5h" assuming #3 was a 1-session item; now it's 1-2 sessions on its own. Scope §3 M1 budget needs revision: **5h → 6-7h.**

Listed as a scope-doc patch in §6.

### S7 — No deliverable mentions the cerebrum/buglog update protocol (TODO)

Per CLAUDE.md, significant findings (S2, S3 in particular — direction-reversal and wrong-math bugs in pre-implementation analytical documents) should be logged to buglog and have cerebrum entries. This impl summary doc is not the right place; will append separately.

---

## Scope-doc patches needed (post-Stage-0)

Edits to [v3_shadertoy_adoption_scope.md](v3_shadertoy_adoption_scope.md) to reflect Stage 0 findings:

| Patch | Section | Change |
|-------|---------|--------|
| P1 | §3 M1 work order | Replace {#3, #4, #6, #7} with {#3-redefined, #6-conditional, #4}; cross-link to [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md) summary table. |
| P2 | §3 M1 budget | Revise from 5h to 6-7h to accommodate #3's expanded scope. |
| P3 | §3 verdict bands | Add note: **M1_PARTIAL_MAGNITUDE has no Path B rescue** (per F2 above + B's verdict). The path forward in that case is hypothesis-refinement on #3/#4, not Path B. |
| P4 | §1.1 Path A ceiling | Replace placeholder with: "Algebraic estimate per [delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md): < 3% irradiance leverage, < 1% bright% shape leverage. Method tag: algebraic." |
| P5 | §2 Delta #3 description | Replace "Bake's smoothstep merge feeds dead .rgb when α=0" with "Per-corner visibility-weighted bilinear merge — see [delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md) for the structural redefinition." |
| P6 | §2 Delta #7 entry | Mark "CONFORMANT (no work)" with link to [delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md). |
| P7 | §2.1 canonical pointer table | Replace the "M0 Stage 0 Deliverable A skeleton" line with a link to the now-existing [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md). |

**These patches are NOT applied by this doc.** Apply them as a separate step before M0 Stage 1 begins, so that the scope doc reflects the locked Stage 0 reality.

---

## Stage 0 → Stage 1 handoff

**What's locked at end of Stage 0:**
- M1 work order: {#3-redefined, #6-conditional, #4}.
- M1 budget: 6-7h (was 5h).
- Path A magnitude levers reduced to #3 + #4.
- Path B's value scoped to topology, not magnitude.
- M1_PARTIAL_MAGNITUDE failure mode has no Path B rescue.
- All four deliverables on disk with citations.

**What Stage 1 needs to do (per scope §3 Stage 1, ~3h):**
1. Capture cornell baselines: cam0 + cam2 with v2.0-postfix Default (per [project_mbrc_v20_postfix_landed.md] in memory).
2. Capture cornell PT reference frame (already exists per recent work; verify).
3. Capture Sponza baselines: 1 camera angle minimum.
4. Capture Sponza PT reference (N=2048 first; if not converged, ladder to N=4096; if still not converged, log provisional bands per scope §3 contingency).
5. Write captures into `tools/v3_baseline/baseline_lock.json` with file paths + hashes.
6. Sign off Stage 1 → M0 closed → M1 Stage 0 (impl doc for redefined #3 + #6 bundle) begins.

**Pre-committed gates for M1 Stage 0:**
- M1 impl doc for #3 (redefined) MUST front-load the [delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md) verdict and define the volumetric per-corner gating mechanism BEFORE any code lands.
- M1 impl doc for #6 MUST bundle with #3 (per A's port disposition) — single A/B harness covers both.

---

## Housekeeping

Files created this session:
- `tools/v3_baseline/delta7_offset_audit.md`
- `tools/v3_baseline/delta3_alpha_audit.md`
- `tools/v3_baseline/delta5_ceiling_estimate.md`
- `doc/7/v20_shadertoy_diff_impl.md`
- `doc/7/v3_m0_stage0_plan.md`
- `doc/7/v3_m0_stage0_impl.md` (this file)
- `doc/7/critic/06_v3_shadertoy_adoption_scope_reply.md`

To do as separate step (out of scope for this Stage 0 impl, but called out per CLAUDE.md):
- Update [.wolf/anatomy.md](../../.wolf/anatomy.md) with the seven new files above.
- Append to [.wolf/memory.md](../../.wolf/memory.md) — one line per deliverable.
- Append to [.wolf/cerebrum.md](../../.wolf/cerebrum.md) the lesson from S2/S3: when writing analytical documents (especially algebraic bounds and "current vs reference" comparisons), self-critique should include a **direction/sign sanity check** and a **derivative-sign-doesn't-change-on-interval check** — both bit me here and are easy to miss in a single-author analysis.
- Apply the seven scope-doc patches (P1-P7).
- Sign off and proceed to M0 Stage 1.

## Open questions deferred to Stage 1 or later

- **Sponza PT reference reachability** at N=4096: the scope's contingency ladder addresses this, but until we actually run it we don't know if cornell-converged extrapolation will be a real fallback.
- **Whether the Delta #3 structural redefinition will actually be 1-2 sessions** or whether the per-corner-gated trilinear is more interlinked with `sampleUpperDirWeighted` than the audit assumed. The audit identified the right two call sites but didn't depth-estimate the surgery.
- **Whether D=8 → D=16 dirRes ablation already done (v2.4)** affects Delta #3's effective leverage in M1 (more bins = finer rejection granularity = potentially more impact from per-corner gating). Recall v2.4 was DEAD at dirRes=16 for the bright-tail per [project_v24_dead_c0_dirres.md] in memory. Worth flagging at M1 impl-doc time.
