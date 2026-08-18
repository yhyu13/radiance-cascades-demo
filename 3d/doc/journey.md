# JOURNEY — How This Project Was Built

> A chronological, two-column account of how the 3D Radiance Cascades demo was built.
> **ME** = the human (requests, decisions, corrections, pivots). **YOU** = Claude (what was built, discovered, got wrong, and fixed).
> Rows are chronological; section headers group the work into eras. Dates are `YYYY-MM-DD`.

---

## Era 0 — Foundation: 2D → 3D migration (Phase 0–1.5)

| ME | YOU |
|---|---|
| "Build a 3D Radiance Cascades demo — migrate the 2D version to 3D." | Stood up the C++17 / OpenGL 4.3 + compute-shader skeleton (CMake + PowerShell `build.ps1`), migrated the 2D cascade concept to a 3D volume grid. |
| — (Phase 0 goal: analytic SDF) | Implemented the analytic SDF system (`sdf_analytic.comp`, `analytic_sdf.cpp`), debug visualization, normal viz. |
| — (Phase 1: debug views) | Added SDF debug modes (slices, normals) and fixed the linker/loading errors that came with them. |
| — (Phase 1.5: real scene) | Integrated the Cornell Box via OBJ loading (`obj_loader.h`), dynamic albedo from primitives, material system, GUI debug controls. |

---

## Era 1 — Cascade architecture: directional storage & merge (Phase 2–5)

| ME | YOU |
|---|---|
| "Make the cascades actually resolve directional light, not just a scalar per probe." | Built the multi-level cascade hierarchy (Phase 2–3), then the per-direction atlas with octahedral direction encoding (Phase 5a/5b). |
| — | Implemented directional merge (Phase 5c), non-co-located probe layout (5d), per-cascade direction-resolution scaling D (5e), directional bilinear sampling (5f). |
| — | Wired directional GI sampling into the final renderer (5g) and added shadow ray (5h) + SDF cone soft shadow (5i). |
| *(correction during this work)* | Learned and recorded that **cascade merging is bake-time, not consume-time** — the compute shader reads the upper atlas during bake; the frag only consumes the lowest cascade. |

---

## Era 2 — Sponza + GPU SDF + performance (doc/4, doc/5)

| ME | YOU |
|---|---|
| "Load Sponza, not just the Cornell box." | Ran the Sponza OBJ→SDF program across Steps 0–7: UI entry point, CPU voxelizer fix, Felzenszwalb EDT → `generateMeshSDF()`, render-pipeline wiring, auto-fit camera + light presets. |
| *(instituted a codex-critic review loop)* | Wrote plan/impl pairs and responded to 13+ codex reviews of the Sponza SDF work, accepting findings and revising each step. |
| — | Moved SDF generation to the GPU (JFA SDF, Step 8), accelerated the OBJ load path (Step 9), added camera/GI diagnostic modes (Step 10), GI bake strip + heatmap (Step 11), perf tooling at 1080p (Step 12). |

---

## Era 3 — Temporal accumulation & capture pipeline (Phase 9–14)

| ME | YOU |
|---|---|
| "The GI flickers / bands." | Implemented temporal accumulation + probe jitter (Phase 9), then chased banding through Phases 9a–9d (bilateral GI blur, TAA history clamp, GI blur mode fixes). |
| — | Built the automated capture pipeline (Phases 6a/6b/12a/12b/14a): RenderDoc in-process capture, screenshot bursts, multi-frame sequences, auto-analysis. |
| — | Added per-cascade range scaling (`c0MinRange`/`c1MinRange`, Phase 14b/c) after C0/C1 coverage analysis. |

---

## Era 4 — Visibility / leak (doc/6)

| ME | YOU |
|---|---|
| "Fix the light leak between probes and surfaces." | Ran the visibility program (Phases 1–3): depth-aware per-bin visibility, bake-side α as per-bin visibility, and the 3D `WeightedSample` leak fix, each gated by decision + critic review. |

---

## Era 5 — Hybrid RC + per-pixel correction (Phase 7–8)

| ME | YOU |
|---|---|
| "Cascade GI alone isn't converged — add a path-traced correction." | Built the progressive PT reference (`pt_reference.comp`, render mode 16), temporal multi-bounce feedback (Phase MB), and the hybrid RC + per-pixel correction GI (half-res PT accumulator + bilateral blur + cooperative merge). |
| **"AABB is killing the PT results."** (red bleed disappeared) | Found the symmetric luminance clamp was scaling bright legitimate bounces *down* toward a noisy neighborhood. Fixed to **HIGH-side-only (firefly) clamp**. This became a permanent rule: *never symmetric clamps on MC accumulators*. |
| *(v1.3.1 variance finding)* | Shipped a variance harness that exposed three silent-failure traps (bug-211/212/213) and showed the "DI cone" NEE fix was a −0.01% RMSE tie — **measurement reversed the tuning plan**. |

---

## Era 6 — MBRC v2.0-pre: measurement before features (2026-05-20 → 05-23)

| ME | YOU |
|---|---|
| **"Ship the measurement report as its own deliverable before any feature code."** | Locked v2.0-pre decisions: hybrid retirement is the goal, cornell-only test bed, unbounded memory, PT-reference (unbiased) quality target. |
| *(signed off the measurement-first contract)* | Built the v2.0-pre measurement harness: pinned measurement cameras, `--cascade-exclude` leave-one-out attribution, render modes 20/21, noise-seed offset, capture sweep driver. |
| — | Ran the named-hypothesis tree (α merge-mode, β MB-gain, γ angular, δ probe-density) and **eliminated all four** on the LDR metric. |
| — | Built the **HDR-EXR honest metric** (tinyexr, mode-17 EXR dump) and discovered the LDR colormap `divisor=0.2` was saturating a real signal — **all four LDR rejections were suspect**. |
| *(pause on cam0/cam2 asymmetry)* | Diagnosed cam0/cam2 asymmetry and learned the colormap pitfall: *never promote a "bidirectional residual" hypothesis from a per-pixel-normalized heatmap without an absolute Σ+/Σ− cross-check*. |

---

## Era 7 — MBRC v2.0: closing the gap (2026-05-23 → 05-26)

| ME | YOU |
|---|---|
| — | Ran the (h) source-disambiguation series: MB toggle, merge-variant sweep (M0/M2/M4), MB factorial, smoothstep blend-zone toggle, probe-cell `fract()` viz, spatial-trilinear A/B, downstream-knob rule-out — narrowing the asymmetry to **bake-side atlas content**, ruling the entire consume path innocent. |
| — | Built per-pixel dominant-direction-bin viz (mode 22, P2), then discovered via P2-E control that mode-22 measures **viewport composition, not GI parity**. |
| — | Ran CV1 absolute convergence → cascade asymptotes at **~65% of PT** (cornell). |
| **"Why are we still visual testing comparison — should we just fix MB RC with theoretical correction? What prevents our 3D RC from matching the ShaderToy 3D RC?"** | Pivoted from measurement to **algorithmic diff vs the in-tree ShaderToy reference**. Derived + landed the paired fix (Deltas #1+#2): consumer contract became `irrad = (4/D²)·Σ(L·cos⁺)`. Post-fix CV1 ratio 0.650 → **0.846**. |
| **"Go for v2.4"**, then **"go b and then c"** | Executed v2.2 (KILLED at Step 0), v2.3 (MARGINAL — leak spread evenly across 0.7% of probes), v2.4 (DEAD — C0 dirRes bump moved |p95| 0.1%), v2.4.b (DEAD — output-side clamp is a global dimmer, not a firefly leash), v2.5 Axis A (CLEAR_ATTRIBUTION — C1→C2 merge = 89.3% of bright% growth). |
| *(absorbed the 31-commit retrospective)* | Judged the **entire MBRC correction program FAILED** as a path to close the gap — no knob axis remained. Recorded the 7 hard "do-not-repeat" rules and banked the reusable learnings. |

---

## Era 8 — v3 ShaderToy pivot (2026-05-26 → 05-28)

| ME | YOU |
|---|---|
| **Pivot decisions locked: Path A → conditional Path B; Sponza first-class from M0; strict `|p95| ≤ 0.50` hybrid retirement (no wider band).** | Wrote the v3 adoption scope with milestone gates, and the M0 Stage 0 closeout (algorithmic diff of 7 deltas against ShaderToy). |
| *(corrected analytical rigor)* | Added a permanent self-critique rule: **direction-sign + derivative-sign-on-interval checks** before publishing any analytical doc (S2/S3 had both been wrong in Stage 0). |
| *(Stage 8–9)* | Attributed Sponza's over-brightness to **MB-feedback over-drive** (not structural) via source-energy A/B; ran the MB-gain ladder → best Sponza at gain=0.10 (non-monotonic), Cornell wants gain=1.0 → **no global gain works**. |
| **"Why does Stage 10 verification look like a pure dark scene?"** | Found the "dark scene" was mode-17 (GI-only) at low gain — working as designed. Re-ran as mode-0 final-composite A/B → **gain=0.10 closes 95% of the cascade-vs-hybrid gap on Sponza**. Vindicated Stages 8/9. |
| *(Stage 11b)* | Proved Cornell's ~2× under-brightness is **bake-side, not consumer** (per-cell ratio ≈ per-pixel ratio). Consumer `sampleProbeDir` is structurally clean. |
| *(Stage 11c)* | Isolated the mechanism to **point-light geometry**: `--light-direction` closes 87% of the gap (cascade gains 1.98× vs PT); cascade-directional even surpasses hybrid-point. |
| *(Stage 11d)* | Refined the mechanism: the real cause is **multi-bounce under-emit under enclosed geometry** — the cascade/PT ratio jumps discretely at the Cornell box boundary. Named H-A' (MB under-convergence), falsifying the earlier NEE hypothesis by code-read. |

---

## Era 9 — Phase 10 refactor: data-driven kernel (2026-07 → 2026-08)

| ME | YOU |
|---|---|
| "Refactor the proven transport into a data-driven kernel; retire the legacy surface-RC path." | Merged parity + legacy transport shaders into one **data-driven kernel** (`reference_transport.comp`) that reads a single scene SSBO and dispatches by primitive kind. Removed the legacy surface-RC path; established the parity gates (G0–G10). |

---

## Era 10 — Phase 11 generalization (2026-08)

| ME | YOU |
|---|---|
| "Generalize the proven kernel from hardcoded Cornell charts to real mesh surfaces (Sponza)." | Wrote `generalization_design.md`: the **chart stays the radiance atom**; only the chart-provider and tracer-integration seams change. Recommended authored UV2 charts + a new kind-5 mesh-island trace primitive; rejected AABB/box proxies and surfel pages. |
| **"Dump the JOURNEY doc (two columns ME / YOU)."** | This document. |

---

## Era 11 — Cornell-box audit: volumetric RC vs ShaderToy (2026-08-17, current)

| ME | YOU |
|---|---|
| "The Sponza scene is too complex for our SDF — the SDF isn't good enough for RC. Use the Cornell box instead to test the new RC." | Audited the SDF and confirmed: analytic Cornell is a clean signed field (**good enough**); Sponza's mesh path is a clamped UDF of a hollow surface shell that trilinear-march tunnels through (**not good enough**). The switch is correct. |
| "Systematically examine the new RC against the ShaderToy reference — concept + implementation, plan the gap, is the SDF correct, is the RC correct, then renderdoc a frame. Long task: use swarm agents + the rigorous-proof skill." | Ran a 5-agent swarm + adversarial audit (rigorous-proof methodology: pin the claim, diverse portfolio, audit for gaps/handwaving/circularity). Wrote `doc/12_cornell_rc_audit/rc_audit_report.md`. |
| *(headline finding)* | The consumer contract the project **believes it shipped** — `irrad = (4/D²)·Σ(L·cos⁺)` (Era 7, "CV1 0.650 → 0.846") — **is not in the committed code.** `raymarch.frag` still uses renormalized `Σ(L·cos·α)/Σ(cos·α)`; `(4/D²)` exists only in unapplied `diff_remote.patch`. Provenance gap: fix was measured on another branch or reverted in `dd4f5df`. |
| *(concept diff)* | 8 concept divergences: surface-charts vs uniform 3D grid; analytic trace vs SDF-march; square-ring hemisphere vs octahedral full-sphere; cubemap hit-distance `.w` vs binary α `.a`; sun+sky vs point-light + env-fill-OFF (default). |
| *(RC correctness)* | **PARTIALLY correct.** Octahedral round-trip, cos⁺ orientation, merge blend all correct. Top-3 defects: (1) `sampleUpperDirWeighted` reads upper atlas `.a` as a hit distance but the bake writes binary α; (2) consumer missing the octahedral Jacobian + `(4/D²)`; (3) `reduction_3d` unweighted D² average. |
| *(renderdoc)* | Rebuilt + ran headless (`--runtime-shell=legacy --auto-rdoc --exit-frames=3000`), captured analytic-Cornell frame 420. GPU ≈ 37.7 ms; C2 bake is the hotspot (11.2 ms for 512 probes → march-length, not probe-count). Full report: `doc/12_cornell_rc_audit/renderdoc_report.md`. |
| *(discovery: app-shell split)* | The Phase-10 refactor made the default `app3d` shell run the surface-RC reference; the volumetric RC now runs behind `--runtime-shell=legacy`. Also: the auto-analysis scripts fail under the default Python 2.7 (non-ASCII source) — `rdoc_extract.py` is fine (qrenderdoc's Python 3.6). |
| *(A2–A9 execution, measured)* | Implemented A2 (octahedral ΔΩ consumer). Its CV1 gate **did NOT pass**: `dim% ≈ 88%` (18× over the 5% bar) — the real energy gap; `bright% ≈ 3.4%` is in spec. `ratio_mean 0.43` and `p95(|ln|)=13.8` are not the gate (13.8 is the zero-floor clamp, a coverage number). Removed the consumer α-gate and read 0.66 as progress — **wrong** (α-gate = leak prevention; 0.66 conflated with bleed). Reverted. |
| *(correction: attribution + A4)* | MB-off 0.22 is the known Cornell collapse (Stage 8, "diagnostic not fix"); its cause is **unverified** (H-A' is the leading hypothesis, 11e.A never ran). The PT reference **already has** directional light (`uLightDir`/`uUseDirectionalLight`); "black" was a sun-direction sign error. A4 = correct sign + sun/sky + cheap self-validation — **not blocked, not circular**. |
| **"Dump to JOURNEY.md."** | This entry. |

---

## The recurring lesson of this project

Every time a fix was built on intuition, measurement or a reference diff overturned it:

- the DI-cone "NEE" was a −0.01% RMSE tie
- the LDR metric was saturating a real HDR signal
- the "consumer-side" Cornell bug was actually bake-side
- the "probe rays miss small light" hypothesis was actually multi-bounce under-emit
- four named lever axes (α/β/γ/δ) all came up empty against a reference that was correct by construction

**The project converged on one working posture: diff against the reference first, measure only to validate the fix — and when a diagnosis is visual, cross-check it against absolute numbers before acting.**

---

## How this project teaches "vibe coding" with AI

This whole repo is one long A/B test of a working style: a human steering by instinct, an AI executing at volume — and the project only started *working* once the two jobs were split correctly. The reusable lesson is the division of labor, not the graphics.

### The human's job — decide, correct, kill

| ME (the vibe) | Why it worked (project anchor) |
|---|---|
| **Steer with one-liners, not specs.** | "dump impl to doc", "go b then c", "why are we still visual testing — diff the reference" moved the project more than any written plan. The AI handles the elaboration; the human only aims. |
| **Correct in one sentence, expect it banked forever.** | "AABB is killing the PT results" became a permanent HIGH-side-only rule that was never violated again. One correction → durable memory is the highest-leverage human move. |
| **Demand verdicts, not vibes.** | "go for v2.4" was always paired with a pre-committed bar; when the AI returned DEAD/MARGINAL instead of a win, the line was *killed*, not retried. The human's discipline is *stop*, not *more*. |
| **Challenge drift.** | The pivot to theoretical-fix happened because you asked "why are we still measuring?" — not because the measurement failed. Catching the AI mid-low-value-loop is a core human job. |

### The AI's job — instrument, falsify, report honestly

| YOU (the vibe) | Why it worked (project anchor) |
|---|---|
| **Measure before building, diff before measuring.** | No reference → measure first (v2.0-pre report). Reference exists (ShaderToy) → diff the algorithm, skip the sweep. Both postures beat "just build the obvious fix." |
| **Report DEAD / MARGINAL / FAILED without flinching.** | Four lever axes eliminated, v2.2 KILLED at Step 0, v2.4/v2.4.b DEAD, the 31-commit program judged FAILED. Killing a line of work is output, not failure. |
| **Cross-check every visual conclusion against a number.** | Mode-19 heatmap *looked* like basis-error; the Σ+/Σ− script said under-bright. The DI-cone *looked* like a win; the variance harness said −0.01%. Visuals lie; scripts don't. |
| **Write the doc without being asked twice.** | Every phase dumped to `doc/`, so context resets cost nothing. The journey you're reading exists because that habit was the default. |

### The portable rules (any AI-coding project)

1. **Pre-commit the verdict band before running** — then the result can't be hand-waved.
2. **Fail fast at Step 0** — test the premise in the same cheap script that ships the data (v2.2 saved ~3.5h this way).
3. **Encode every correction as a Do-Not-Repeat** — the AI must not re-derive it next session.
4. **Narrow the mechanism, don't widen it** — each failure rules *out* a class of fixes, not just a value.
5. **Trust the contract, not the name** — "cascade merge is bake-time", "the chart is the radiance atom" — and check actual numbers before claiming "wider/tighter/larger".
6. **Adversarial review is cheap insurance** — every plan/impl went through a codex critic before it became code.

### One-sentence takeaway

**Vibe coding works when the human supplies taste and the willingness to kill, and the AI supplies throughput and the honesty to report failure — the moment either side fakes the other's job, the project stalls.**
