# Plan: Phase 2.5 — Phase 2 Residual Cleanup + Soft α (rev 2, post-critic-10)

**Date:** 2026-05-14
**Predecessors:**
- [visibility_phase2_impl.md](visibility_phase2_impl.md) — Phase 2 shipped: render-side α-gate works; bake-side leaks NOT fixed
- [reply_09_visibility_phase2_impl_review.md](critic/reply/reply_09_visibility_phase2_impl_review.md) — accepted critic findings W1-W9
- **[critic 10](critic/10_visibility_phase2.5_plan_review.md) → revision 2 (this version)** — 4 HIGH findings forced a major scope cut: the bake-side leak fix is research-level, not a one-day shader patch. Removed from Phase 2.5; filed as Phase 3.

**TL;DR (rev 2):** Phase 2's residual gaps split cleanly into "shippable today" and "research-level." Phase 2.5 ships only the former; the latter is filed as Phase 3.

- **Phase 2.5a — Investigations (~1 day, no behavior change).** Run the deferred formal bake-leak quantitative test to **measure how big the bake-side leak is today** (so a future Phase 3 has a baseline to drive toward zero). Investigate `reduction_3d` +42% timing anomaly. Pick a sky/surface α encoding for soft-α work.
- **Phase 2.5b — Soft α (~1.5 days, possibly visible quality change).** Add soft α to surface bins via SDF-proximity smoothstep. Requires the encoding decision from 2.5a. **Does NOT touch the bake's radiance merge formula** (that's Phase 3 — see "What was scope-cut" below).
- **Phase 2.5c — Cleanup (~0.5 day).** Delete `--visibility-mode=N` CLI stub. Remove archaeological comments.

**Total: 2.5–3 days**, lower-risk than rev 1 (which estimated 3–4 days but included a research-level item). **Phase 3 (the bake-side leak fix) is filed separately** because critic 10 H1 demonstrated the proposed formula in rev 1 was geometrically wrong; doing it correctly requires a real derivation, not a one-day shader patch.

---

## 1. Context

### What Phase 2 left undone (per critic 09)

| Residual | Severity | This phase? |
|---|---|---|
| Bake-time leaks NOT FIXED | HIGH | **NO — Phase 3** (research-level; rev 1's attempt was wrong per critic 10 H1) |
| Sky/surface α encoding ambiguity | MEDIUM | **YES — 2.5a.3** (decision) + **2.5b** (implementation) |
| v5 normalization coupled to current leaky bake | MEDIUM | **NO — Phase 3** (only matters if the bake is fixed) |
| `reduction_3d` +42% possibly latent bug | MEDIUM | **YES — 2.5a.2** (audit) |
| Formal bake-leak quantitative test never executed | MEDIUM | **YES — 2.5a.1** (baseline measurement) |
| `--visibility-mode=N` CLI stub | LOW | **YES — 2.5c** |
| `(Phase 2 2C)` archaeological comments | LOW | **YES — 2.5c** |

### What was scope-cut (rev 1 → rev 2)

Critic 10 H1 demonstrated that rev 1's proposed bake-side leak fix formula was wrong in three ways: coordinate systems mixed up between cascade grids, lateral wall topology not addressed, and the test didn't account for what the upper cascade actually contains. Per H1's recommendation, I'm not pretending a one-day shader patch can solve correctly fixing bake-side leaks with non-co-located probes — that's a research problem.

**Action:** filed as Phase 3 (visibility / bake-side leak fix). No plan doc yet; will be drafted when someone is ready to spend days deriving the correct formula from a reference paper or Sannikov's followups.

**What this means for Phase 2.5:**
- The v4 render normalization switch (W4) is also scope-cut. v4 is correct under an energy-conserving bake; until Phase 3 ships an energy-conserving bake, v5's coupling to the current leaky bake remains the right choice.
- The soft α work (2.5b) ships independently of the bake fix. Soft α is an enhancement to the existing α derivation, not a replacement of the bake merge formula.

### Why the remaining work is still worth doing

- **2.5a measurements feed Phase 3** — without quantified leak baselines per scene, Phase 3 has no success criterion.
- **2.5a's `reduction_3d` audit** is independent and may surface a real bug.
- **2.5b's soft α** adds visible quality (gradual occlusion at near-surface boundaries) without depending on Phase 3.
- **2.5c is pure tidy** that should ship anyway.

---

## 2. Phase 2.5a — Investigations (~1 day, no behavior change)

### 2.5a.1 — Formal bake-leak quantitative test (revised per critic 10 H2)

**Goal:** quantify how much leaked radiance is in the atlas today (Phase 2 baseline). This is the measurement Phase 3 will drive toward zero.

**The metric (corrected per critic 10 H2):** the rev-1 plan proposed `sum(bin.rgb × bin.a)` and then noted in §6 that this returns zero for current Phase 2 (since occluded bins have α=0). Use the corrected metric upfront:

> Sum `bin.rgb` (RGB only, **without** multiplying by α) restricted to bins that satisfy ALL of:
> 1. The probe is in a geometrically occluded region (per scene-specific test geometry).
> 2. The bin's `bdir` points toward a known light source (`dot(bdir, normalize(lightPos − probePos)) > 0`).
> 3. The bin's stored α=0 (the bake classified it as opaque OR sky-terminal — both encode α=0 in the current Phase 2).

This metric:
- Returns nonzero today (the bake's smoothstep mixes upper-cascade radiance into surface-hit bins; α=0 hides this at render but the RGB is in the atlas).
- Returns ~zero post-Phase-3 (when the bake-side merge formula no longer leaks).
- Excludes legitimate sky-RGB (the test geometry is a closed-room alcove behind a partition; sky bins shouldn't be selected by step 1 — probe is inside walls).

**Caveat (per critic 10 H2):** "stored α=0" includes both surface hits and sky exits in the current Phase 2 encoding. For probes inside an alcove the geometric test (step 1) excludes most sky-bin scenarios, but a bin pointing UP through a missing ceiling (env-fill-ON case) could still be counted. The cornell-orig-alcove scene is fully enclosed (no missing ceiling) so this concern is theoretical there. **For Sponza spot-checks, this metric may overcount; treat Sponza numbers as upper-bound estimates.**

**Method:**
1. Load `cornell-orig-alcove`. Render with default settings until temporal accumulation converges (≥ 60 frames at α=0.1 OR temporal-off).
2. Capture the directional atlas via RenderDoc.
3. Run a Python post-processing script (extends `tools/rdoc_extract.py` or new) that walks the atlas:
   - For each probe in the alcove (`probe_world_x > 0.30`):
     - For each bin where `bdir` points toward the light fixture (`dot(bdir, normalize(lightPos - probeWorld)) > 0`):
       - If `bin.a < 1e-3` (effectively zero — surface hit OR sky), accumulate `length(bin.rgb)`.
4. Aggregate: total occluded-region radiance leak (a single scalar per scene per cascade).

**Deliverable:** [tools/phase2.5_bake_leak_baseline.json](../../../tools/phase2.5_bake_leak_baseline.json) with per-scene per-cascade leak quantities.

**Cost:** 0.5 day.

### 2.5a.2 — `reduction_3d` +42% investigation (revised per critic 10 M3)

**Goal:** determine if real or noise. If real, also propose a fix.

**Method:**
1. Run N=3 RenderDoc captures of Phase 2 default at cam.md Sponza. Average per-pass timings.
2. Run N=3 of pre-Phase-2 Mode 0 (revert visibilityMode default temporarily on a separate clone).
3. If averaged delta is still > 20%, audit `res/shaders/reduction_3d.comp` for any code reading or thresholding atlas alpha.

**If a real bug is found, what does the fix look like?** Three categories:

- **Cosmetic** (reduction reads α for display/diagnostic only, not for compute path): document; no fix needed.
- **Statistic-skewing** (reduction sums or averages atlas α as part of "probe energy" or similar): the new α (transparency 0/1) is semantically different from old α (hit-distance). Either (a) reinterpret the statistic with new semantics, or (b) compute the equivalent of the old hit-distance from another source.
- **Performance regression** (reduction's branch-divergence pattern changed because α distribution changed): probably accept; no fix.

**Deliverable:** [tools/phase2.5_reduction_3d_audit.md](../../../tools/phase2.5_reduction_3d_audit.md) with verdict + fix shape if applicable.

**Cost:** 0.5 day.

### 2.5a.3 — Sky/surface α encoding decision (revised per critic 10 H3+M2)

**Goal:** pick an encoding that distinguishes sky (terminal) from surface (potentially soft) α, so Phase 2.5b can implement soft α.

**Three options reconsidered (the rev-1 plan recommended Option A — sentinel α=-1 — which critic 10 H3 showed silently breaks `sampleProbeDir` and the bake's `sampleUpperDir` α propagation through cascade chains):**

| Option | Encoding | Pro | Con |
|---|---|---|---|
| ~~A — Sentinel α (`sky=-1`)~~ | sky = `-1.0`; surface = `0.0`; miss = `1.0` | One-line shader change; no memory delta | **Breaks `w = wcos × a.a` (negative weights subtract sky radiance) AND breaks `alpha = thisAlpha × upperDir.a` propagation (negative α propagates through cascade merge as `-α_chain`). Per critic 10 H3 — silently breaks consumers.** Rejected. |
| **B — Reserved range (recommended)** | sky = strict `0.0`; surface hard = `ε = 1.0/65504.0` (RGBA16F minimum positive); miss = `1.0`; soft surface ∈ `(ε, 1)` | No negative values; `> 0` test works for "any visibility"; consumers don't need updating (math just works) | The ε threshold is RGBA16F-precision-fragile (denormals get flushed?); sentinel-via-magnitude is a code smell |
| C — Separate metadata texture | α stays continuous `[0, 1]`; new R8 texture stores 1-bit "is sky"; readers consult both | Cleanest separation; α has uniform semantics | +memory (R8 atlas same dims = ~46 MB at 1280×720 × 5 cascades × D²); +1 fetch per render-side α-gate; new texture binding to maintain |

**Recommendation: Option B (reserved range).** The ε encoding of "hard surface" requires zero consumer code changes — the math `wcos × a.a` works for ε just like 0.0 (both round-to-zero contribution). The only place that cares about distinguishing surface-α=ε from sky-α=0 is **the soft-α derivation in 2.5b**, which can use `if (hit.a > 0.0) alpha = max(SDF_smoothstep, eps);` — sky stays at strict 0.

**RGBA16F precision concern**: half-float minimum positive normal is ~6.1e-5; denormal minimum is ~5.96e-8. The proposed `ε = 1/65504 ≈ 1.5e-5` is a denormal in half-float. Driver behavior on denormals varies; some flush to zero. **Need to verify on the target driver before committing to this encoding.** If denormals flush, use `ε = 1e-4` (clearly normal) — this slightly biases all α values but avoids the precision trap.

**Deliverable:** decision pinned (Option B with `ε = 1e-4` if denormals are a concern; smaller if not). Implementation rolls into 2.5b.

**Cost:** 0 days (decision only). Driver-precision check is a single shader-compile-and-print test that takes minutes.

---

## 3. Phase 2.5b — Soft α via SDF-Proximity Smoothstep (~1.5 days)

### 3.1 Goal

Replace binary surface α (currently `0` for hit, `1` for miss/sky) with a smoothstep based on near-surface SDF distance. Surface bins gradually gate as the ray approaches the wall; bins that hit deep into a wall stay binary `0` (or `ε` per Option B); bins that hit near a wall edge get α ∈ `(0, 1)` reflecting the partial occlusion.

This adds gradual occlusion at near-surface boundaries, reducing the "hit/miss flicker" that current binary α can cause for surfaces near probe-cell boundaries.

**Does NOT touch the bake's radiance merge formula.** The radiance values stored in the atlas are unchanged; only the α derivation changes. Phase 3 will revisit the merge formula separately.

### 3.2 Algorithm

In [radiance_3d.comp](../../../res/shaders/radiance_3d.comp) per-direction loop, when computing α for a surface hit:

```glsl
// Current Phase 2 binary α:
//   if (hit.a > 0.0) alpha = 0.0;
//   else if (hit.a < 0.0) alpha = 0.0;  // sky = terminal
//   else                  alpha = 1.0;  // miss

// Phase 2.5b soft α (Option B encoding):
const float kSurfaceEps = 1e-4;  // hard surface; ε > 0 distinguishes from sky=0
if (hit.a > 0.0) {
    // Surface hit. Sample SDF a small step BEFORE the hit to detect grazing
    // approaches. SDF near 0 → hit was head-on into a wall → α near eps (opaque).
    // SDF clearly positive → hit was a grazing approach → α near 1 (transparent).
    vec3  worldSize = uVolumeMax - uVolumeMin;
    float voxelSize = worldSize.x / float(uVolumeSize.x);
    float sampleDist = max(hit.a - voxelSize * 0.5, 0.0);
    float sdfBefore  = sampleSDF(probeWorld + bdir * sampleDist);
    // Map SDF: 0 → α=eps; voxelSize → α=1.
    alpha = mix(kSurfaceEps, 1.0,
                smoothstep(0.0, voxelSize, sdfBefore));
} else if (hit.a < 0.0) {
    alpha = 0.0;  // sky terminal — distinct from surface (α≥eps)
} else {
    alpha = 1.0;  // in-volume miss
}
```

**The smoothstep range `[0, voxelSize]`** is a starting heuristic. Wider range (e.g. `[0, 2 * voxelSize]`) gives softer transitions at the cost of more "leak fade-in" near walls. Narrower range gives sharper transitions closer to binary. Tunable.

### 3.3 Implementation surface

- **`radiance_3d.comp`**: replace the binary-α derivation in the per-direction loop with the smoothstep above. ~10 lines.
- **`raymarch.frag`**: no change needed. `sampleProbeDir` already does `w = wcos × a.a`; if α is soft, the bin contributes proportionally. Existing behavior is correct.
- **No C++ changes.** The atlas format is unchanged (still RGBA16F, since Phase 5g).

### 3.4 Verification (revised per critic 10 H4 — tiered pass/fail)

Quality A/B at the standard test viewpoints (cam.md Sponza, Cornell-orig, Cornell-orig-alcove with the explicit alcove-cam viewpoint per M4 below). Compare against Phase 2 baseline (NOT Phase 1 Mode 4 — Phase 2 is the new reference).

**Tier 1 — Ship 2.5b:** All scenes maintain or improve quality vs Phase 2.
- Sponza RMSE vs Phase 2 ≤ 0.02 (small, expected change from soft α).
- Cornell scenes RMSE vs Phase 2 ≤ 0.02.
- Subjective: no new artifacts (no banding at hit/miss boundaries, no visible "halo" around walls).

**Tier 2 — Ship 2.5b but document partial improvement:** Quality acceptable but not strictly improved.
- Any scene RMSE between 0.02 and 0.05 vs Phase 2 → ship if subjective inspection shows no obvious regression.
- Document in impl doc: "soft α changes the look slightly; users with strong opinions about the Phase 2 look can revert."

**Tier 3 — Don't ship soft α:** Quality regresses noticeably.
- Any scene RMSE > 0.05 vs Phase 2 → don't ship; investigate smoothstep range tuning.
- New visible artifacts (banding, halo, energy loss) → don't ship; iterate.

**Bake-leak baseline re-measurement:** soft α changes the count of "α=0" bins (now α=ε or smoothstep instead). Re-run 2.5a.1's metric with the new threshold (`bin.a < kSurfaceEps × 1.5` instead of `< 1e-3`) to confirm Phase 3's eventual baseline is still measurable.

### 3.5 Decision gate at end of 2.5b

| Outcome | Action |
|---|---|
| Tier 1 pass on all scenes | Ship soft α as new default. |
| Tier 2 partial pass | Ship with documentation; flag for user review. |
| Tier 3 fail on any scene | Don't ship soft α. Revert this commit; ship 2.5a + 2.5c only. File soft α retry as Phase 2.6. |
| `voxelSize`-relative smoothstep range needs scene-specific tuning | Keep range as a uniform; expose as ImGui slider (similar to existing `softShadowK`). Default to `[0, voxelSize]`. |

---

## 4. Phase 2.5c — Cleanup (~0.5 day)

### 4.1 Delete `--visibility-mode=N` CLI stub

Phase 2's deprecation grace period was "one release." Phase 2 hasn't been tagged as a release yet — Phase 2.5 is part of the same release. The stub can be deleted in 2.5c.

**Code change:** delete the `else if (arg.rfind("--visibility-mode=", 0) == 0)` branch in [main3d.cpp:281-289](../../../src/main3d.cpp#L281-L289). Delete the `setVisibilityMode` stub in [demo3d.h:570-577](../../../src/demo3d.h#L570-L577).

### 4.2 Remove archaeological comments

Phase 2 2C left "(removed in Phase 2 2C cleanup)" comments in [raymarch.frag](../../../res/shaders/raymarch.frag) at deletion sites. Delete them now.

### 4.3 Cornell-orig-alcove camera viewpoint pinning (per critic 10 M4)

The auto-fit camera for the alcove scene may not show the alcove well. Add a pre-defined camera in code that points at the alcove for visualization tests.

Suggested viewpoint (to be empirically validated when 2.5a.1 runs):
- pos: `(0.6, 1.0, 0.5)` — outside the alcove, above eye level
- target: `(0.6, 0.0, -0.5)` — looking down into the alcove

Add a CLI flag `--cam-preset=alcove` or document the explicit `--camera-pos` / `--camera-target` to use.

### 4.4 Atlas debug viewer label (per critic 10 M5)

The existing atlas debug viewer (ImGui mode in render-mode combo) shows raw atlas RGB without honoring α. With soft α landing in 2.5b, the viewer becomes more misleading.

Two options:
- **Quick:** add a tooltip on the viewer mode that says "shows raw atlas RGB; ignores α (which gates visibility at render). Bake-time leaks visible here are expected per Phase 2 §3.4."
- **Cleaner:** add a checkbox "respect α" that multiplies displayed RGB by `max(α, 0)` (handles soft α correctly).

Default to "Quick" for 2.5c; "Cleaner" can be a follow-up.

### 4.5 Final regression test

Build clean + smoke run. **Pixel-identical PNG match** (per critic 09 W7) against pre-2.5c baseline. Ensures cleanup didn't change behavior.

---

## 5. Sub-phase commit shape (revised per critic 10 M1 with per-commit time)

Recommended commits:

| # | Commit | Time | Description |
|---|---|---:|---|
| 1 | `[Claude] Phase 2.5a.1: bake-leak baseline measurement script + JSON` | 0.5d | New analysis script; new baseline JSON. No code semantics change. |
| 2 | `[Claude] Phase 2.5a.2: reduction_3d audit (3-run averaged timings)` | 0.5d | New audit doc. May or may not include a fix commit if a bug is found. |
| 3 | `[Claude] Phase 2.5b: soft α via SDF-proximity smoothstep` | 1.5d | radiance_3d.comp α derivation + verification per §3.4 tiers. **Subject to §3.5 decision gate.** |
| 4 | `[Claude] Phase 2.5c: cleanup CLI stub + archaeological comments + alcove viewpoint + atlas viewer label` | 0.5d | Tidy. Pixel-identical match expected. |

**Total: 3 days.** Commit 3 is the only one with non-trivial risk; commit 4 ships independently if 3 fails.

---

## 6. Risks (revised per critic 10 throughout)

- **Soft α may introduce new banding** at hit/miss boundaries where binary α was hard. The smoothstep range needs tuning per scene; if no single value works across Sponza + Cornell, add the ImGui slider per §3.5.
- **RGBA16F denormal-flush** on the target driver could break Option B encoding (sky=0 vs surface=ε indistinguishable). Pre-2.5b verification step: compile a tiny shader that writes ε and reads it back; confirm precision survives. If it doesn't, use a larger ε (e.g. `1e-3` clearly normal) or fall back to Option C (separate metadata texture).
- **`reduction_3d` audit may find a real bug** that requires its own commit before 2.5b can land cleanly. Plan accommodates this — 2.5a closes before 2.5b starts.
- **Soft α changes atlas content distribution** — temporal-blend AABB clamp may behave differently. The Phase 1 prerequisite patch (`temporal_blend.comp` preserves `cur.a`) is still in place; the AABB clamp on RGB is unchanged. But **the new α distribution mid-bake** could trigger flicker if a probe oscillates between hard-surface (α=ε) and soft-surface (α=0.5) frame-to-frame. Verify temporal stability during 2.5b.
- **Bake-leak baseline metric (2.5a.1) may overcount in Sponza** (per critic 10 H2). Treat Sponza numbers as upper-bound; cornell-orig-alcove is the rigorous scene.
- **The atlas debug viewer label/checkbox is a usability fix, not a correctness fix.** Until Phase 3 ships the bake-side leak fix, the atlas genuinely contains leaked radiance and the viewer correctly shows that. The label just makes it less surprising.
- **Cost regression from soft α** — the SDF sample in the bake (`sampleSDF(probeWorld + bdir * sampleDist)`) adds work to the per-direction loop. May be ~1 SDF fetch per bin per probe per cascade per frame. Could be significant if the bake is already a bottleneck. Pre-commit verification: time the bake before/after.

---

## 7. What was scope-cut (Phase 3, separate doc)

Removed from Phase 2.5 per critic 10 H1:

- **Bake-side radiance merge formula change.** Rev 1 proposed a "geometry-aware merge" using probe-position offsets along bdir. Critic 10 H1 demonstrated the formula was geometrically wrong (mixed coordinate systems, ignored lateral wall topology, didn't account for upper cascade contents). Doing this correctly is research-level — needs a derivation from a reference paper (or a collaborator with deeper RC literature knowledge). Filed as Phase 3.
- **v4 render normalization switch.** v4 (cos-only `wsum += wcos`) is correct under an energy-conserving bake. Until Phase 3 ships an energy-conserving bake, v5's coupling to the current leaky bake remains correct. Filed as part of Phase 3.

When Phase 3 happens (no timeline; needs research investment), the 2.5a.1 baseline measurements will be the success criterion (drive the leak metric toward zero).

---

## 8. Files this plan would produce

### Phase 2.5a
- `tools/phase2.5_bake_leak_baseline.json` — quantified per-scene per-cascade leak
- `tools/analysis/phase2.5_bake_leak_metric.py` — script extending rdoc_extract output
- `tools/phase2.5_reduction_3d_audit.md` — verdict on the timing anomaly (+ optional fix commit)

### Phase 2.5b
- Edits: `res/shaders/radiance_3d.comp` (α derivation only; no merge formula change)
- New: `tools/phase2.5_post_*.png` captures + diff metrics
- New: `doc/6/claude_plan/visibility_phase2.5_impl.md` (impl doc)

### Phase 2.5c
- Edits: `src/main3d.cpp` (delete CLI branch), `src/demo3d.h` (delete stub), `res/shaders/raymarch.frag` (delete archaeological comments)
- Optional: `src/demo3d.cpp` (alcove camera preset + atlas viewer label)
- Bit-exact regression capture

---

## 9. Recommended next action

**Start with Phase 2.5a — both investigations are cheap, the metric design is now correct (per critic 10 H2), and the encoding decision (per critic 10 H3) needs the driver-precision check before 2.5b can begin.**

If 2.5a's reduction_3d audit finds a real bug, fix it and re-evaluate before 2.5b.

If 2.5a's encoding-precision check shows denormals flush (Option B fails), switch to Option C (separate metadata texture) before 2.5b. The plan should be revised at that point.

Phase 2.5b's decision-gate criteria (Tier 1/2/3) will determine if soft α ships.

Phase 2.5c is independent and ships regardless of 2.5b.

---

## 10. Honest assessment

This rev-2 plan is **smaller in scope and lower in risk** than rev 1. The rev 1 plan oversold what could fit in 3-4 days; critic 10 H1 demonstrated the bake fix was actually research-level. Rev 2 acknowledges this honestly: ship the cheap, well-defined improvements; file the hard problem as Phase 3.

**What this plan delivers:**
- Quantified bake-leak baseline (data Phase 3 will need)
- `reduction_3d` audit (closes a possibly-latent bug)
- Soft α (visible quality improvement, low risk)
- CLI/comment cleanup (tidy)

**What this plan does NOT deliver:**
- Bake-side leak fix (Phase 3)
- Energy-conserving bake (Phase 3)
- v4 render normalization switch (Phase 3)
- Architectural completion of the visibility/GI subsystem (Phase 3 — there's still real work to do)
