# Critique: v3_shadertoy_adoption_scope.md

**Reviewer:** Kilo (automated)
**Date:** 2026-05-26T11:21+08:00
**Target:** `doc/7/v3_shadertoy_adoption_scope.md`

---

## Structural Strengths

1. **Disciplined gate framework.** Pre-committed verdict bands (STRONG/MARGINAL/DEAD) with numeric thresholds, Sponza veto, and rollback criteria are excellent engineering hygiene. The v2.x failure-learnings clearly informed these safeguards.
2. **Path A → conditional B ordering.** The forcing-function rationale is sound: doing A first is strictly cheaper in information-theoretic terms than jumping straight to B. "Doing A → conditional B is strictly cheaper than going straight to B and discovering A would have sufficed" is the correct framing.
3. **Forbidden actions carry-over.** The 9-item DNR list is hard-won wisdom correctly preserved. Items #8 (per-delta isolation) and #9 (no skipping M0) are well-chosen additions specific to the pivot context.
4. **Per-delta isolation discipline.** Rule #8 prevents the most likely failure mode for a "known destination" pivot — the temptation to land everything at once because the target algorithm is already validated elsewhere.
5. **Sponza as first-class.** Elevating Sponza from afterthought to veto-capable scene is the correct response to the v2.5 finding that hybrid was load-bearing for Sponza atrium GI.

---

## Issues

### I1. Three cross-referenced documents don't exist

§8 lists `v20_shadertoy_diff_impl.md`, `v20_shadertoy_diff_diagrams.md`, and `engine_default_validation_impl.md` — none are on disk. A "locked" scope document referencing nonexistent canonical sources is a credibility gap. The delta definitions in §2 are underspecified as a result; Delta #3 ("skip rgb feed when α=0") is one sentence for what may be a nuanced volumetric-vs-surface-attached merge difference.

**Risk:** Each delta's impl doc will have to re-derive the algorithmic diff from ShaderToy source rather than referencing a canonical analysis document. This is error-prone and defeats the "one mechanism at a time" discipline if two engineers (or two sessions) interpret the same delta differently.

**Recommendation:** Create or stub `v20_shadertoy_diff_impl.md` with per-delta algorithmic descriptions before M0 starts. Even a skeletal version (ShaderToy code location, current-impl code location, semantic diff) would suffice.

### I2. Path A ceiling is admitted but unquantified

The scope says "ceiling: unknown" for Path A. Delta #5 (bake-time cosine pre-weighting) is the single largest architectural incompatibility — it moves the Lambertian integral from consume-time to bake-time. The failure-learnings doc established that the bright tail is a structural bias, not variance, concentrated at the C1→C2 merge. Cosine pre-weighting changes how that merge accumulates energy. An analytical estimate of #5's magnitude (even rough: "in the ShaderToy reference, #5 reduces bright% by ~X pp") would make the A→B decision point more than a blind jump.

**Risk:** If Path A lands M1_PARTIAL with no analytical bound on #5's contribution, the decision to commit to Path B is based on "maybe #5 is big" rather than "we know #5 closes the remaining gap of Y pp." This is the exact kind of unquantified hypothesis the failure-learnings warned against.

**Recommendation:** Add a §1.1 or appendix that estimates #5's contribution. Method: compare ShaderToy reference output with #5 artificially disabled (hardcode `cos(θ)=1` in the bake) vs enabled, measure the bright%/|p95| delta. This is a ShaderToy-side experiment that can be done in <1 hour without touching the production pipeline.

### I3. Metric bands are calibrated against a consume-side pipeline

The acceptance bands (ratio, |p95|, bright%, dim%) are defined against the Riemann-sum consumption model `(4/D²)·Σ L cos⁺`. Under Path B, the consumer is a cubemap fetch with bake-time pre-integration. The same numeric bands may not be apples-to-apples comparable after the fundamental contract change.

**Example:** Path B pre-integrates `L·cos(θ)·ΔΩ` at bake time, so the consumer fetches irradiance directly. The "ratio" metric (cascade/PT) now measures a different quantity structurally — it's comparing pre-integrated irradiance against PT irradiance, rather than raw radiance sums against PT. The numbers may coincidentally be similar, but the comparison is semantically different.

**Risk:** Path B could achieve |p95| ≤ 0.50 by coincidence of metric interpretation rather than genuine convergence, or could fail the numeric bands despite being visually correct because the pre-integrated values have a different distribution shape.

**Recommendation:** Add an explicit note to M2 gates stating whether they reference M0 baselines (Riemann-sum pipeline) or whether Path B should re-baseline against its own M2-PT comparison. At minimum, the scope should acknowledge this semantic shift and define what "ratio" means under each topology.

### I4. M1_PARTIAL is two different conditions under one verdict

The M1_PARTIAL verdict is triggered by: "M1_CLOSES_GAP on cornell only, OR |p95| ≤ 0.70 on both scenes." These are radically different situations:

- **(a) Full closure on cornell, gap remains on Sponza** → suggests the deltas work for simple geometry but fail for complex occlusion, implying #5 (surface-aware cosine) is the blocker for Sponza specifically.
- **(b) Partial closure on both scenes** → suggests all four deltas are partial levers that collectively don't close enough, implying #5 may be large but not the sole remaining gap.

These should be split into **M1_PARTIAL_GEOMETRY** and **M1_PARTIAL_MAGNITUDE**, each with different Path B expectations:

- M1_PARTIAL_GEOMETRY → Path B is likely to close the gap (the problem is surface-awareness, which Path B provides).
- M1_PARTIAL_MAGNITUDE → Path B's ceiling is uncertain (the problem may not be #5 alone; other volumetric-specific issues may persist even after topology switch).

**Recommendation:** Split M1_PARTIAL into two sub-verdicts with differentiated Path B expectations.

### I5. Delta #7 should be audited in M0, not M1

The description says it "may overlap with existing code already using it (Phase 5d trilinear, Phase 5f bilinear)." If the offset is already applied consistently, #7 is a no-op that consumes a full gate cycle (impl doc, A/B capture, verdict evaluation). This is wasted effort within a milestone that's already budgeted at 2–3 sessions.

**Recommendation:** Audit the -0.5 offset consistency across all probe-sampling sites in M0 baseline lockdown. If already correct, skip #7 from M1 entirely and note it as "already conformant" in the baseline lock file. If inconsistent, it enters M1 with a specific list of sites to fix.

### I6. Delta #6 placement rationale is thin

Delta #6 is placed last because it's "smallest leverage suspect" with no supporting evidence. The failure-learnings localized the leak to C1→C2 merge geometry; #6 changes θ semantics in `WeightedSample` — this affects how the consumer interprets which bin's direction contributes to the irradiance integral. This is arguably a merge-side change (the merge's output is weighted by θ semantics) rather than a pure consumer-side change.

**Risk:** If #6 is actually a meaningful leverage point but is shipped last after three other deltas have already shifted the baseline, its A/B is contaminated by cumulative drift. The "smallest leverage suspect" label may be self-fulfilling — it gets tested in the worst conditions and produces MARGINAL, reinforcing the assumption.

**Recommendation:** Flag #6's ordering as "low-confidence, subject to re-ordering after M1 Delta #3 results." If #3 produces STRONG, #6 stays last. If #3 produces MARGINAL or DEAD, consider #6 earlier as the next hypothesis. Add this as a conditional note in the ordering.

### I7. M0 time budget is underestimated

The scope estimates "~1 h" for M0. The work includes:

- Re-running Cornell capture (fast, script exists)
- Creating Sponza capture script (new development)
- Verifying Sponza PT convergence at N=2048 (potentially expensive render)
- Extending metric harness to Sponza (code changes to the harness)
- Running 4 capture configs (2 Cornell + 2 Sponza)
- Populating `baseline_lock.json` with 4 entries + hashes

Sponza is a new scene with no existing infra. The Sponza capture script alone (forking from cornell-hardcoded `cv1_capture.ps1`) is likely 30–60 minutes of development. The PT convergence render at N=2048 on Sponza (a more complex scene) may take longer than Cornell. Total realistic estimate: 3–4 hours.

**Recommendation:** Re-estimate M0 at 3–4 hours. This doesn't change the milestone's structure but prevents time-budget surprise that could rush the Sponza baseline and produce unreliable numbers.

### I8. No fallback for Sponza PT convergence instability

M0's gate says "if PT reference convergence on Sponza is unstable at N=2048, that's the blocker to surface before M1 starts." But there's no remediation plan:

- What N is needed for Sponza PT convergence? (Cornell uses N=2048; Sponza is more complex.)
- If N=4096 or N=8192 is needed, does the infrastructure support it?
- If Sponza PT convergence requires a different camera (atrium vs. corridor), which is canonical?
- Can wider Sponza-specific bands be accepted temporarily while PT convergence is improved?

A blocker without a bypass plan means M0 can stall indefinitely.

**Recommendation:** Add a M0 contingency: if Sponza PT at N=2048 is unstable, escalate to N=4096 with a time budget. If still unstable, define wider provisional Sponza bands (e.g., |p95| ≤ 0.70 instead of 0.50) and document the provisional status. Do not allow an unbounded stall.

### I9. Path B is a single-shot attempt

If M2 lands MARGINAL (not STRONG, not DEAD), the fallback is "keep v2.0-postfix Default + hybrid permanently." But MARGINAL M2 means the topology switch *improved* things without closing the gap — the surface-attached pipeline might have implementation bugs, probe-placement tuning issues, or merge-logic errors that are fixable within the topology.

The scope treats Path B as a single pass: either it closes the gap or it's abandoned. This is inconsistent with the per-delta iteration discipline that Path A enjoys. Path B is a 3–6 session rewrite; a single MARGINAL result after that effort should allow at least one iteration cycle to debug the surface-attached pipeline before declaring pivot failure.

**Recommendation:** Add a M2_ITERATION verdict between M2_PARTIAL and M2_DEAD: if M2 improves metrics but doesn't close the gap, allow one 1–2 session iteration to fix identified issues within the surface-attached topology. If the iteration also fails to close the gap, then declare pivot failure.

### I10. Forbidden action #6 may conflict with Delta #6

The v2.x DNR list includes "No consume-side fix attempts inside v2.0-postfix's `sampleProbeDir`." But Delta #6 (WeightedSample θ-of-ray vs θ-of-bin semantics) changes how θ is interpreted in the consumer's direction-to-bin mapping, which is consumed via `sampleProbeDir`. The scope doesn't address whether Path A deltas are exempt from the v2.x DNR list.

**Risk:** An engineer (or a future session) implementing #6 might hesitate or refuse based on the DNR, creating a false conflict between the locked scope and the locked forbidden actions.

**Recommendation:** Add an explicit exemption clause to §5: "Path A deltas (#3, #4, #6, #7) are exempt from v2.x DNR #6 by construction — they port ShaderToy mechanisms into the current pipeline rather than inventing new consume-side fixes within the v2.0-postfix architecture."

### I11. Delta #3 porting may not be mechanism-equivalent

The scope describes #3 as "single-shader change: skip the rgb feed when α=0." But the ShaderToy reference's α=0 comes from rays hitting the surface the probe sits on (surface-attached topology: hemisphere above `gNor`). In the volumetric topology, α=0 means "ray didn't hit any geometry" — these are semantically different conditions:

- **ShaderToy α=0**: ray terminated at own surface → no incoming radiance from that direction → skip `.rgb`.
- **Volumetric α=0**: ray escaped the scene → no occluder hit → could mean "sky/infinite light from that direction" or "miss."

In volumetric topology, skipping `.rgb` when α=0 might discard valid sky contribution. The delta description conflates two different α=0 semantics.

**Risk:** Porting #3 naively (branch on α=0 → skip rgb) in volumetric topology could dim the scene by removing sky/ambient contribution from unoccluded rays, rather than removing self-surface leakage as intended.

**Recommendation:** Clarify Delta #3's α=0 semantics for volumetric topology. The correct volumetric analog is likely "skip `.rgb` when α=0 AND the ray's smoothstep merge produced no upper-cascade contribution" (i.e., the `.rgb` from the merge is garbage, not meaningful sky light). This requires checking the merge logic to confirm what `.rgb` carries when α=0 in the current bake — it may already be zero, making #3 a no-op. Audit this in M0 alongside #7.

### I12. Hybrid permanent retention has no maintenance cost estimate

If neither M1 nor M2 closes the gap, hybrid stays ON permanently. The scope frames this as acceptable ("v3 delivers improved cascade quality, hybrid kept as safety net") but doesn't address the ongoing cost:

- Hybrid shaders must be kept compatible with every pipeline change (both Path A deltas and potential Path B rewrite).
- The permanent dual-path (cascade + hybrid correction) increases debugging complexity — any visual bug requires determining which path is responsible.
- Future feature additions (e.g., animated lights, new scenes) must be validated against both paths.

**Recommendation:** Add a brief cost acknowledgment to §3 M3 failure path: "Permanent hybrid retention carries a maintenance cost: all pipeline changes must preserve hybrid compatibility, and debugging requires dual-path analysis. This cost is accepted as-is under the strict retirement criterion; it is not a reason to relax the criterion."

---

## Summary Table

| Issue | Severity | Section | Action Required |
|-------|----------|---------|-----------------|
| I1. Missing cross-references | HIGH | §8 | Create/stub `v20_shadertoy_diff_impl.md` before M0 |
| I2. Path A ceiling unquantified | HIGH | §1 | Estimate #5 magnitude via ShaderToy-side experiment |
| I3. Metric bands semantic shift under Path B | MEDIUM | §3 M2 | Explicitly define what "ratio" means per topology |
| I4. M1_PARTIAL ambiguity | MEDIUM | §3 M1 | Split into M1_PARTIAL_GEOMETRY + M1_PARTIAL_MAGNITUDE |
| I5. Delta #7 audit timing | LOW | §3 M1 | Audit in M0, skip from M1 if already conformant |
| I6. Delta #6 ordering rationale | LOW | §3 M1 | Flag as low-confidence, add conditional re-ordering note |
| I7. M0 time budget underestimated | MEDIUM | §3 M0 | Re-estimate at 3–4 hours |
| I8. No Sponza PT convergence fallback | MEDIUM | §3 M0 | Add contingency plan (higher N or provisional bands) |
| I9. Path B single-shot | MEDIUM | §3 M2 | Add M2_ITERATION verdict for marginal results |
| I10. DNR #6 conflicts with Delta #6 | LOW | §5 | Add explicit exemption clause for Path A deltas |
| I11. Delta #3 α=0 semantics | HIGH | §2 | Clarify volumetric vs surface-attached α=0 meaning; audit in M0 |
| I12. Hybrid permanent retention cost | LOW | §3 M3 | Add maintenance cost acknowledgment |

---

## Overall Assessment

The scope document is well-structured with strong gate discipline, clear rollback criteria, and appropriate forbidden-action carry-over. Its primary weaknesses are: (a) missing source documents that should be created before execution begins, (b) an unquantified ceiling for Path A that makes the A→B decision point a blind jump rather than an informed choice, and (c) semantic mismatches in two deltas (#3 and #5 analog) that arise from the volumetric-vs-surface-attached topology difference and aren't addressed in the brief delta descriptions. The gate framework itself is sound; the issues above are about the information density and precision needed to make the gates produce reliable verdicts.