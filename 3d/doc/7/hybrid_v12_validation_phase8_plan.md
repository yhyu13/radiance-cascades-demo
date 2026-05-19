# Phase 8 Plan — Hybrid v1.2 Validation Suite + Measurement-Driven Next-Phase Decision

**Date:** 2026-05-19
**Status:** Plan (rev 1 → 2 after self-critic, see §11)
**Predecessor:** [hybrid_rc_pixel_correction_impl.md](hybrid_rc_pixel_correction_impl.md) (v1.2.1 cooperative inverse-variance merge)
**Driving learning:** [cerebrum L9](../../.wolf/cerebrum.md) — smoke tests do NOT validate visual-output correctness; visual A/B is mandatory before claiming a rendering feature works.

---

## 1. Why this phase exists

v1.2.1 hybrid shipped two non-trivial fixes (relative variance + confidence ramp) on top of a fresh architecture (separate bilateral blur + cooperative merge) WITHOUT any visual validation. The console smoke test confirms "doesn't crash." It does NOT confirm:

- Whether the variance-merge actually closes the mode 19 blue gap
- Whether the cooperative property is visible on Sponza
- Whether the per-frame perf cost is acceptable (~10-20 ms target was a guess, never measured)
- Whether the principled fix `cascade_MB_delta` (plan v2) is still needed once measurement is in

This phase exists to ANSWER those questions empirically. The output is either "v1.2.1 ships as the final answer" or "here is the measured gap; v2 spec is justified by N% RMSE residual."

## 2. TL;DR

- **Day 1:** Build a `--hybrid-ab-sweep=path` driver that captures a fixed scene+camera 4 ways (cascade-only / hybrid-mix / hybrid-variance-merge / PT-truth) at matched-sample-count to disk
- **Day 2:** Per-pixel quality metrics: RMSE vs PT, mode 19 blue-pixel count, mean GI brightness ratio. Python script in `tools/analysis/`
- **Day 3:** Perf instrumentation: GL timer queries on `hybrid_correction.comp` + `hybrid_blur.comp`; report ms/frame at 720p / 1080p
- **Day 4:** Run the suite on Cornell-orig (control) + Sponza cam.md (target)
- **Day 5:** Decision gate. Three branches:
  - **A — Ship v1.2 final:** RMSE < 10% vs PT_GI on both scenes; perf < 25 ms at 1080p
  - **B — Spec cascade-MB-delta v2:** RMSE 10-25%; gap concentrated in regions cascade MB would help
  - **C — Re-investigate variance merge math:** RMSE > 25% or qualitative oddity (banding, ghosting)
- **Realistic budget: 4-6 days** (matches critic-05 scope-honesty pattern; visual validation phases routinely exceed initial estimates)

## 3. What's actually being measured (the scientific question)

Given two GI signals A (cascade) and B (clean hybrid correction), is the cooperative inverse-variance merge a SUFFICIENT answer to the multi-bounce-loss tradeoff (critic-05 H1), or is the principled separate-MB-delta dispatch still required?

Concretely:
- `merged = (wCorr*B + wCasc*A) / (wCorr + wCasc)` with relative variance + confidence ramp
- vs.
- `principled = clean_correction + (cascade_with_MB - cascade_without_MB)` (plan v2, ~2× cascade cost)

The merge OUGHT to approximate `principled` because it uses cascade signal where cascade is more confident (cascadeVar low) and correction where correction is more confident (corrVar low after convergence). But "ought to" is a hypothesis; this phase tests it.

The measurement: per-pixel RMSE(merged, PT) vs per-pixel RMSE(principled, PT). If they're within ~5%, merge is sufficient. If `principled` is substantially closer, the v2 work is justified.

## 4. Implementation steps

### 4.1 Day 1 — A/B sweep driver

**New CLI flag:** `--hybrid-ab-sweep=<output_dir>`

When set, after the existing burst-screenshot triggers (already in place per Phase 12b), additionally capture:

```
output_dir/
  cascade_only.png     # useHybrid=0, default cascade
  hybrid_mix.png       # useHybrid=1, mix mode, w=1.0
  hybrid_max.png       # useHybrid=1, max mode (legacy)
  hybrid_variance.png  # useHybrid=1, variance-merge mode (v1.2 default)
  pt_reference.png     # render-mode=16, samples-matched
  metadata.json        # scene, camera, samples, hybrid params, build hash
```

All five captures use **the same camera + scene + lighting + sample budget**. Capture happens at frame F after warmup ≥ 60 frames (lets cascade settle, hybrid accumulator converge ≥ confidence threshold).

**Implementation cost:** ~80 lines in `src/main3d.cpp` (sweep driver) + reuse of existing `TakeScreenshot` helper. Bind+rebind composition mode + render-mode programmatically via existing setters.

### 4.2 Day 2 — Quality metrics

**New script:** `tools/analysis/hybrid_quality_metrics.py`

Reads the five PNGs from §4.1 and computes:

```
For each of {cascade_only, hybrid_mix, hybrid_max, hybrid_variance}:
  rmse_vs_pt          = sqrt(mean((sample - pt_reference)^2))   # full image
  rmse_vs_pt_gi_only  = same but indirect-only (subtract PT direct)
  blue_pixel_count    = mode 19 dominant-blue pixel count
  red_pixel_count     = mode 19 over-bright pixel count
  mean_brightness_ratio = mean(sample) / mean(pt_reference)
  per_region_rmse     = RMSE broken into 4x4 grid (find local hotspots)
```

Output: `metrics.json` + a markdown table renderable inline. Targets per cerebrum L9 discipline: include `cascade_only` as baseline so the relative improvement (or lack thereof) is unambiguous.

**Implementation cost:** ~120 lines of Python. Image I/O via PIL; arithmetic via numpy. Pattern mirrors existing `tools/analysis/phase1_diff_metrics.py`.

### 4.3 Day 3 — Perf instrumentation

**Code change in `Demo3D::hybridDispatchCorrection`:**

Wrap both dispatches in `GL_TIMESTAMP` queries:

```cpp
GLuint queries[3]; glGenQueries(3, queries);
glQueryCounter(queries[0], GL_TIMESTAMP);
glDispatchCompute(...); // hybrid_correction
glQueryCounter(queries[1], GL_TIMESTAMP);
glDispatchCompute(...); // hybrid_blur
glQueryCounter(queries[2], GL_TIMESTAMP);
// Read async next frame; store EMA into hybridCorrectionMs + hybridBlurMs
```

Display in the perf metrics panel (already exists; gated on `showPerformanceMetrics`).

Then run at 720p + 1080p + 1440p with cornell + sponza, record numbers.

**Implementation cost:** ~50 lines C++. EMA-smoothed timer pattern reused from existing `raymarchTimeMs` / `cascadeTimeMs`.

### 4.4 Day 4 — Run the suite

Two driver invocations (cornell-orig + sponza), each with `--hybrid-ab-sweep` + matched `--auto-burst` triggers + headless frames.

Outputs land in `tools/hybrid_validation/cornell_orig/` and `tools/hybrid_validation/sponza_cam_md/`.

Generate the metrics tables. Eyeball the captured PNGs for qualitative defects (banding, ghosting, dimming) not caught by per-pixel RMSE.

### 4.5 Day 5 — Decision gate

Three concrete branches based on measured RMSE_GI residual vs PT_GI:

| Outcome | Action | Doc to produce |
|---|---|---|
| Variance-merge RMSE < 10% AND perf < 25 ms at 1080p | Ship v1.2 final | `hybrid_v12_final_status.md` |
| Variance-merge RMSE 10-25%, gap concentrated in MB-dominant regions | Spec cascade-MB-delta v2 | `hybrid_v2_cascade_mb_delta_plan.md` |
| Variance-merge RMSE > 25% OR qualitative defects (banding, ghosting) | Investigation phase | `hybrid_v12_investigation_log.md` |

Decision is **measurement-driven, not opinion-driven** (cerebrum L9 anti-anti-pattern).

## 5. Success criteria

### Hard requirements
- [ ] `--hybrid-ab-sweep` driver lands and produces the five PNGs reliably
- [ ] Metrics script produces a parseable JSON + markdown table
- [ ] Timer-query perf numbers logged at 720p + 1080p
- [ ] Suite runs on cornell-orig AND sponza-master
- [ ] Decision gate doc with one of the three branches chosen, with evidence

### Honest framing
- [ ] No "ship it" without quantified RMSE evidence (L9 anti-pattern)
- [ ] Per-pixel metric AND mean-brightness metric (mode 18 vs 19 lesson: averages lie)
- [ ] Each capture's matching sample budget is the SAME (PT spp = hybrid spp), otherwise apples-to-oranges
- [ ] If variance-merge fails one scene but passes the other, BOTH must be documented; don't cherry-pick

## 6. Risks

- **R1: Screenshot determinism.** Stochastic hybrid noise differs across runs even with fixed RNG seed (frame timing affects accumulator state). Mitigation: capture at fixed frame number after warmup, OR average over N consecutive captures.
- **R2: PT itself isn't fully converged at matched spp.** PT at 60 frames × 1 ray = 60 spp is high-variance. If we use it as "truth" the metric noise is huge. Mitigation: PT capture uses higher spp (e.g., 600) for ground truth; hybrid uses the operational ~60.
- **R3: Sponza cam.md camera may not stress-test the failure mode the user described.** Mitigation: also capture 2-3 alternate viewpoints; user-guided viewpoint selection if cam.md doesn't surface the issue.
- **R4: Variance-merge defaults (cascadeVar=0.001, confidence=8) may need scene tuning.** Mitigation: include `tune_grid_search.json` step — try cascadeVar ∈ {0.0001, 0.001, 0.01, 0.1} per scene, pick best per scene, document optimum.

## 7. What this UNBLOCKS

- If A (ship): can close hybrid feature, focus elsewhere (specular GI, dynamic scenes, perf optimization)
- If B (spec v2): plan has concrete RMSE residual as its size justification — no hand-waving
- If C (investigate): the captured per-region RMSE pinpoints where the math is wrong, so investigation is bounded

## 8. What this does NOT cover

- Specular / glossy GI (out of scope, hybrid is diffuse-only)
- Animated scenes (PT reference assumes static camera + scene)
- VR / multi-view (single-camera only)
- Memory measurement (3× half-res RGBA32F is ~8 MB at 1080p; trivial, not worth measuring)

## 9. Files to produce in this phase

| File | What |
|---|---|
| `src/main3d.cpp` | +80 lines: `--hybrid-ab-sweep` driver |
| `src/demo3d.cpp` | +50 lines: GL_TIMESTAMP query wrappers + EMA timer state |
| `src/demo3d.h` | +4 lines: `hybridCorrectionMs`, `hybridBlurMs` state |
| `tools/analysis/hybrid_quality_metrics.py` | NEW, ~120 lines |
| `doc/7/hybrid_v12_validation_report.md` | NEW, generated from the sweep results |
| Decision-gate doc per §4.5 outcome | NEW |

## 10. Out-of-scope items deliberately punted

- **Per-pixel cascade variance estimation** (J3 from v1.2.1 critic). The constant prior is a known approximation; promoting it to spatially-varying needs its own design phase.
- **Bilateral kernel optimization** (J6 from v1.2.1 critic). Separable kernel or 5×5 with tighter sigmas could halve perf cost; addressed only IF Day 3 measurements show >25 ms at 1080p.
- **Plan v2 (separate cascade-MB-delta dispatch).** Specified ONLY IF Day 5 decision picks branch B.
- **OBJ-mode analytic-SDF parity** (I7 from impl notes). Hybrid uses grid SDF either way; analytic mode is a debug mode, not a hybrid use case.

---

## 11. Self-critic findings — Phase 8 plan (rev 2 changes)

Reviewed the plan above as if it were a critic round. 4 findings; severity classified.

### K1 (HIGH) — "Matched sample budget" comparison is ill-defined

**Original §3 said:** "merge OUGHT to approximate `principled`... measurement: per-pixel RMSE."

**Problem:** I never specified how `principled` is captured. Plan v2 (the principled fix) isn't implemented yet. So the comparison can't be "merged vs principled" without first building v2 — which is what this phase is supposed to AVOID until measurement justifies it.

**Fix:** Replace "vs principled" with "vs PT-reference at converged spp" as the ground truth. `principled` is a future hypothetical; PT is the existing reference. Branch B's decision is "is the residual concentrated in patterns cascade-MB would help" (qualitative pattern-matching from §4.2 per-region RMSE), not a head-to-head numeric comparison.

**Doc updated:** §3 paragraph rewritten in rev 2 (this commit).

### K2 (HIGH) — Screenshot determinism risk under-mitigated

**Problem (R1):** I noted the risk but Mitigation "capture at fixed frame number after warmup" is fragile — accumulator state at frame F depends on what FRAME F MINUS K looked like, which depends on shader timing, which is non-deterministic. Two runs at "frame 60 after warmup" will differ.

**Fix:** Require captures be **averaged over N=10 consecutive frames** at the FINAL converged state. Adds 10 frames to capture time. Eliminates per-frame stochastic flicker from the metric. Also: report standard deviation of the 10 captures as a noise floor — any RMSE < that noise floor is not a real signal.

**Doc updated:** §4.1 and §6 R1 in rev 2.

### K3 (MEDIUM) — Decision gate thresholds are arbitrary

**Problem (§4.5):** "10% RMSE" and "25 ms at 1080p" are pulled from thin air. What's the actual user requirement?

**Reframing:**
- RMSE 10% threshold: empirical — Cornell PT_GI mean is ~0.076, so 10% RMSE = ~0.0076 absolute = barely visible (≤ 1 luminance step at 8-bit). Defensible BUT only as "visually indistinguishable from PT," not as "matches all users' quality bars."
- Perf 25 ms 1080p: current frame budget at 720p with cascade is ~16 ms; +25 ms hybrid = ~41 ms = ~24 fps at 1080p. That's below the 30 fps interactive bar.

**Fix:** Recast thresholds with explicit justification + add "soft fail" tiers (i.e., RMSE 10-15% = "good enough for static viewer; degraded for interactive"; RMSE > 25% = "needs investigation"). Don't pretend the thresholds are sharp.

**Doc updated:** §4.5 table reframed with tiered language in rev 2.

### K4 (MEDIUM) — Day 5 might not actually fit in 1 day

**Problem (§2):** I wrote "Day 5: Decision gate" as if writing a decision doc takes 1 day. Actually if branch B is picked, that day-5 is "spec the v2 plan" which is its own multi-day effort.

**Fix:** Day 5 is "EVALUATE measurements + DECIDE branch + write SHORT decision doc (1-2 pages)." If branch B/C, the FOLLOW-UP plan/investigation is its own phase, not part of Phase 8's day budget. Documented explicitly.

**Doc updated:** §2 day budget refined; §4.5 distinguishes decision-doc from follow-up-plan.

### K5 (LOW) — Risk R4 (defaults tuning) could itself be a sub-phase

**Problem (§6 R4):** "tune_grid_search.json" implies a grid search over cascadeVar values, but that's another sub-experiment that adds days.

**Fix:** Scope R4 as "Day 4 includes ONE retune iteration if Day-2 defaults look obviously off" — i.e., a quick eyeball-tune, not a systematic grid search. Systematic tuning gets its own follow-up if needed.

**Doc updated:** §6 R4 in rev 2.

### K6 (LOW) — Mode 19 metric duplication

**Problem (§4.2):** I have both `rmse_vs_pt_gi_only` AND `blue_pixel_count` for mode 19. These measure overlapping things — blue pixel count is essentially a thresholded version of the RMSE.

**Fix:** Keep both. They serve different purposes: RMSE is the scalar quality metric; blue pixel count is the visual A/B at-a-glance number that users (and prior cerebrum entries) already think in. Defensible duplication.

**No doc change needed.**

---

## 12. Honest framing (rev 2 vs rev 1)

| Aspect | Rev 1 (over-claim) | Rev 2 (after self-critic) |
|---|---|---|
| Ground truth | "vs principled v2" (didn't exist) | PT-reference at converged spp |
| Determinism | "fixed frame after warmup" | 10-frame averaged capture + noise floor |
| Thresholds | "< 10% RMSE" as if sharp | Tiered: < 10% = visually-indistinguishable; 10-25% = sub-bar; > 25% = investigate |
| Day 5 scope | "Decision gate" (vague) | Decision doc (1-2 pages); follow-up plan is separate phase |
| Tuning | implied grid search (R4) | One eyeball-tune iteration in Day 4 |

## 13. What's the value of doing this plan vs. just implementing v2

If we skip validation and just implement plan v2 (the principled fix) directly:
- 200-300 lines of new code (separate cascade dispatch, MB-delta texture, composition rewrite)
- ~15-30 ms additional bake cost
- We never learn whether v1.2 was actually broken — we just assume

If we run Phase 8 first:
- Branch A: Phase 8's 4-6 days SAVES the v2 effort entirely
- Branch B: Phase 8's measurements GUIDE v2's design (which regions need it most → can localize the dispatch)
- Branch C: Phase 8's per-region RMSE points at WHAT'S wrong in v1.2 before any v2 work

The validation cost is bounded; the savings (or guidance) are unbounded. Measurement-first dominates.
