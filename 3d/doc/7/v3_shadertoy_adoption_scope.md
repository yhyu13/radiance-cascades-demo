# v3 — ShaderToy RC adoption: scope, milestones, gates

> ⚠️ **SUPERSEDED by [v4 ShaderToy Adoption Scope](../8_shadertoy/v4_shadertoy_adoption_scope.md)**
> **Date superseded:** 2026-05-28
> **Why:** The v3 M1 plan (port Deltas #3/#6/#4) was abandoned after the 2×2 matrix returned DEAD on 2026-05-27. The subsequent 11-stage diagnostic chain (Stages 2–11d) found the real constraints: Sponza MB-gain=0.10 clears the retirement gate, Cornell point-light under-emit is a volumetric topology limit. The v4 plan documents the actual findings and the new scope.
> **This document is preserved for historical reference only.** Read v4 for the current plan.

**Date:** 2026-05-26.
**Predecessor:** [v25_z_mbrc_correction_failure_learnings.md](v25_z_mbrc_correction_failure_learnings.md) — closes the v2.x correction line; locks the pivot direction.
**Goal:** fully adopt the in-tree ShaderToy 3D RC reference ([shader_toy/](../../shader_toy/)) as the production radiance pipeline. Retire the current `radiance_3d.comp` bake chain and the hybrid per-pixel correction safety net.

This document defines the milestones, pre-committed acceptance bands, rollback criteria, and the topology decision that gates phase ordering. It does NOT pre-decide implementation details inside each milestone — those land in per-milestone impl docs as the milestones run.

---

## 1. The topology fork (first-order decision)

The ShaderToy reference and our current impl differ in one architectural prior that constrains every subsequent port choice (see [v20_shadertoy_diff_impl.md §2](v20_shadertoy_diff_impl.md)):

|                | ShaderToy reference                                | Current impl                                       |
|----------------|-----------------------------------------------------|----------------------------------------------------|
| Probe placement | **Surface-attached** — each probe carries `gTan/gBit/gNor/gPos` for the wall it sits on | **Volumetric** — 3D grid, no surface association  |
| Probe rays     | Hemisphere above `gNor` (octahedral `θ ∈ [0, π/2]`) | Full sphere via `binToDir` over S²                |
| Bake stores    | Pre-integrated `L · cos(θ) · ΔΩ` per bin            | Raw `L_in(ω)` per bin + binary α                  |
| Consumer       | Cubemap fetch (integral done at bake)               | Per-pixel hemispheric Riemann sum at consume time |

The pivot must commit to one of two paths:

### Path A — keep volumetric topology, port what's portable

Bank Deltas #1+#2 (already landed). Port Deltas #3, #4, #6, #7 (do not require surface normal). Skip Delta #5 (bake-time cosine pre-weighting; requires `gNor`, not portable to volumetric).

**Cost:** 1–2 sessions.
**Ceiling:** unknown — Deltas #1+#2 were the dominant magnitude levers, so the remaining four may not move the |p95| needle much. Honest framing: this is "complete the diff-driven port within the current topology."

### Path B — full topology switch to surface-attached

Rewrite probe placement (mesh-aware), atlas layout (per-surface cube faces), bake (hemisphere over `gNor`), consumer (cubemap fetch). Adopts all 7 deltas including #5.

**Cost:** 3–6 sessions of structural rewrite. Sponza/non-Cornell scenes need a probe-placement strategy (currently they share the same volumetric grid).
**Ceiling:** the reference's actual algorithm, by construction. This is the "fully adopt" reading of the pivot.

**Recommendation:** Path A first as a forcing function — it tells us whether Deltas #3/#4/#6/#7 alone close the bright tail. If they do, Path B is unnecessary. If they don't, Path A's result quantifies what's left for Path B to close. **Doing A → conditional B is strictly cheaper than going straight to B and discovering A would have sufficed.**

**LOCKED 2026-05-26:** Path A → conditional Path B. Re-evaluate at M1 cumulative gate.

### 1.1 Path A ceiling estimate (RESOLVED — Stage 0 Deliverable B, P4)

**Verdict (locked 2026-05-26):** algebraic bound per [delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md) → **< 3% magnitude leverage** (< 1 pp bright% shape leverage; uniform scale shift, not distribution-shape change). Method tag: algebraic (Jensen bound on bin-center cosine eval, octahedral solid-angle non-uniformity, hemisphere clipping topology).

**Consequence for §3 M1 verdict bands:** Delta #5 has insufficient magnitude leverage to rescue M1_PARTIAL_MAGNITUDE via topology switch alone. Path B's value is topology cleanup, not pre-weighting. See §3 M1_PARTIAL_MAGNITUDE row (P3) for the locked decision tree.

**Method used (historical):** algebraic primary path; an empirical ShaderToy R-on / R-off ablation was scoped as the original method but dropped during Stage 0 self-critique (C2/C3) in favor of the tighter Jensen-based algebraic bound.

---

## 2. The seven deltas — port status

| Delta | Description | Path A portable? | Path B portable? | Current status |
|-------|-------------|------------------|------------------|----------------|
| **#1** | Consumer drops surface-hit bins from irradiance integral | ✓ | ✓ | **LANDED** (v2.0-postfix) |
| **#2** | Consumer normalizes to weighted mean, not Riemann sum | ✓ | ✓ | **LANDED** (v2.0-postfix) |
| **#3 (redefined)** | Per-corner visibility-weighted bilinear merge. **Structural redefinition** per [delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md) (P5): α conventions are inverted; literal port discards radiance. Volumetric analog = 8-corner gated trilinear (rejected corner contributes 0 to both numerator and denominator). See [v20_shadertoy_diff_impl.md §Delta #3](v20_shadertoy_diff_impl.md) for the 8-corner formula. | ✓ (redefined) | ✓ | NOT STARTED — M1 work, bundled with #6 |
| **#4** | Multi-bounce stochastic single-bin vs 4-cube-read average | ✓ | ✓ | NOT STARTED |
| **#5** | Bake-time cosine + per-bin solid-angle pre-weighting | ✗ (no `gNor`) | ✓ | NOT STARTED (Path B only) |
| **#6** | WeightedSample θ-of-ray vs θ-of-bin semantics | ✓ | ✓ | NOT STARTED — ordering "smallest leverage suspect" is low-confidence; conditionally re-ordered (see §3 M1) |
| **#7** | Probe-position -0.5 offset placement convention | ✓ | ✓ | **CONFORMANT (no work)** per [delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md) (P6). Uniform convention across all sites. Removed from M1 entirely. |

### 2.1 Delta sources (canonical code locations)

[v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md) (Stage 0 Deliverable A, shipped) now holds the canonical per-delta inventory; this table remains as a quick-jump index. Each M1 per-delta impl doc references v20 for code paste + diff rather than re-deriving.

| Delta | ShaderToy reference location | Current-impl mirror location | Status |
|-------|------------------------------|------------------------------|--------|
| #1 | [shader_toy/Image.glsl](../../shader_toy/Image.glsl) — irradiance integration | `src/shaders/probe_consume.glsl` (or equivalent consumer shader) | LANDED v2.0-postfix |
| #2 | [shader_toy/Image.glsl](../../shader_toy/Image.glsl) — weighted-mean normalization | (same as #1) | LANDED v2.0-postfix |
| #3 | [shader_toy/CubeA.glsl](../../shader_toy/CubeA.glsl) — smoothstep merge of upper cascade | `src/shaders/radiance_3d.comp` — `upperDir`/`smoothstep` block | NOT STARTED |
| #4 | [shader_toy/CubeA.glsl](../../shader_toy/CubeA.glsl) — multi-bounce 4-cube-read average | `radiance_3d.comp` — `sampleC0AtlasStochastic` and call sites | NOT STARTED |
| #5 | [shader_toy/CubeA.glsl](../../shader_toy/CubeA.glsl) — `L · cos(θ) · ΔΩ` pre-weighting | (no current analog — bake stores raw radiance + binary α) | PATH B ONLY |
| #6 | [shader_toy/Common.glsl](../../shader_toy/Common.glsl) — `WeightedSample` θ handling | Consumer shader's bin→direction mapping (paired with #1/#2) | NOT STARTED |
| #7 | [shader_toy/Common.glsl](../../shader_toy/Common.glsl) — probe position math | `radiance_3d.comp` probe-position derivation + Phase 5d trilinear + Phase 5f bilinear sample sites | M0 AUDIT |

**Canonical source-of-truth diff doc (P7):** [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md) (Stage 0 Deliverable A, shipped). Per-delta entries with ShaderToy code paste + current-impl code paste + semantic diff + topology tag + port disposition. Includes M1 summary work-order table; per-delta M1 impl docs reference this rather than re-deriving.

---

## 3. Milestones

### M0 — Baseline lockdown (~5 h total: Stage 0 ~2.5 h + Stage 1 ~3 h)

**Goal:** capture and freeze the v2.0-postfix Default state as the pre-pivot reference. Anything subsequent diffs against this. Also produce the pre-work that the per-delta M1 impl docs depend on.

#### Stage 0 — Pre-work (~2.5 h, must complete before Stage 1)

These four deliverables address review findings (I1, I2, I5, I11) and produce the source-of-truth artifacts that M1 per-delta impl docs reference.

- **Deliverable A — `v20_shadertoy_diff_impl.md` (~60 min) — SHIPPED.** [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md). All 7 deltas with code paste + diff + topology tag + port disposition + summary work-order table. Addresses I1.
- **Deliverable B — Path A ceiling estimate (~60 min) — SHIPPED.** [delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md). Algebraic bound: < 3% magnitude leverage. §1.1 holds the locked verdict. Method tag: algebraic (Jensen). Empirical ShaderToy ablation was the originally-scoped method but was dropped during Stage 0 self-critique in favor of the tighter algebraic bound. Addresses I2.
- **Deliverable C — Probe-position -0.5 offset audit (~30 min) — SHIPPED.** [delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md). Verdict: **Delta #7 CONFORMANT** (uniform +0.5/-0.5 convention across all probe-sampling sites). Removed from M1. Addresses I5.
- **Deliverable C+ — Delta #3 α=0 semantics audit (~30 min) — SHIPPED.** [delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md). Verdict: **Case B (stronger than expected)** — α conventions are inverted between implementations; ShaderToy's per-corner WeightedSample gates both numerator and denominator. Literal port discards radiance. Delta #3 requires structural redefinition (per-corner gated trilinear); M1 scope expanded to 1-2 sessions. Addresses I11.

**Stage 0 acceptance:** all 4 deliverables on disk. v3 scope §1.1 has the #5 numeric estimate. `baseline_lock.json` has Delta #3 and #7 audit verdicts. M1 cannot start until Stage 0 is complete.

#### Stage 1 — Capture baselines (~3 h)

- **Cornell baseline (existing):** Re-run `tools/v20_convergence/cv1_capture.ps1` at N=2048 (cornell/cam0/MB-ON g=1.0/hybrid-OFF/mode-17). Already exists at `tools/v20_convergence/captures_cv1_postfix/N2048` — verify hashes/dates and re-capture if stale. (~15 min if cached, ~45 min if re-render needed.)
- **Cornell hybrid-ON baseline:** Capture at same config with `--use-hybrid=1`. (~30 min.) This is what the pivot must match before hybrid retirement.
- **Sponza measurement harness (NEW infra, ~60 min).** Fork `cv1_capture.ps1` to `tools/v3_baseline/sponza_capture.ps1` — replace cornell scene config with Sponza, set canonical atrium camera, extend metric script to handle Sponza-sized framebuffer + Sponza PT reference. Verify the metric output schema matches the cornell harness so a single `baseline_lock.json` can hold both.
- **Sponza PT convergence verification (contingency ladder, see below).** Run Sponza PT reference at N=2048 twice; compute variance across runs on the tracked metrics. If variance < 5% → converged, proceed. Otherwise escalate per ladder below.
- **Sponza baseline (cascade) + Sponza baseline (hybrid-ON):** Capture both. (~45 min for both.)
- **Snapshot to `tools/v3_baseline/baseline_lock.json`:**
  ```
  cornell_cascade  : ratio=0.977, |p95|=0.883, bright%=11.1, dim%=5.1
  cornell_hybrid   : (to be measured)
  sponza_cascade   : (to be measured) [provisional: true/false]
  sponza_hybrid    : (to be measured) [provisional: true/false]
  delta_3_audit    : (verdict from Stage 0 C+)
  delta_7_audit    : (verdict from Stage 0 C)
  delta_5_estimate : (numeric from Stage 0 B)
  ```

**Sponza PT convergence contingency ladder (I8):**
1. **N=2048 stable** (variance < 5% across re-runs) → proceed normally; `provisional: false`.
2. **N=2048 unstable, N=4096 budget 30 min available** → escalate to N=4096. If stable → `provisional: false`. If 30 min budget exceeded without stability → step 3.
3. **N=4096 unstable or budget exceeded** → use N=2048 result with provisional Sponza bands: M1/M2 evaluate Sponza against |p95| ≤ 0.70 (not 0.50), bright% ≤ 8% (not 5%). Mark `sponza_*` entries with `provisional: true`. The strict |p95| ≤ 0.50 retirement criterion (per §7 lock) **still applies to cornell** without relaxation.
4. **M3 hybrid retirement requires Sponza moved off provisional status.** If Sponza was provisional at M0, M3 cannot proceed until Sponza PT convergence is re-attempted (higher N, different sampling strategy, or re-derived camera) and `provisional: false` is achieved.

**Acceptance:** baseline file exists for all 4 capture entries + 3 audit entries; Sponza measurement harness lands as reusable infra; provisional status is explicit per entry.

**Gate:** M0 is mechanical for cornell, structural for Sponza. The contingency ladder bounds the stall; M0 cannot consume more than ~5 h end-to-end without escalating to user.

### M1 — Path A port (Deltas #3-redefined, #6-bundled, #4) (~6-7 h total, P2)

**Goal:** land the portable deltas inside the current volumetric topology. Measures whether the diff-driven port alone closes the gap.

**Post-Stage-0 delta scope (P1):** the canonical work order and per-delta dispositions live in [v20_shadertoy_diff_impl.md §Summary table — M1 work order](v20_shadertoy_diff_impl.md). Stage 0 outcomes that changed the M1 list:
- **#7** — CONFORMANT per [delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md). **Removed from M1 entirely.**
- **#3 (redefined)** — Case B per [delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md): α conventions are inverted between implementations; the literal port discards radiance. Requires structural redefinition (per-corner gated trilinear). **In M1 with expanded scope (1-2 sessions instead of 1).**
- **#6** — bundled with #3 in a single A/B harness (shared 2×2 matrix). Sequencing: #6 follows #3. Drop-rule applies (see v20 summary table).
- **#4** — formulation-comparative A/B (deterministic-N-sample vs current MC stochastic-1-sample), not an additive port. Verdict semantics differ from STRONG/MARGINAL/DEAD bands; see v20 entry.

**Net M1 work order:** {#3-redefined, #6-bundled, #4}. Estimated budget: **6-7 h** (revised from 5h to accommodate #3's expanded scope per P2).

**Per-delta gate (each in its own impl doc) — evaluated on BOTH cornell and Sponza:**

| Verdict | Bands (cornell/cam0/N=2048 AND sponza/atrium/N=2048 vs M0 cascade baselines) | Action |
|---------|-------------------------------------------------------------------------------|--------|
| STRONG  | ratio shift ≤ 0.05, **\|p95\| drops ≥ 30%**, bright% drops ≥ 3 pp, dim% not worse by > 2 pp — on **both** scenes | Land; proceed to next delta |
| MARGINAL | ratio shift ≤ 0.10, \|p95\| drops 10–30%, bright% drops 1–3 pp — on **either** scene with the other not regressing | Land if cumulative; document per-scene split |
| DEAD     | otherwise, OR Sponza regresses visually | Revert; mark delta as "not portable to volumetric" in failure-learnings |

**Sponza-specific veto:** any delta that regresses Sponza atrium GI visually is DEAD regardless of cornell numbers. Sponza is first-class per 2026-05-26 lock.

**M1 cumulative gate (after all M1 deltas land) — bands evaluated on cornell AND sponza:**

| Verdict | Bands (must hold per row) | Action |
|---------|---------------------------|--------|
| **M1_CLOSES_GAP** | ratio ∈ [0.95, 1.05] AND \|p95\| ≤ 0.50 AND bright% ≤ 5% AND dim% ≤ 5% — on **both** cornell and sponza | Skip M2; proceed to M3 (hybrid retirement under strict criterion) |
| **M1_PARTIAL_GEOMETRY** | M1_CLOSES_GAP holds on **cornell**, gap remains on **sponza** | Path B is **likely** to close the gap — the cornell pass shows the deltas work for simple geometry; the sponza miss isolates the residual to topology/geometry mechanisms (hemisphere-only sampling under `gNor`, per-corner gating in surface-attached basis, surface-aware probe placement) rather than to magnitude (Delta #5 alone is < 3% per Stage 0 Deliverable B). Commit to Path B with high confidence; M2 prep precondition (P8) defines which Path B mechanism owns the close. |
| **M1_PARTIAL_MAGNITUDE** | Improvement on both scenes but neither hits M1_CLOSES_GAP; OR \|p95\| ≤ 0.70 on both scenes | **Path B has no rescue (P3, post-Stage-0).** Stage 0 Deliverable B ([delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md)) bounds Delta #5 at < 3% of Lambertian irradiance — the only Path B-exclusive magnitude lever cannot close a multi-pp gap. **Action:** return to #3/#4 hypothesis-refinement; do NOT auto-proceed to M2. M2's value (per scope §M2 trigger + Path B mechanism scoping precondition, P8) is topology cleanup, not magnitude. |
| **M1_DEAD**       | no individual delta landed STRONG/MARGINAL on either scene | Path A failed entirely. Must commit to Path B or accept current Default + hybrid permanently. |

In all PARTIAL/DEAD cases, M3 is NOT permitted under the strict retirement criterion (§7 lock).

### M2 — Path B topology switch (conditional, ~3-6 sessions)

**Trigger:** M1 returns PARTIAL_GEOMETRY (high confidence) or DEAD (last resort, last attempt before pivot termination) AND user commits to Path B after seeing M1 numbers. **PARTIAL_MAGNITUDE no longer triggers M2** per Stage 0 Deliverable B (P3): Delta #5's < 3% magnitude leverage means topology switch alone cannot close a multi-pp gap; M2 is reserved for topology-leverage cases only.

**M2 prep precondition (P8 — Path B mechanism scoping doc).** Before M2 Stage 0 begins, a separate Path B mechanism scoping doc (`doc/7/v3_m2_pathb_mechanism_scope.md` or equivalent) must enumerate which Path B mechanisms close which gaps with what magnitude estimate. Three candidate mechanisms surfaced during Stage 0 (see [v20_shadertoy_diff_impl.md §Delta #5 — Path B mechanism candidates](v20_shadertoy_diff_impl.md) and [critic/07_v20_shadertoy_diff_impl_reply.md §I5](critic/07_v20_shadertoy_diff_impl_reply.md)), **each tagged as HYPOTHESIS (not proven; magnitude estimate deferred to M2 Stage 0 per critic/07 I5 partial-acceptance):**

- (a) **Hemisphere-only sampling** — HYPOTHESIS that restricting bake-time integration to the upper hemisphere of the surface normal improves both perf (fewer cancelling samples) and conditioning (less variance from below-horizon noise). Magnitude unknown — depends on actual below-horizon contribution in current bake. Source: critic/07 I5 partial-acceptance.
- (b) **Per-corner gating under surface-attached topology** — HYPOTHESIS that the surface-attached probe layout makes Delta #3's per-corner visibility weighting (currently a Path A redefinition under Case B per delta3 audit) recover with a *native* geometric basis (probe corners already align with surface tangent frame). Magnitude unknown — depends on whether Delta #3's Case B impl already captures the leverage or whether topology adds further. Source: critic/07 I5 partial-acceptance.
- (c) **Surface-aware probe placement** — HYPOTHESIS that scattering probes on triangle surfaces (vs the current 3D-grid lattice) provides geometric leverage on meshes with significant surface-detail-vs-grid-cell mismatch (Sponza columns, vegetation). Magnitude unknown — depends on how much of the Sponza gap is attributable to lattice-vs-surface placement vs other topology mechanisms. Source: critic/07 I5 partial-acceptance.

None of these three mechanisms have an estimated magnitude bound at Stage 0 closeout. **M2 Stage 0's first job is to produce those estimates before any M2 implementation begins**, so the M2 budget is not committed against unverified mechanism leverage. This precondition prevents M2 from entering as an under-scoped 3-6-session commitment — M2 budget assumes mechanism-level priors exist.

**Metric-semantics note (I3).** Path B's irradiance estimator is bake-time: the consumer fetches pre-integrated `L·cos(θ)·ΔΩ` from a cubemap rather than running a hemispheric Riemann sum on raw radiance. Both estimators target the same physical quantity (irradiance), so the M0 PT reference remains the comparison anchor and "ratio" stays comparable in expectation. However, the per-pixel **error distribution shape** shifts — Path A errors are dominated by under-sampled bin variance; Path B errors are dominated by interpolation artifacts and probe-placement granularity. Consequence: M2 metrics like bright% and |p95| are not drop-in numerically comparable to M0 numbers as "same distribution moved down" — treat M2 numbers as absolute quality measurements against PT, not as delta-from-M0 in distribution-shape terms. The numeric bands themselves (|p95| ≤ 0.50, bright% ≤ 5%) stay because they are absolute targets, not differential.

**Goal:** switch probe storage from volumetric 3D grid to surface-attached cubemaps; port Delta #5 + any deltas DEAD'd in M1 due to topology.

**Work (sketched; detailed scope in a separate doc at M2 entry):**
1. Probe-placement pass (mesh-aware): scatter probes on triangle surfaces; persist `gTan/gBit/gNor/gPos`.
2. Atlas layout: replace `uDirectionalAtlas` 3D texture with per-surface cubemap array (or texture-2D-array with octahedral encoding).
3. Bake rewrite: hemisphere-only sampling above `gNor`; pre-integrate `L · cos(θ) · ΔΩ`.
4. Consumer rewrite: cubemap fetch replaces hemispheric Riemann sum (the consumer of v2.0-postfix is decommissioned — the consumer-side contract `(4/D²) · Σ L cos⁺` no longer applies because the integral has moved to bake).
5. Cascade hierarchy: confirm 1:4 spacing translates; merge between cascades happens at bake time as before.

**Pre-committed gates** (numeric bands inherit from M1 cumulative — must hold on both cornell AND sponza — but reference both M0 baseline AND M1 final):

| Verdict | Bands | Action |
|---------|-------|--------|
| **M2_CLOSES_GAP** | M1_CLOSES_GAP bands hit on both scenes after the topology switch | Retire entire current bake chain (`radiance_3d.comp` archived not deleted; flag stays to revert). Proceed to M3. |
| **M2_PARTIAL**    | Improvement over M1 final but does not hit M2_CLOSES_GAP | Decision: M2_ITERATION (one debug cycle, see below) or accept Path B incomplete and declare pivot failure. |
| **M2_ITERATION**  | Sub-verdict of M2_PARTIAL — granted only when failure mode is identifiable (probe-placement tuning needed, atlas layout artifact, merge-logic bug discovered during impl) | One 1–2 session debug cycle permitted, **constrained to:** probe-placement tuning, atlas layout adjustment, merge-logic debugging within the surface-attached topology. No new architectural axes (no new deltas, no consumer rewrites beyond Path B's planned cubemap fetch). After iteration, re-evaluate at M2_CLOSES_GAP/M2_PARTIAL/M2_DEAD. |
| **M2_DEAD**       | No improvement over M1 final, OR M2_PARTIAL + M2_ITERATION fails | Pivot terminated. Emergency fallback is keep v2.0-postfix Default + hybrid permanently. Revert all topology code on a side branch. |

### M3 — Hybrid retirement (~1 session) — STRICT CRITERION

**Trigger:** M1_CLOSES_GAP or M2_CLOSES_GAP. **M1_PARTIAL/M2_PARTIAL does NOT trigger M3 under the strict 2026-05-26 lock.**

**Goal:** retire hybrid per-pixel correction. Keep CLI flag for diagnostic comparison but turn off by default.

**Strict retirement criterion (LOCKED 2026-05-26):** hybrid may only be retired when the pivot pipeline achieves **|p95| ≤ 0.50 on both cornell AND sponza**. No wider-band fallback. If neither M1 nor M2 hits this bar, hybrid stays ON by default permanently and v3 is judged a failure of the retirement goal (even if it improved cascade quality).

**Work:**
1. Confirm pivot pipeline holds |p95| ≤ 0.50 on cornell/cam0 AND sponza/atrium at N=2048. If either scene fails, STOP — do not flip the default.
2. Run A/B at both scenes: pivot pipeline vs pivot+hybrid. If hybrid still adds measurable benefit (|p95| improvement > 5%) on either scene, do NOT retire — flag as "M3 deferred, pivot not yet sufficient."
3. Flip default `useHybrid = false`. Document in CLAUDE.md / cerebrum.
4. Sponza visual smoke test (the hybrid is load-bearing for Sponza atrium GI per the v2.x program's observation that the cascade-only pipeline under-bounces in the atrium space). If Sponza regresses visually despite metrics passing, revert M3 and document the metrics/visual divergence.

**Acceptance:** hybrid OFF by default; both scenes hold |p95| ≤ 0.50 with hybrid OFF; Sponza visual unchanged or improved; CLI revert path documented.

**Failure path:** if strict criterion is not met after M2, hybrid is permanent. v3 ships the pivot pipeline as Default with hybrid still ON; the v3 deliverable is then "improved cascade quality, hybrid kept as safety net." Do not relax the criterion mid-program.

**Cost of permanent hybrid retention (I12 acknowledgment).** "Kept as safety net" is not free. The ongoing cost includes:
- Every future pipeline change (Path A deltas already landed, any future cascade work, hybrid bug fixes) must validate against both cascade-only and cascade+hybrid configurations.
- Debugging visual regressions requires dual-path bisection: which path produced the artifact?
- Future scene additions or feature work (animated lights, new materials) must be validated against both paths or the dual-path validity guarantee weakens silently.
- Build/test matrix permanently includes a "hybrid-ON" axis.

This cost is **accepted as-is under the strict |p95| ≤ 0.50 retirement criterion** (§7 lock). It is **not** grounds for relaxing the criterion mid-program. The 2026-05-26 user lock explicitly chose strict-criterion + permanent-hybrid-if-fails over wider-band + retire-anyway.

---

## 4. Rollback criteria (apply at any milestone)

- **Any delta lands DEAD AND breaks an existing baseline** (e.g., Sponza visual regression, cornell ratio shifts > 0.15): revert that delta's commit, log to buglog, do not proceed until cause understood.
- **M1 cumulative DEAD**: do not start M2 without explicit user re-confirmation. Path A failure is informative (means the four deltas don't matter much in our topology) but not a mandate to commit to a 3-6 session rewrite.
- **M2 lands DEAD**: revert all topology code on a side branch; ship v2.0-postfix Default + hybrid as permanent. The pivot is judged terminated.
- **Sponza atrium GI regresses visually at any milestone**: pause that milestone, capture the regression, decide before proceeding.

---

## 5. Forbidden actions (DNR carry-over from v2.x failure-learnings)

These remain in force throughout the v3 pivot:

1. No more named hypothesis trees on the *current* cascade implementation. (If a v3 hypothesis emerges, it must be against the new pipeline.)
2. No more output-side symptom clamps.
3. No more bake-bin resolution bumps as "fix" attempts.
4. No more merge-formula reshapes targeting bright-tail isolation.
5. No more LDR-only verdicts — EXR or it didn't happen.
6. No more consume-side fix attempts inside v2.0-postfix's `sampleProbeDir` (Path A leaves the consumer alone; Path B replaces it wholesale).
7. No more P2-dominant-bin-driven fix work.

Adds for v3 specifically:

8. **No "land the whole port in one commit."** Each delta gets its own A/B + impl doc + gate. The failure-learnings doc was built on the discipline of *one mechanism at a time*; the pivot must keep that discipline even though the destination is known.
9. **No skipping M0.** The baseline file must be on disk before any delta lands, even if "we already have those captures." The pivot's whole point is bookkeeping the closure of the gap — that requires a frozen pre-pivot snapshot. **M0 Stage 0 pre-work** (deliverables A/B/C/C+) must also complete before M1; per-delta impl docs reference Stage 0 outputs.

**Exemption clauses (added 2026-05-26 in response to review I10):**

- **Path A deltas (#3, #4, #6, #7) are exempt from DNR #6** by construction. DNR #6 forbids "consume-side fix attempts inside v2.0-postfix's `sampleProbeDir`" because such attempts in the v2.x program were unprincipled patches absent a reference algorithm. Path A deltas port specific, named mechanisms from the [shader_toy/](../../shader_toy/) reference — they are *not* "new consume-side fixes invented within v2.0-postfix." Each delta's impl doc must cite the ShaderToy source location (per §2.1) it mirrors; the citation discharges the DNR check. A delta impl doc without a §2.1 citation does NOT clear the exemption and is subject to DNR #6.

---

## 6. Reusable infrastructure to carry into v3

- **CV1 measurement harness** (`tools/v20_convergence/`) — works as-is for cascade vs PT diffs. M0 just re-runs it.
- **Per-cascade contribution isolation** (`--max-cascade-level=N`, `tools/v25_axisA/`) — keep as diagnostic; Path A's merge-side deltas (#3) will benefit from running this sweep post-fix to verify the C2 over-contribution closes.
- **EXR triplet capture** (mode 17: cascade_gi + pt_full + pt_direct) — universal A/B substrate.
- **`--indirect-clamp-k=K` (v2.4.b debug flag)** — keep; useful as a sanity probe in v3 to verify the pivot doesn't re-introduce the same symptom.
- **GI presets / scene config** — carry-over without changes.
- **Hybrid correction shaders** — keep wired through M2; retire only at M3.

---

## 7. User decisions (RESOLVED 2026-05-26 — locked before M0)

1. **Pivot direction:** **Path A → conditional Path B.** Port portable deltas in current volumetric topology first; commit to topology rewrite only if M1 returns PARTIAL/DEAD. (§1 updated.)
2. **Sponza scope:** **First-class from M0.** Every milestone gate evaluates both cornell/cam0 and sponza/atrium. M0 must produce a Sponza measurement harness as reusable infra. Sponza-veto applies to every delta. (§3 M0 + M1 updated.)
3. **Hybrid retirement criterion:** **Strict — retire only if pivot achieves |p95| ≤ 0.50 on both scenes.** No wider-band fallback. If neither M1 nor M2 hits the bar, hybrid stays ON permanently and v3 is judged a failure of the retirement goal. (§3 M3 updated.)

These three decisions are locked. Any later relaxation must be explicit and documented as a scope amendment, not absorbed silently.

---

## 8. Cross-references

**On disk:**
- Failure learnings: [v25_z_mbrc_correction_failure_learnings.md](v25_z_mbrc_correction_failure_learnings.md)
- ShaderToy in-tree source: [shader_toy/CubeA.glsl](../../shader_toy/CubeA.glsl), [shader_toy/Image.glsl](../../shader_toy/Image.glsl), [shader_toy/Common.glsl](../../shader_toy/Common.glsl)
- Critique that produced this revision: [critic/06_v3_shadertoy_adoption_scope_review.md](critic/06_v3_shadertoy_adoption_scope_review.md)
- Reply to critique: [critic/06_v3_shadertoy_adoption_scope_reply.md](critic/06_v3_shadertoy_adoption_scope_reply.md)

**M0 Stage 0 outputs (shipped 2026-05-26):**
- [v20_shadertoy_diff_impl.md](v20_shadertoy_diff_impl.md) — Deliverable A (per-delta diff doc).
- [tools/v3_baseline/delta5_ceiling_estimate.md](../../tools/v3_baseline/delta5_ceiling_estimate.md) — Deliverable B (Path A ceiling, < 3%).
- [tools/v3_baseline/delta7_offset_audit.md](../../tools/v3_baseline/delta7_offset_audit.md) — Deliverable C (#7 CONFORMANT).
- [tools/v3_baseline/delta3_alpha_audit.md](../../tools/v3_baseline/delta3_alpha_audit.md) — Deliverable C+ (#3 Case B redefinition).
- [v3_m0_stage0_plan.md](v3_m0_stage0_plan.md), [v3_m0_stage0_impl.md](v3_m0_stage0_impl.md) — plan + impl summary.
- [v3_m0_stage0_closeout_plan.md](v3_m0_stage0_closeout_plan.md), [v3_m0_stage0_closeout_impl.md](v3_m0_stage0_closeout_impl.md) — closeout (scope-doc patches P1-P8).
- [critic/07_v20_shadertoy_diff_impl_review.md](critic/07_v20_shadertoy_diff_impl_review.md), [critic/07_v20_shadertoy_diff_impl_reply.md](critic/07_v20_shadertoy_diff_impl_reply.md) — review + reply.

**Previously cited but never written (removed):**
- `v20_shadertoy_diff_diagrams.md` — not on disk, no plan to write. Inline ASCII or per-delta impl docs cover the diagrammatic need.
- `v20_postfix_cv1_impl.md` — not on disk; v2.0-postfix consumer contract is documented inline in [v25_z_mbrc_correction_failure_learnings.md](v25_z_mbrc_correction_failure_learnings.md) §"Deltas #1+#2 landed" section.
- `engine_default_validation_impl.md` — not on disk; the Sponza-hybrid-load-bearing finding it referenced is now documented inline in §3 M0/M3 of this scope doc.

## 9. Execution log

(append-only, populated as milestones run)
