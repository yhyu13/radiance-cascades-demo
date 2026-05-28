# M0 Stage 0 — Plan

**Date:** 2026-05-26.
**Predecessor:** [v3_shadertoy_adoption_scope.md §3 M0 Stage 0](v3_shadertoy_adoption_scope.md), [critic/06_v3_shadertoy_adoption_scope_reply.md](critic/06_v3_shadertoy_adoption_scope_reply.md).
**Goal:** complete the 4 pre-M0 deliverables (A/B/C/C+) so M0 Stage 1 (baseline captures) can begin and so M1 per-delta impl docs have a source-of-truth to reference.

This is the plan only. Self-critique follows in §6. Revisions in §7.

---

## 1. Inputs (known)

- **ShaderToy reference:** [shader_toy/Common.glsl](../../shader_toy/Common.glsl) (231 lines), [shader_toy/CubeA.glsl](../../shader_toy/CubeA.glsl) (239 lines), [shader_toy/Image.glsl](../../shader_toy/Image.glsl) (239 lines). Total ~700 lines GLSL source.
- **Saved ShaderToy webpage:** [shader_toy/Radiance Cascades 3D.mhtml](../../shader_toy/Radiance Cascades 3D.mhtml) (683 KB MHTML; original page at shadertoy.com).
- **Current-impl bake shader:** [res/shaders/radiance_3d.comp](../../res/shaders/radiance_3d.comp) (844 lines).
- **Current-impl consumer shader:** [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag) (line count tbd; contains probe sampling).
- **No runnable ShaderToy harness in tree.** The shader_toy/ files are read-only source; running them requires browser (mhtml or shadertoy.com) or a tiny GLFW driver.

## 2. Deliverable A — `v20_shadertoy_diff_impl.md` skeleton

**Goal:** produce a single source-of-truth document with per-delta entries that each M1 per-delta impl doc can reference instead of re-deriving the diff.

**Structure per delta:**
```
### Delta #N — <name>

**ShaderToy source:** shader_toy/<file>:<lines>
```glsl
<paste of the relevant block, ~5–20 lines>
```

**Current impl mirror:** res/shaders/<file>:<lines>
```glsl
<paste of the relevant block, ~5–20 lines>
```

**Semantic diff:** <1 paragraph: what the ShaderToy version does that the current doesn't, why the difference matters, what changes if we port it>

**Topology dependency:** ✓ portable to volumetric / ✗ requires surface-attached / requires audit
```

**Per-delta work order (cheapest first):**
1. #1, #2 — already LANDED; document as "already in v2.0-postfix consumer" with a 1-paragraph reference.
2. #7 — probe-position -0.5 offset. Plumbing only. Document with code locations.
3. #3 — smoothstep merge dead-α. Single-block ShaderToy → single-block current. With α=0 semantic caveat from §3 (cross-link to Deliverable C+ verdict).
4. #4 — multi-bounce 4-cube-read average. Two-block diff (ShaderToy `sampleAtlas`-style call site vs current `sampleC0AtlasStochastic`).
5. #6 — WeightedSample θ-of-ray vs θ-of-bin. The most subtle; involves bin direction reconstruction.
6. #5 — bake-time cosine + ΔΩ pre-weighting. Path B only; document as "no current analog" + reference architecture note.

**Time estimate:** ~60 min.

**Acceptance:** all 7 entries on disk with code paste + diff paragraph + topology tag.

## 3. Deliverable B — Path A ceiling estimate

**Goal:** numeric prior for Delta #5's leverage in the surface-attached topology, used at the M1 cumulative gate to distinguish M1_PARTIAL_GEOMETRY/MAGNITUDE.

**Critical clarification of I2's proposal.** The reviewer wrote "measure the bright%/|p95| delta" but those metrics are defined against a PT reference. The in-tree ShaderToy has no PT reference (it IS the cascade renderer). The measurement we can actually take is **R-on vs R-off delta** — i.e., pixel-difference statistics between ShaderToy with #5 active and ShaderToy with #5 ablated.

This gives a *relative* signal ("Delta #5 changes ShaderToy output by L1=X% / Linf=Y%") that bounds #5's leverage from above: if R-on ≈ R-off, #5 is small; if R-on diverges substantially, #5 is large. The number is not directly comparable to bright%/|p95|, but it informs whether committing to Path B for #5 alone is justified.

**Three execution options, ranked:**

- **B-empirical (preferred if feasible):** Open `shader_toy/Radiance Cascades 3D.mhtml` in a browser; capture screenshot of cornell-analog frame. Edit the local mhtml to hardcode `cos(θ) = 1` in CubeA's pre-integration (or use browser dev tools to patch the live shader); capture R-off screenshot. Compute L1/L∞ pixel delta with a Python script. Time: ~60 min if mhtml renders, ~∞ if it doesn't and we need to scaffold.
- **B-algebraic (fallback):** Derive #5's contribution analytically. Lambertian integral is `(1/π) ∫ L(ω) cos(θ) dω`. ShaderToy bakes `L · cos(θ) · ΔΩ` per bin → consumer integrates by sum. Current impl bakes `L` per bin + binary α → consumer multiplies by `cos⁺` at integration. The mathematical equivalence question reduces to: are the bake-time per-bin cos weights numerically equivalent to consume-time per-ray cos weights, modulo binning discretization? Answer with closed-form math + a worst-case-discretization bound. Time: ~30 min. Lower confidence but defensible — the failure mode is "we missed a non-obvious topology effect."
- **B-stub (last resort):** Document that the experiment was not run with explicit reason; mark §1.1 of scope doc with "estimate unknown, M1_PARTIAL_MAGNITUDE → Path B decision must consider this gap." Time: ~5 min.

**Plan:** attempt B-empirical first; if mhtml doesn't render or screenshot patching is impractical within ~30 min, fall back to B-algebraic. Do not pursue B-empirical past its budget.

**Acceptance:** §1.1 of v3 scope doc has a numeric or analytical estimate, with method tag (empirical/algebraic/stub).

## 4. Deliverable C — Probe-position -0.5 offset audit

**Goal:** determine whether Delta #7 needs to be an M1 work item or is already conformant.

**Work:**
1. Grep `radiance_3d.comp` for probe-position derivation (uniforms like `uProbeSpacing`, `uProbeOrigin`, computations involving `vec3(0.5)` or `-0.5`).
2. Grep `raymarch.frag` (consumer) for sampling sites: `texture(..., probePos)`, `textureLod(...)`, any trilinear/bilinear neighborhood lookups.
3. Grep any hybrid correction shaders for probe sampling.
4. Tabulate: site, file:line, offset used (-0.5 / +0.5 / 0 / other).
5. Verdict:
   - **Uniform**: all sites use the same offset → Delta #7 conformant, mark in `baseline_lock.json` and skip from M1.
   - **Non-uniform**: site list goes into M1 Delta #7 patch plan.

**Time estimate:** ~30 min.

**Acceptance:** audit table written to `tools/v3_baseline/delta7_offset_audit.md` with verdict.

## 5. Deliverable C+ — Delta #3 α=0 semantics audit

**Goal:** determine what `upperDir.rgb` contains when α=0 in the current bake; classifies whether Delta #3 ports naively or requires semantic redefinition.

**Work:**
1. Read `radiance_3d.comp` merge block — locate the smoothstep/blend lines that combine upper-cascade contribution.
2. Trace what `upperDir.rgb` is sourced from when α=0:
   - Is it explicitly zeroed (e.g., `if (alpha == 0) upperDir = vec4(0)`)?
   - Does it carry a sky/ambient term?
   - Does it carry uninitialized / debug / prior-frame garbage?
3. Classify into one of three cases (per scope §3 M0 Stage 0):
   - **Case A — Already zero:** Delta #3 is a no-op in volumetric. Skip from M1.
   - **Case B — Semantic content (sky/miss):** Delta #3 cannot port naively. M1 impl doc must define volumetric analog.
   - **Case C — Garbage:** Delta #3 ports naively. Proceed to M1 with "skip rgb on α=0" patch.
4. Compare against ShaderToy CubeA.glsl: confirm ShaderToy α=0 = "ray hit own surface" (the topological difference that motivated I11).

**Time estimate:** ~30 min.

**Acceptance:** verdict (A/B/C) written to `tools/v3_baseline/delta3_alpha_audit.md` with code citations.

## 6. Self-critique (Phase 1b)

### C1 — Deliverable order is wrong (HIGH)

A is listed first, but A's entries for Delta #3 and #7 depend on the verdicts produced by C+ and C respectively. Drafting A first means either:
- Stubbing those two entries with "TBD pending Stage 0 C/C+" → fragile, easy to forget to revisit.
- Or writing them based on the ShaderToy code alone without the current-impl verdict → defeats the point of A being a *diff* doc.

**Fix:** reorder execution to **C → C+ → A → B**. C and C+ are short and independent; their outputs feed A directly. B is independent of the rest.

### C2 — B-empirical budget is optimistic and lacks a fast-fail check (HIGH)

Opening an `.mhtml` in a modern browser may not restore WebGL state (mhtml's MIME-bundle format often loses canvas/WebGL context). Editing the embedded shader to ablate #5 requires either:
- Browser dev-tools shader hot-replace — possible in Chrome but fiddly; ShaderToy's per-shader compilation pipeline obscures the GLSL source after build.
- Editing the mhtml as text — fragile (base64-encoded sections, MIME content boundaries).

60 min is unrealistic if either fails. I need a 5-min binary go/no-go check before sinking effort.

**Fix:** B-empirical's first 5 min is "open mhtml in Edge/Chrome → confirm the shader renders an animated frame → confirm dev-tools shows the GLSL source." If any step fails, abandon to B-algebraic immediately. Hard total budget for B-empirical: 30 min.

### C3 — B-algebraic argument needs structure and a defensible conclusion (HIGH)

I wrote "answer with closed-form math + worst-case-discretization bound" but didn't think through whether the answer is non-trivial. On reflection:

ShaderToy bakes `Σ_bins L_bin · cos(θ_bin) · ΔΩ_bin` per bin where θ_bin = angle of bin-center direction relative to gNor.
Current bakes `L_bin` per bin (no cosine, no ΔΩ); consumer multiplies by `cos⁺(ray_dir · normal)` at integration time and sums.

These compute the SAME integral `∫ L cos⁺ dω` discretized differently:
- ShaderToy: cosine evaluated at bin center (bake-time, exact for bin center).
- Current: cosine evaluated per ray (consume-time, exact for the ray's actual direction).

For dirRes=8, bin solid angle is 4π/8² = π/16 ≈ 0.2 sr (~25° angular width). Across a 25° bin, cos can change substantially near grazing (θ ≈ π/2). However: this is a *discretization* difference, not a magnitude lever. Both formulations converge to the same answer as dirRes → ∞. The bright tail we measure in volumetric is structural bias from C1→C2 merge geometry (per failure-learnings §3), not Lambertian-integral discretization error.

**Therefore the algebraic answer is likely: "Delta #5 has small magnitude leverage even in surface-attached topology — both formulations are O(angular_width²)-bounded discretizations of the same integral. #5's primary contribution in Path B comes not from the cosine-pre-weighting itself but from the *topology* (hemisphere-only sampling above gNor) that makes the cosine pre-weighting tractable. Path B's leverage is the topology, not #5."**

This is a substantive prediction that informs the M1 → M2 decision: if M1_PARTIAL_MAGNITUDE, the gap-closing potential of Path B is the *topology* (less leakage from below-surface bins, fewer ill-conditioned rays) rather than the cosine-redistribution per se. The algebraic answer is therefore *more* useful than the empirical R-on/R-off pixel diff, because it identifies the actual mechanism rather than its surface manifestation.

**Fix:** elevate B-algebraic to the primary path. Drop B-empirical as a fallback or curiosity. Spec the algebraic deliverable with the structure above + a magnitude estimate (e.g., "discretization bias bound for dirRes=8: < 5% on irradiance for cornell-like geometry"). Time: 30–45 min instead of 60.

### C4 — Deliverable A timing is rosy (MEDIUM)

7 deltas × code paste + diff paragraph in 60 min = ~8 min per delta. Deltas #4 (multi-bounce 4-cube average — two call sites, MB feedback path) and #6 (WeightedSample θ semantics — subtle, paired with #1/#2) are not 8-min deltas.

**Fix:** revise A budget to ~90 min. If still over budget after 90 min, ship A with #4/#6 marked "[detail-paragraph deferred to per-delta impl doc]" rather than blocking Stage 0 closure.

### C5 — Stage 0 doesn't address OpenWolf housekeeping (LOW)

Per project CLAUDE.md: update `.wolf/anatomy.md` when creating files, append to `.wolf/memory.md` after significant actions. Plan ignored both.

**Fix:** add §3.5 Housekeeping noting that anatomy.md gets updated for the new doc files; memory.md gets a one-line entry per deliverable.

### C6 — Deliverable C scope omits hybrid shaders' location (LOW)

I said "Grep any hybrid correction shaders for probe sampling" without checking where they live. The hybrid shaders might be in `res/shaders/` or in a subdir; if they don't exist as separate files, audit scope shrinks.

**Fix:** §4 step 0: locate hybrid shader files first. If none, audit is just radiance_3d.comp + raymarch.frag.

### C7 — No definition of "cornell-analog frame" for ShaderToy B-empirical (LOW)

Moot if C3's fix lands (B-empirical dropped). Leaving here for completeness.

### C8 — Deliverable A doesn't specify where to put it (LOW)

Implied location is `doc/7/v20_shadertoy_diff_impl.md` per scope §2.1. Confirm.

---

## 7. Plan revisions (Phase 1c)

Revised execution order and budgets after self-critique:

| Order | Deliverable | Action | Time | Output |
|-------|-------------|--------|------|--------|
| 1 | C | Locate hybrid shaders (5 min); grep + tabulate -0.5 offsets across radiance_3d.comp + raymarch.frag + hybrid (if exist). | ~30 min | `tools/v3_baseline/delta7_offset_audit.md` |
| 2 | C+ | Read radiance_3d.comp merge block; classify `upperDir.rgb` content when α=0 into Case A/B/C. | ~30 min | `tools/v3_baseline/delta3_alpha_audit.md` |
| 3 | A | Write per-delta diff doc, leveraging C and C+ verdicts. If over 90 min, ship with #4/#6 detail-paragraphs marked deferred. | ~90 min | `doc/7/v20_shadertoy_diff_impl.md` |
| 4 | B | B-algebraic primary (drop B-empirical from plan). Derive bake-vs-consume cosine discretization argument, magnitude estimate for dirRes=8. | ~30–45 min | Inline append to v3 scope §1.1 + standalone note in `tools/v3_baseline/delta5_ceiling_estimate.md` |
| 5 | Housekeeping | Append memory.md entries (one per deliverable); update anatomy.md for new files. | ~10 min | Updated wolf files |

**Total revised: ~3 h** (was 2.5 h, +30 min realistic for A and B-algebraic depth). Still within the M0 ~5 h overall budget.

**Pre-execution gates locked:**
- **B-algebraic threshold:** "#5 is a large lever" if the algebraic bias estimate exceeds 10% of bright% (currently 11.1% on cornell). Below that, #5 alone cannot account for the bright tail and Path B's leverage must come from topology, not pre-weighting.
- **C+ Case B veto:** if Delta #3 audit returns Case B (semantic sky/miss content), the entire M1 work order for #3 is paused until a separate impl doc defines the volumetric analog. Do not auto-proceed.

---

## 8. Execution log

(Append-only as deliverables complete.)

## 8. Execution log

(Append-only as deliverables complete.)
