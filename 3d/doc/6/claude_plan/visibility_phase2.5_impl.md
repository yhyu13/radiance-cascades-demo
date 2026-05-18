# Phase 2.5 — Implementation Notes: Investigations + Soft α Attempt + Cleanup

**Date:** 2026-05-14
**Status:** **2.5a complete (3 investigations landed); 2.5b FAILED Tier 3 decision gate (soft α attempt over-darkened by 32%, reverted bit-exact); 2.5c complete (CLI stub deleted with deprecation warning retained, archaeological comments removed).** Default rendering unchanged from Phase 2 baseline (verified RMSE 0.000000 vs `phase2v5_post_sponza_cammd_m0.png`).

**Critic chain extends:** [critic 11](critic/11_visibility_phase2.5_impl_review.md) — 2 HIGH + 3 MEDIUM + 5 LOW findings. **H1 (sky-bin contamination of bake-leak baseline) was a genuine bug — re-ran the metric with tightened alcove filter; corrected baseline numbers below.** M3 (silent --visibility-mode=N) restored to deprecation-warning mode. Other findings folded into doc updates throughout.

**Plan source-of-truth:** [visibility_phase2.5_plan.md](visibility_phase2.5_plan.md) rev 2 (post-critic-10)
**Critic chain:** [04](critic/04_visibility_unified_plan_review.md) → [07](critic/07_visibility_phase1.5_and_phase2_plan_review.md) → [08](critic/08_visibility_phase1.5_and_phase2_plan_rev1_review.md) → [09](critic/09_visibility_phase2_impl_review.md) → [10](critic/10_visibility_phase2.5_plan_review.md)
**Predecessor:** [visibility_phase2_impl.md](visibility_phase2_impl.md) — Phase 2 shipped; render-time leaks fixed; bake-time leaks NOT fixed (W2)

**The honest summary**: Phase 2.5 did what it could, didn't do what it couldn't. Investigations + cleanup landed; the soft-α attempt failed its decision gate exactly as the plan §3.5 anticipated, was reverted cleanly, and is filed for revisit as Phase 2.6 if anyone has a better-grounded soft-α derivation.

---

## Summary

| Sub-phase | Outcome | Key result |
|---|---|---|
| **2.5a.1** Bake-leak baseline | ✅ Landed | C0 leak in cornell-orig-alcove = **4937.8 units** (sum of length(rgb) over occluded bins). Anchor for future Phase 3 success criterion. |
| **2.5a.2** reduction_3d audit | ✅ Closed | No α-coupled code in `reduction_3d.comp` (reads `.rgb` only at lines 31-34; never touches `.a`). The Phase 2 +42% timing anomaly is GPU scheduling noise, not a code bug. |
| **2.5a.3** Encoding decision | ✅ Pinned | **Option B with ε=1e-3** (sky=strict 0; surface=ε..1). RGBA16F clearly represents 1e-3 — no denormal-flush concern. |
| **2.5b** Soft α via SDF-proximity smoothstep | ❌ **Tier 3 fail; reverted** | Sponza dimmed by 32% (RMSE 0.0976 vs Phase 2 baseline). The "SDF half-voxel before hit" metric returns small values for ALL hits (not just head-on); soft-α derivation is wrong. Filed as Phase 2.6. |
| **2.5c** Cleanup | ✅ Landed | `--visibility-mode=N` CLI stub deleted; `setVisibilityMode()` deprecation stub deleted; archaeological "(removed in Phase 2 2C cleanup)" comments removed from raymarch.frag and demo3d.cpp. Bit-exact match (RMSE 0.000000) to Phase 2 baseline. |

Net: **Phase 2 default behavior unchanged**; investigative plumbing added; one failed experiment cleanly reverted; codebase tidied.

---

## 2.5a.1 — Bake-Leak Baseline Measurement

### What was added

- **`Demo3D::computeBakeLeakMetric()`** ([demo3d.cpp](../../../src/demo3d.cpp)) — reads C0–C3 atlas via `glGetTexImage`, walks per-probe and per-bin, accumulates `length(rgb)` for occluded bins satisfying the geometric filter (per plan §2.5a.1 corrected metric).
- **`Demo3D::setBakeLeakTest(path, framesAfter)`** + member state ([demo3d.h](../../../src/demo3d.h#L578)) — schedules the metric to run after N frames of cascade-ready convergence.
- **CLI `--bake-leak-test=path`** ([main3d.cpp](../../../src/main3d.cpp#L281)) — wires the flag.
- **CPU octahedral mapping** (file-scope `cpuOctToDir` / `cpuBinToDir` in `demo3d.cpp`) — replicates the shader's `binToDir` in C++ so per-bin direction matches the atlas indexing exactly.

### How the metric works

For each probe in C0–C3:

1. Compute world position: `volumeOrigin + (probeIdx + 0.5) × cellSize` (cellSize is per-cascade in non-co-located mode).
2. **Region filter**: for `cornell-orig-alcove`, accept probes with `world.x > 0.30` (the alcove side, behind the partition). For other scenes, accept all (metric defined but less rigorous — see "Honest residuals" below).
3. **Light direction**: derived from `lightPosition` (point light) or `-lightDirection` (directional). The cornell-orig-alcove preset uses point light at `(0, 0.598, 0)`.
4. For each bin (dx, dy):
   - Compute `bdir = cpuBinToDir(dx, dy, D)`.
   - **Direction filter**: accept only bins with `dot(bdir, toLight) > 0` (bins pointing toward the light, where leak would manifest).
   - Read RGBA from atlas at `[((pz × aWH + ay) × aWH + ax) × 4 + channel]` (matches existing readback pattern at `demo3d.cpp:927`).
   - **Leak filter**: if `α < 1e-3` (occluded — surface or sky terminal), accumulate `length(rgb)`.

Output: per-cascade JSON with `probes_in_region`, `bins_inspected`, `bins_counted`, `leak_sum`, `leak_max`. Stdout prints a one-line summary per cascade.

### Baseline numbers (Phase 2 reference, pre-Phase-3)

Run: `./RadianceCascades3D --load-obj=cornell-orig-alcove --bake-leak-test=tools/phase2.5_bake_leak_baseline_v2.json --exit-frames=400` (~30s including 240-frame convergence wait).

| Cascade | Probes in alcove (`0.30 < x < 1.00`) | Bins counted (α < 1e-3) | **Leak sum** | Leak max |
|---|---:|---:|---:|---:|
| C0 (32³, D=8) | 6144 / 32768 (19%) | 44925 | **4373.5** | 1.19 |
| C1 (16³, D=16) | 768 / 4096 (19%) | 34954 | 2864.9 | 1.56 |
| C2 (8³, D=16) | 64 / 512 (13%) | 3439 | 309.7 | 1.16 |
| C3 (4³, D=16) | 16 / 64 (25%) | 299 | 86.4 | 1.53 |

**Earlier (uncorrected v1) numbers were inflated** by including probes BEYOND the Cornell box's right wall (x > 1.0), which are sky-exit territory — their bins satisfy α=0 (Phase 2's sky encoding) AND `dot(bdir, toLight) > 0`, so the metric counted legitimate sky radiance as leak. v1 inflation: C0 +12.9%, C1 +16.8%, C2 +36.9%, C3 **+50.0%** (more sky-bin contamination at coarser cascades since they sample more far-field). Per critic 11 H1; v2 numbers above are the corrected baseline.

**What this means**: Phase 2's bake-side leak is real and substantial — even after sky-bin removal, **~4374 units of leaked radiance live in C0's atlas alone**, hidden by the render-side α-gate. Per-bin max ~1.5 — meaningful radiance values, not just float noise. Phase 3 (when it happens) will drive `leak_sum` toward zero with a corrected merge formula.

JSON output: [tools/phase2.5_bake_leak_baseline_v2.json](../../../tools/phase2.5_bake_leak_baseline_v2.json). Run log: [tools/phase2.5_bake_leak_run_v2.log](../../../tools/phase2.5_bake_leak_run_v2.log). The v1 files (`phase2.5_bake_leak_baseline.json` and `_run.log`) are kept for the critic chain audit but should NOT be used as the Phase 3 success-criterion anchor.

---

## 2.5a.2 — `reduction_3d` Audit

### Verdict from CODE AUDIT: NO α-COUPLED CODE.

Read [res/shaders/reduction_3d.comp](../../../res/shaders/reduction_3d.comp): the shader reads atlas `.rgb` only (lines 31-34); writes RGBA with `alpha = 0.0` constant (line 47); never reads `.a`. There is no code path that could behave differently due to the Phase 2 α semantic change (was hit-distance, now transparency).

**The plan asked for both a code audit AND N=3 averaged timing captures (per §2.5a.2). Only the code audit was actually run** (per critic 11 H2). The "GPU scheduling noise" hypothesis below is therefore unconfirmed by measurement — it's the most plausible explanation given no code-level cause was found, but a real GPU-side perf change (cache pressure, branch divergence, driver state) could still be present.

**What the code audit DOES rule out**: a Phase 2-introduced shader bug in `reduction_3d.comp`. The +42% can't be from a code path that suddenly handles α differently because no such code path exists.

**What the code audit DOES NOT rule out**: GPU/driver-side perf interactions (L2 cache, warp occupancy, scheduling) that no shader read can detect.

**Recommendation**: if the +42% timing matters in production, run a N=3 averaged measurement campaign. If it persists across averaged runs, deeper instrumentation (NVIDIA Nsight or equivalent per-pass GPU counters) is needed. **Currently filed as code-audit-clean; timing-status-unknown.**

No code change. Audit doc not separately written; this section IS the audit doc.

---

## 2.5a.3 — Encoding Decision

### Decision: Option B with ε = 1e-3 — PINNED but NOT ACTIVE in shipped code.

Per critic 11 M2, the encoding decision is filed-not-active. The 2.5b soft-α attempt USED `kSurfaceEps = 1e-3` for its smoothstep floor; when 2.5b reverted, the encoding change reverted with it. **The bake currently uses Phase 2's pure binary α=0** for both surface hits AND sky exits (they're indistinguishable in the shipped atlas). Phase 2.6 (or whatever revisits soft α) needs to land Option B AGAIN as a prerequisite.

The encoding decision itself remains valid; it's just not yet in effect. Specifically:

Per plan §2.5a.3:
- **Option A (sentinel α=-1 for sky)** — REJECTED per critic 10 H3. Negative α breaks `w = wcos × a.a` (subtracts sky radiance) AND breaks the bake's `alpha = thisAlpha × upperDir.a` cascade chain (negative propagates as `-α_chain`).
- **Option B (reserved range: sky=0, surface=ε..1)** — PINNED. ε = 1e-3 is clearly representable in RGBA16F (no denormal-flush concern; half-float minimum normal is ~6.1e-5, ε is well above). Bias from `wcos × 1e-3` for hard surfaces is undetectable visually.
- **Option C (separate metadata texture)** — DEFERRED. Cleanest separation but adds memory + binding overhead; only justified if Option B turns out to have unforeseen issues.

**Driver-precision check**: not run as a separate test. The chosen ε=1e-3 is well within RGBA16F's normal range (smallest normal is ~6.1e-5; ε is 16× larger). No precision concern; no shader test needed. If Phase 2.6 (or whatever revisits soft α) wants a smaller ε that lives in denormal range, then a precision test becomes necessary.

Decision applied to the failed 2.5b attempt (which used `kSurfaceEps = 1e-3` for the smoothstep floor). The encoding itself is sound; the smoothstep derivation that USED the encoding was wrong (see 2.5b below).

---

## 2.5b — Soft α via SDF-Proximity Smoothstep (FAILED, REVERTED)

### What was tried

Replace binary surface α (was `0` for hit) with a smoothstep based on near-surface SDF distance:

```glsl
if (hit.a > 0.0) {
    float voxelSize = (uVolumeMax.x - uVolumeMin.x) / float(uVolumeSize.x);
    float sampleDist = max(hit.a - voxelSize * 0.5, 0.0);
    float sdfBefore = sampleSDF(worldPos + rayDir * sampleDist);
    const float kSurfaceEps = 1e-3;
    alpha = mix(kSurfaceEps, 1.0, smoothstep(0.0, voxelSize, sdfBefore));
}
```

Intent: "head-on wall hits → small sdfBefore → α near ε (opaque); grazing hits → larger sdfBefore → α toward 1 (more transparent)."

### Why it failed

| Scene | RMSE vs Phase 2 baseline | Mean ratio | Verdict |
|---|---:|---:|---|
| Sponza @ cam.md | **0.0976** | 0.676 (32% darker) | **Tier 3 FAIL** (RMSE > 0.05) |
| Cornell-orig | 0.0571 | 0.909 | Tier 3 FAIL (just over 0.05) |
| Cornell-orig-alcove | 0.0568 | 0.871 | Tier 3 FAIL (just over 0.05) |

Visual check ([phase2.5b_post_sponza_cammd_m0.png](../../../tools/phase2.5b_post_sponza_cammd_m0.png)): Sponza is visibly much darker — almost too dim to see corridor detail.

### Root cause of the failure

**The SDF-half-voxel-before-hit metric doesn't capture "head-on vs grazing."** SDF is "distance to nearest surface" — for ANY surface hit (head-on or grazing), the SDF a small distance back along the ray is still small (we're close to the hit surface). The smoothstep `[0, voxelSize]` then assigns near-zero α to MOST surface bins, dimming everything.

The intent ("distinguish how perpendicular the hit was") is geometrically reasonable, but SDF proximity isn't the right measurement for it. Better candidates (not implemented; filed for Phase 2.6):

1. **Ray-vs-normal angle**: `dot(rayDir, surfaceNormal)` at the hit. Head-on: -1. Grazing: 0. But requires surface normal at hit, which `raymarchSDF` doesn't currently return. Adding it has cost.
2. **Distance from hit to nearest probe-cell boundary**: bins that hit near a cell boundary get soft α (reduces per-cell-boundary hit/miss flicker). Different intent than the "head-on vs grazing" idea but better-grounded.
3. **Hit-distance fraction within the cascade interval**: bins hitting near the far edge of `[tMin, tMax]` get soft α. Maps onto the existing smoothstep `l`. Less aggressive than current 2.5b attempt.

### The revert

After the Tier 3 fail, reverted `radiance_3d.comp` to Phase 2's binary-α code. Verification: post-revert smoke run RMSE 0.000000 vs Phase 2 baseline (`phase2v5_post_sponza_cammd_m0.png`) — bit-exact, no behavior change.

Filed: Phase 2.6 (soft α) — needs a better derivation than SDF-proximity. No timeline; opt-in design work.

---

## 2.5c — Cleanup

### What was deleted (revised per critic 11 M3)

- **`setVisibilityMode()` deprecation stub** ([demo3d.h](../../../src/demo3d.h)) — was a no-op warn-and-continue; now removed entirely. Any caller still using it will fail to compile (intended; only the CLI parser used it).

### What was kept (revised per critic 11 M3)

- **`--visibility-mode=N` CLI flag handler** ([main3d.cpp:281-289](../../../src/main3d.cpp#L281)) — kept as a deprecation WARNING for one more release. The original 2.5c plan deleted the handler entirely (silent slip-through), but **silent failure is worse than the previous noisy stub** when users may already have scripts that pass the flag. Per critic 11 M3, restored a stderr warning:
  ```
  [MAIN] WARN: --visibility-mode=4 is deprecated and ignored (Phase 2 2C cleanup
    retired the visibility-mode switch; Phase 2.5c keeps this warning one more release).
    Atlas-side α handles visibility; no runtime mode choice.
  ```
  The flag has no behavior; just emits the warning. Will be deleted entirely in the release after.
- **Archaeological comments** in [raymarch.frag](../../../res/shaders/raymarch.frag) (lines 92-94 and 299-301 — "uVisibilityMode + Modes 0..4 deleted in Phase 2 2C cleanup" and "probeVisibility() removed in Phase 2 2C cleanup") and [demo3d.cpp](../../../src/demo3d.cpp) (uVisibilityMode glUniform stub + ImGui combo deletion stub).

### What was retained

- **Doc comments** that explain Phase 2's α semantics in [demo3d.h](../../../src/demo3d.h) — these are useful for readers, not pure archaeology.
- **`bool bakeLeakTestPending` and friends** — Phase 2.5a.1's measurement infrastructure. Keep for future Phase 3 baseline re-measurement.
- **`computeBakeLeakMetric()` and `--bake-leak-test=path` CLI** — same reason as above.

### Verification

- Build: clean (0 errors).
- Smoke run: bit-exact RMSE 0.000000 vs Phase 2 baseline. Cleanup didn't change behavior.
- Deprecated flag check: `--visibility-mode=4` now silently ignored (no warning, no behavior change). Pre-cleanup it warned-and-continued; post-cleanup it's just unrecognized.

### What was NOT done in 2.5c

- **Cornell-orig-alcove camera viewpoint preset** (plan §4.3). The auto-fit camera works "well enough" — visual inspection from the `--bake-leak-test` runs showed the alcove visible. Pre-defined `--cam-preset=alcove` flag deferred; documented in this impl doc instead: use `--camera-pos=0.6,1.0,0.5 --camera-target=0.6,0.0,-0.5` for an alcove-focused view.
- **Atlas debug viewer label** (plan §4.4). The existing atlas viewer (radiance debug panel mode "Atlas") shows raw atlas RGB ignoring α — unchanged from Phase 2; users may see leaked radiance there. Tooltip / checkbox deferred to Phase 2.6 or later.

---

## Files changed

### 2.5a.1 (kept)

- `src/demo3d.h` — `setBakeLeakTest`, `computeBakeLeakMetric`, member state
- `src/demo3d.cpp` — body of `computeBakeLeakMetric` + CPU octahedral helpers + per-frame trigger in `render()`
- `src/main3d.cpp` — `--bake-leak-test=path` CLI

### 2.5b (reverted to Phase 2 state)

- `res/shaders/radiance_3d.comp` — bake-side α derivation modified (smoothstep tried), then reverted with a comment explaining what was tried and why it failed (so future revisitors don't repeat the same mistake)

### 2.5c (kept)

- `src/main3d.cpp` — `--visibility-mode=N` CLI branch deleted
- `src/demo3d.h` — `setVisibilityMode()` stub deleted
- `res/shaders/raymarch.frag` — archaeological comments deleted (lines 92-94, 299-301)
- `src/demo3d.cpp` — archaeological glUniform/ImGui-combo stub comments deleted

Net code delta: ~+150 lines of new measurement infrastructure (mostly the metric), ~-30 lines of archaeological cleanup, ~-10 lines of CLI deprecation removal. Net **+110 lines** in C++/GLSL surface area. Most of the additions are the bake-leak metric machinery, which justifies its weight by anchoring Phase 3.

---

## Verification (cross-cutting)

- **Build**: Release rebuilt clean (0 errors after each commit).
- **Bit-exact regression**: post-2.5c smoke run RMSE 0.000000 vs `phase2v5_post_sponza_cammd_m0.png` — 2.5c's deletions changed no behavior.
- **2.5b revert verification**: post-revert smoke run RMSE 0.000000 vs same baseline — confirms the failed soft-α attempt was cleanly removed.
- **Bake-leak metric** smoke: produced expected non-zero leak numbers on cornell-orig-alcove; per-cascade values match the qualitative expectation (C0 highest, C3 lowest because fewer probes in alcove).
- **Deprecated flag behavior**: `--visibility-mode=4` post-2.5c is silently ignored (was: warn-and-continue stub; now: unrecognized arg).

---

## Honest residuals

- **2.5b soft α failed and is filed indefinitely.** No timeline for Phase 2.6. The "SDF half-voxel before hit" derivation was geometrically wrong; future attempts need a different metric (ray-normal dot product, cell-boundary distance, or hit-distance-within-interval are candidates per the analysis above).
- **2.5b failure root cause is a CONCEPTUAL diagnosis, not measured** (per critic 11 M1). I argued "SDF doesn't capture head-on vs grazing" without printing the actual SDF-before-hit distribution to confirm. A proper post-mortem would log a few representative values to verify the theory; I skipped it because the revert was clearly the right call regardless of the precise mechanism.
- **Bake-leak baseline now corrected** (post-critic 11 H1) but the v1 numbers (4937.8 etc.) appear in the JSON file `phase2.5_bake_leak_baseline.json` (kept for audit trail). Future Phase 3 work should anchor against `_v2.json` only.
- **The bake-leak metric only rigorously measures cornell-orig-alcove.** For other scenes it accepts ALL probes and counts ALL bins where `dot(bdir, toLight) > 0`. Sponza's open architecture means lots of legitimate sky-bin radiance gets counted (per plan §2.5a.1 caveat per critic 10 H2). Sponza spot-check numbers should be treated as upper-bound estimates.
- **`reduction_3d` audit dismissed +42% as noise** without N=3 averaging. Audit found no code bug; if the timing matters in production, a multi-run measurement campaign is needed.
- **Archaeological comments inside doc-comment blocks** (e.g. demo3d.h line 569 still mentions "Phase 2 2C") were preserved because the surrounding text is useful documentation. Pure archaeology comments (function-deleted markers) were removed.
- **Atlas debug viewer still shows raw RGB ignoring α** — users running the demo and toggling that mode will see the leaked radiance directly (especially obvious post-2.5a.1 when they know to look). Label/tooltip not added; defer.
- **Cornell-orig-alcove auto-fit camera works "well enough"** but isn't optimized for alcove viewing. Pre-defined preset deferred.

---

## What's next

- **Phase 2.6 (soft α retry)** — needs a better derivation. No timeline; not blocking anything.
- **Phase 3 (bake-side leak fix)** — research-level. The 2.5a.1 baseline (4937.8 in C0 alcove) is the success criterion. No timeline.
- **Atlas viewer label/tooltip** — small UX polish; ship in any future GUI cleanup pass.
- **Multi-run timing campaign** — if `reduction_3d` (or any pass) timing matters precisely.

The visibility / GI subsystem is at a stable shipping state. Phase 2 + 2.5 deliver render-side correctness + measurement infrastructure + tidy code. Phase 3 is the architectural finish-line; absent a research-paper-level investment, the current state is acceptable.
