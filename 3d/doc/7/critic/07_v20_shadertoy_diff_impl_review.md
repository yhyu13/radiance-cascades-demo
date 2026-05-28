# Critique: v20_shadertoy_diff_impl.md

**Reviewer:** Kilo (automated)
**Date:** 2026-05-26T16:15+08:00
**Target:** `doc/7/v20_shadertoy_diff_impl.md`

---

## Structural Strengths

1. **Per-entry structure is consistent and useful.** ShaderToy code paste → current-impl code paste → semantic diff paragraph → topology dependency tag → port disposition. This 5-field template makes each delta self-contained and prevents re-derivation across sessions.

2. **Topology tag legend (✓ / ⚠ / ✗) is a good dimension.** It cleanly separates "port this literally" from "port the concept but redefine" from "can't port at all," which is the most important structural question for the A→B pivot.

3. **Delta #7 correctly resolved as CONFORMANT.** The audit verified 14 sites; v20 records the verdict and removes #7 from M1. This is exactly the right response to an audit finding — don't waste a gate cycle on a no-op.

4. **Delta #3's scope framing correction is well-documented.** v20 explicitly marks the original "skip rgb when α=0" framing as wrong, identifies the α convention inversion, and points to the C+ audit for the volumetric analog. This is the kind of pre-implementation correction that prevents a wasted session.

5. **Delta #5's ceiling estimate is referenced correctly.** v20 notes "#5 itself has small magnitude leverage; #5's value is the topology change that makes it tractable" — consistent with the <3% algebraic bound.

6. **All code references verified line-by-line.** Every ShaderToy citation (CubeA.glsl lines) and every current-impl citation (radiance_3d.comp, raymarch.frag lines) matches the actual source files. No factual errors found.

---

## Issues

### I1. Delta #3 volumetric analog formulation is missing

v20 identifies the structural gap precisely: ShaderToy uses 4-corner bilinear with per-corner `.xyz/.w` gating + `.w`-renormalization; current uses 8-corner trilinear with scalar `aFactor` uniform attenuation on a full `.rgb` sum. The topology tag says "⚠ portable-with-redefinition" and notes that "the trilinear merge in volumetric has 8 corners (not 4) and the .w renormalization is over a trilinear denominator rather than bilinear."

But v20 **does not define the volumetric analog formulation**. An implementer needs to know: what does per-corner gating look like in 3D? The trilinear renormalization formula for `.w` is different from bilinear (8 corners with 3 blend axes vs 4 corners with 2). The C+ audit presumably contains this derivation, but v20 just references it without summarizing the formulation.

**Risk:** Each M1 impl doc must re-derive the 8-corner trilinear renormalization from scratch rather than referencing a canonical definition in v20. Two sessions (or two engineers) could derive different formulations, producing inconsistent implementations.

**Recommendation:** Add a "Volumetric analog sketch" subsection to Delta #3 with the following content:
- The 8-corner trilinear `.w`-renormalization formula: `result.xyz = Σ(w_i · corner_i.xyz) / Σ(w_i)` where `w_i = 0` for rejected corners and `w_i = triBlendWeight_i` for visible corners.
- Note that this requires `sampleUpperDirTrilinear` to return per-corner `.xyz` and `.w` as separate values (currently returns a single blended `vec4`), or to be replaced by a per-corner-gated variant.
- Identify the two code sites that must change: `sampleUpperDirTrilinear` (lines 229-266) and the merge call site (lines 660-687).

### I2. #3/#6 bundling recommendation contradicts per-delta isolation discipline

v20 recommends "bundle #6 with #3 in the same impl doc; A/B both ON/OFF combinations (#3 alone, #6 alone, both)." This creates a 2×2 A/B matrix:

| Condition | #3 state | #6 state |
|-----------|----------|----------|
| Baseline  | OFF      | OFF      |
| #3 alone  | ON       | OFF      |
| #6 alone  | OFF      | ON       |
| Both      | ON       | ON       |

This requires 4 capture configs per scene per N value, doubling the capture workload vs sequential per-delta gates. It also complicates the verdict logic: which condition's result determines #3's gate? #6's gate? The scope's rule #8 says "each delta gets its own A/B + impl doc + gate," and a 2×2 matrix produces two deltas' gates from one impl doc.

**Risk:** If #3 ON + #6 OFF is STRONG but #3 OFF + #6 ON is also STRONG, both deltas get landed — but their individual attribution is contaminated by the combinatorial design. If #3 ON + #6 OFF is MARGINAL and #3 ON + #6 ON is STRONG, #6 gets credited for closing the gap that #3 alone couldn't, but #6's contribution is only measurable when #3 is present.

**Recommendation:** Acknowledge the trade-off explicitly. State: "Bundling #3/#6 trades per-delta isolation (scope rule #8) for shared A/B harness efficiency and controlled contamination. The 2×2 matrix is justified because #6's cone-derivation change is unlikely to produce meaningful results without #3's per-corner gating in place (v20 §Delta #6: 'without #3, a wider cone just smears MORE radiance through the uniform aFactor'). If the 2×2 matrix shows #6 alone is DEAD, #6 is dropped regardless of #3's outcome."

### I3. M1 work order reordering contaminates Delta #4's A/B baseline

v20 reorders from the scope's {#3, #4, #6} to {#3, #6, #4}, justified as "#6 follows #3 to share impl doc and A/B harness." Under the original order, #4's A/B baseline includes only #3's changes (plus #7 which is conformant). Under v20's reordered sequence, #4's A/B baseline includes both #3 AND #6's changes. This means #4's attribution is harder: if #4 produces MARGINAL, is that because #4 is a weak lever, or because #3+#6 already absorbed most of the available improvement?

**Risk:** #4 is the MB-feedback formulation change. Under the original ordering, a MARGINAL #4 after a STRONG #3 would clearly indicate #4 is a weak lever. Under the reordered sequence, a MARGINAL #4 after STRONG #3 + STRONG #6 could mean #4 is weak OR the gap is nearly closed by #3+#6 and #4 can't add much on top. The verdict interpretation differs.

**Recommendation:** If #3 and #6 both land STRONG, #4's A/B should be evaluated against a stricter baseline: "if #3+#6 cumulative already reduced bright% to <7% and |p95| to <0.60, #4's MARGINAL verdict is expected and should not be interpreted as 'MB formulation doesn't matter' — it's just that the remaining gap is small." Add this as a conditional note to the M1 work order table.

### I4. Delta #4 is a comparative experiment, not a definitive port

v20 honestly states: "the port shape is 'test whether deterministic-N-sample replaces stochastic-1-sample favorably' — not a literal 'do what ShaderToy does.'" This is candid, but the document's overall framing ("per-delta source-of-truth for the 7 ShaderToy→current deltas") implies each delta has a definitive port target — something the current impl is missing that ShaderToy has. #4 doesn't fit this pattern: the current impl already has a MB formulation (MC); the question is whether ShaderToy's alternative formulation (4-tap) is better.

**Risk:** The A/B for #4 measures "which formulation is better" rather than "does the missing mechanism close the gap." This is a different kind of verdict. A MARGINAL #4 where MC wins over 4-tap would be "ShaderToy's formulation is worse for our pipeline" — not "Delta #4 is unportable." The gate bands (STRONG/MARGINAL/DEAD) were designed for "does the missing mechanism help," not "which alternative is better."

**Recommendation:** Add a note to Delta #4's port disposition: "This delta's A/B is formulation-comparative, not mechanism-additive. The STRONG/MARGINAL/DEAD bands apply to the deterministic-N-sample mode relative to baseline (MC only). If deterministic wins STRONG → replace MC default. If MC wins → keep MC default, remove the flag, #4 is 'verified-equivalent-or-better' not 'ported.'"

### I5. Delta #5 strategic implication is undersynthesized

v20 states: "M0 Stage 0 Deliverable B (algebraic estimate) predicts #5 itself has small magnitude leverage; #5's value is the topology change that makes it tractable." The ceiling estimate confirms <3% magnitude for #5, and further states: "Path B closes geometry only — not the magnitude gap."

But v20 doesn't synthesize the strategic question: **if #5 is <3% and the topology alone doesn't close the magnitude gap, what mechanism under Path B actually closes the bright tail?** The ceiling estimate says "Delta #3's per-corner gating + Delta #4's MB-feedback formulation are the higher-leverage candidates." But these are Path A deltas. Under Path B, the question becomes: does per-corner gating work *better* under surface-attached topology (where hemisphere sampling eliminates grazing-angle artifacts)? Or does the topology switch introduce new mechanisms (hemisphere-only sampling, fewer ill-conditioned rays) that close the gap independently?

**Risk:** If M1 returns M1_PARTIAL_MAGNITUDE (all deltas are partial levers on both scenes), the scope's decision logic says "check §1.1 Delta #5 estimate: if #5 ≪ remaining gap → Path B necessary BUT NOT sufficient." v20 references this logic but doesn't identify what would make Path B sufficient beyond #5. An engineer evaluating M1_PARTIAL_MAGNITUDE would need to know: what's the actual Path B lever, and is it the topology change itself or the combination of topology + #3 working better under surface-attached?

**Recommendation:** Add a "Path B leverage analysis" subsection (or a forward reference to a separate doc) that identifies the specific mechanisms Path B introduces beyond #5:
- Hemisphere-only sampling above gNor → eliminates grazing-angle bins that produce low-cosine but high-energy artifacts in the volumetric full-sphere bake.
- Per-corner gating under surface-attached → rejected corners in hemisphere sampling have a clearer geometric meaning (ray hit own surface → reject) vs volumetric (ray hit nothing → semantics unclear).
- Surface-aware probe placement → probes concentrated on surfaces where GI matters, reducing wasted volume-probes in empty space.

Each mechanism should have a rough magnitude estimate (even "unknown, hypothesized from ShaderToy reference behavior") so M1_PARTIAL evaluation can reason about whether Path B is likely to close the remaining gap.

### I6. Only Case B outcome documented for Delta #3; Cases A and C not referenced

The M0 Stage 0 plan lists three cases for the C+ audit:
- **Case A:** `upperDir.rgb` is already zero when α=0 → #3 is a no-op, skip from M1.
- **Case B:** `upperDir.rgb` has semantic content (sky/miss) → #3 requires volumetric analog definition before code change.
- **Case C:** `upperDir.rgb` is nonzero garbage → #3 ports naively as "skip rgb on α=0."

v20 only documents Case B's outcome. While Case B is what the audit actually found, v20 should at least reference the other cases for completeness — an engineer reading v20 without the M0 plan wouldn't know why Case B was the relevant outcome, or what would have happened if A or C had been found.

**Recommendation:** Add a brief "Audit context" note to Delta #3: "The C+ audit evaluated three cases (A: already zero → no-op, B: semantic mismatch → redefinition needed, C: garbage → naive port). Case B was found: α conventions are inverted between implementations. Under Case A, #3 would have been dropped from M1; under Case C, #3 would have been a single-line conditional branch. Case B is the most complex outcome, requiring structural per-corner gating rather than a threshold test."

### I7. Topology tag legend doesn't capture sequencing dependencies

The ✓/⚠/✗ tags capture portability (whether the concept transfers to volumetric topology). But Delta #6 has a sequencing dependency ("conditional on #3 landing first") that's a different dimension. The tag ⚠ means "portable-with-redefinition" but #6's dependency on #3 is about **ordering**, not about **redefinition**. These should be tracked separately.

**Risk:** A reader scanning the status snapshot table sees ⚠ for both #3 and #6, but #3 is "portable-with-redefinition" (structural change needed) while #6 is "portable-with-redefinition + conditional on #3" (structural change needed AND can't be tested independently). The same tag obscures a critical difference.

**Recommendation:** Add a sequencing column to the status snapshot table, or extend the topology tag legend with a sequencing notation like "[→#3]" meaning "must follow #3." Example: Delta #6's topology tag becomes "⚠ [→#3]" — portable-with-redefinition, must follow Delta #3.

### I8. Delta #6 semantic diff is overly dense

The semantic diff paragraph for #6 is ~15 lines mixing:
- Numerical comparison (sin(theta) ≈ 0.92 vs 0.248)
- Structural comparison (geometric probe-size cone vs discretization bin-width cone)
- Topology concern ("Whether this is correct in volumetric is unclear")
- Conditional dependency ("without #3, a wider cone just smears MORE radiance")
- Implementation scope ("cone-derivation refactor + tuning sweep, not a single-line code change")

These are distinct information types that should be separated for clarity.

**Recommendation:** Split #6's semantic diff into three subsections:
- **Structural diff:** ShaderToy's cone is geometric (probe-size-derived); current's cone is discretization-derived (bin-width-derived).
- **Numerical diff:** ShaderToy ≈ 0.92 at probeSize=4; current ≈ 0.25 at D=8. ~4× difference.
- **Port considerations:** Volumetric correctness unclear; conditional on #3; not a single-line fix (5-7 day tuning sweep per phase3_plan §3.6).

### I9. Delta #6's "correctness unclear" creates an evaluation problem

v20 states: "Whether this is correct in volumetric is unclear." This is honest, but it creates a problem for the A/B evaluation: the gate bands measure whether the change produces better metrics, not whether it's geometrically correct. A wider cone (moving toward ShaderToy's 0.92) could reduce bright% by smearing energy across more probes (statistically better per the metric bands) while being geometrically incorrect (the cone should match the actual angular extent of the lower probe, not an arbitrary wider value).

**Risk:** A STRONG verdict for #6 based on metric improvement could mask a geometrically incorrect implementation. This is the same class of problem as the v2.x "output-side symptom clamp" — metric improvement via a mechanism that's not principled.

**Recommendation:** Add a correctness criterion to #6's gate: "STRONG requires not only metric improvement but also a principled geometric justification for the chosen cone size. 'The cone matches the volumetric cell's apparent angular extent' is principled; 'we widened it until metrics improved' is not." This echoes DNR #4's "no more merge-formula reshapes targeting bright-tail isolation" — the mechanism must be correct, not just effective.

### I10. Delta #4 post-A/B cleanup not addressed

v20 recommends: "keep MC formulation, add an optional N-tap deterministic mode behind a flag, A/B at M1." But what happens after the A/B?

- If deterministic wins STRONG → does MC get removed? Or does both modes stay permanently?
- If MC wins → does the flag get removed? Or does it persist as a debug option?
- If MARGINAL → does both modes stay? What's the maintenance cost of a permanent dual-mode?

The scope's DNR list doesn't address this, and v20's recommendation is open-ended.

**Recommendation:** Add a post-A/B disposition to Delta #4: "If deterministic wins STRONG → MC mode removed after one session of cleanup (flag deleted, `sampleC0AtlasStochastic` simplified). If MC wins → flag removed, deterministic mode deleted. If MARGINAL → both modes kept for one additional session of investigation; if no resolution, keep whichever mode has cleaner code (likely MC, since it's already the default)."

### I11. Delta #5 ceiling estimate's "both evaluate cosine at the same point" insight is underemphasized

The ceiling estimate contains a key mathematical insight: "both formulations evaluate cosine at the SAME point (bin center)" — meaning the discretization error is bounded by O(Δθ²) for both, and the numerical difference between bake-time and consume-time cosine is a uniform scale shift, not a shape change. v20 mentions "both are O(angular_width²)-bounded discretizations" but doesn't explicitly state the same-point evaluation finding, which is the stronger claim.

**Risk:** An engineer might assume bake-time cosine (Delta #5) provides a fundamentally different weighting distribution than consume-time cosine. The same-point evaluation shows this assumption is wrong — the distributions are nearly identical up to a <3% uniform scale. This insight directly supports the "skip #5 in Path A" decision and should be front-and-center.

**Recommendation:** Add a one-line mathematical note to Delta #5: "Key insight: both formulations evaluate cos(θ) at the bin center θ_i. The discretization error is therefore O(Δθ²) for both, and the numerical difference is a <3% uniform scale shift — not a distribution shape change. This is why #5 is 'small magnitude leverage' regardless of topology."

### I12. Status snapshot table's "M1 work" column is inconsistent with M1 work order table

The status snapshot (§0) lists Delta #6 as "⚠ | M1 (conditional on #3)" and Delta #3 as "⚠ | M1 (redefined; see C+ audit)." But the M1 work order table (§Summary) lists {#3, #6, #4} with #6 as "Conditional on #3 outcome. Bundle in same impl doc as #3." The snapshot says "conditional on #3" but doesn't mention the bundling recommendation. The work order table says "bundle" but doesn't mention the 2×2 A/B matrix implications.

**Recommendation:** Make the two tables consistent. Either the snapshot should note "bundle with #3 in same impl doc" or the work order table should note the isolation trade-off (I2). Currently, each table has information the other doesn't.

---

## Summary Table

| Issue | Severity | Section | Action Required |
|-------|----------|---------|-----------------|
| I1. Delta #3 volumetric analog missing | HIGH | Delta #3 | Add volumetric analog sketch subsection |
| I2. #3/#6 bundling vs isolation trade-off | MEDIUM | Delta #6 / Summary | Acknowledge trade-off explicitly in both tables |
| I3. #4 baseline contamination from reorder | MEDIUM | Summary table | Add conditional note for #4's verdict interpretation |
| I4. #4 is comparative experiment, not port | MEDIUM | Delta #4 | Add formulation-comparative note to port disposition |
| I5. Path B leverage undersynthesized | HIGH | Delta #5 | Add Path B leverage analysis (mechanisms beyond #5) |
| I6. Delta #3 Cases A/C not referenced | LOW | Delta #3 | Add audit context note |
| I7. Topology tag lacks sequencing dimension | LOW | §0 / Summary | Add sequencing notation to tag legend |
| I8. Delta #6 semantic diff too dense | LOW | Delta #6 | Split into structural/numerical/port subsections |
| I9. #6 correctness vs metric effectiveness | MEDIUM | Delta #6 | Add principled-justification criterion to gate |
| I10. #4 post-A/B cleanup not addressed | LOW | Delta #4 | Add post-A/B disposition (cleanup plan) |
| I11. #5 same-point evaluation insight underemphasized | LOW | Delta #5 | Add one-line mathematical note |
| I12. Status snapshot vs work order inconsistency | LOW | §0 / Summary | Synchronize bundling info across both tables |

---

## Overall Assessment

v20 is factually accurate — all code references verified, all claims confirmed against source. Its primary weakness is **insufficient specification for implementation**: Delta #3's volumetric analog is referenced but not defined; Delta #6's dense semantic diff needs structural separation; and the Path B strategic picture (what actually closes the gap beyond #5) is missing. The #3/#6 bundling recommendation is pragmatic but creates isolation and attribution trade-offs that aren't acknowledged. These are specification-depth issues, not correctness issues — the document's claims are right, but an implementer would need to read 3-4 additional documents (C+ audit, ceiling estimate, phase3_plan, scope) to derive the actual implementation plan for each delta. v20's value as a "single source-of-truth" is undermined by these forward references without summaries.