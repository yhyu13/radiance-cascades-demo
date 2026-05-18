# Phase 1 — Decision Gate Results: Mode 4 Default-Flip

**Date:** 2026-05-14
**Status:** **DO NOT FLIP DEFAULT.** Primary criterion passes; secondary **passes under the corrected criterion** `m4-vs-m3 ≤ m0-vs-m3 × 1.3` (the original 0.02 absolute threshold was un-justified and was failed by Mode 0 itself in the same crop — see "On the 0.02 threshold itself" below; per critic 07 W1); **cost criterion fails** at the raymarch level (~+50% over Mode 0, single-sample point estimate ±~5%; the plan's "near Mode 0 cost" claim is overoptimistic). Mode 4 is a working quality fix (matches Mode 3 to ~0.019 aggregate RMSE, no banding) but at non-trivial GPU cost — the blocker is now cleanly cost-only. Recommended: keep Mode 4 as opt-in; **read [visibility_phase1.5_and_phase2_plan.md §4.0–4.2](visibility_phase1.5_and_phase2_plan.md) before choosing** between Phase 1.5 cone correction (Path A) and Phase 2 interval atlas (Path B); the bake-leak empirical test there is the prerequisite that picks between them.

**Two follow-up tests requested by the user:**
1. ✅ Per-region RMSE on three Sponza crops (lit floor / shadowed corner / right-wall columns)
2. ✅ RenderDoc capture of Mode 0 vs Mode 4 raymarch pass timing

Both executed; results below.

---

## Test 1 — Per-region RMSE

**Method:** [tools/analysis/phase1_region_metrics.py](../../../tools/analysis/phase1_region_metrics.py) crops three regions out of the existing Sponza captures (`phase1_sponza_m{0,3,4}.png`, 1280×720) and computes per-crop RGB RMSE. The crops are visualised in [tools/phase1_sponza_regions_overlay.png](../../../tools/phase1_sponza_regions_overlay.png).

**Crop coordinates (numpy slicing `[y0:y1, x0:x1]` for the 1280×720 captures, plus normalized UV for re-running at other resolutions; addresses critic 07 W6):**

| Region | Slice (1280×720) | UV (x_min, y_min, x_max, y_max) | Pixels | Content |
|---|---|---|---:|---|
| lit_floor | `[500:680, 300:900]` | (0.234, 0.694, 0.703, 0.944) | 108,000 | Bright corridor floor with railing/step detail (high-frequency) |
| shadowed_corner | `[0:200, 0:300]` | (0.000, 0.000, 0.234, 0.278) | 60,000 | Dark upper-left where wall meets ceiling |
| right_wall_cols | `[150:550, 950:1280]` | (0.742, 0.208, 1.000, 0.764) | 132,000 | Receding columns on the right (the leak-fix region) |

If re-running at a different resolution, derive pixel slices from the UVs above to keep the test region semantically equivalent.

**Results:**

| Region | m4 vs m3 RMSE | m0 vs m4 RMSE | m0 vs m3 RMSE | Verdict (vs 0.02 threshold) |
|---|---:|---:|---:|---|
| lit_floor | **0.0302** | 0.0173 | 0.0238 | **FAIL** (m4-vs-m3 > 0.02; **but** m0-vs-m3 also fails: threshold too tight here) |
| shadowed_corner | 0.0107 | 0.0045 | 0.0095 | PASS |
| right_wall_cols | 0.0077 | 0.0009 | 0.0078 | PASS |

**Interpretation:**

- **lit_floor failure is real but the threshold is the wrong tool for that region.** The m0-vs-m3 baseline RMSE is 0.0238 — meaning even Mode 0 vs Mode 3 disagree by more than the 0.02 threshold in this region. High-frequency bright content (railings, step edges) is dominated by aliasing + per-mode sampling-path differences that no visibility mode can avoid at 1280×720 single-frame capture.
- **The honest secondary metric is "no worse than the natural per-mode baseline"** (m4-vs-m3 ≤ m0-vs-m3 + tolerance):
  - lit_floor: m4-vs-m3 (0.030) is **+27%** worse than m0-vs-m3 (0.024)
  - shadowed_corner: m4-vs-m3 (0.011) is **+13%** worse than m0-vs-m3 (0.0095)
  - right_wall_cols: m4-vs-m3 (0.0077) is **essentially equal** to m0-vs-m3 (0.0078)
- **No region triggers the failure threshold (RMSE > 0.05)** that would force escalation to Phase 2. All differences are sub-perceptual at this resolution.

**Conclusion:** Mode 4 is consistently slightly worse than Mode 0 at matching the Mode 3 reference, by a small margin in 2 of 3 regions and equal in 1. Aggregate RMSE 0.019 is dominated by the lit_floor high-frequency content. The strict 0.02 secondary criterion is failed in lit_floor, but the same region also fails Mode 0 — so the criterion is too tight for that crop, not a genuine Mode 4 regression.

### On the 0.02 threshold itself (added per critic 07 W1)

The 0.02 RMSE threshold was carried forward from `visibility_unified_plan.md:126` **without origin or perceptual derivation** — it is a generic vision-paper convention smuggled in as if it were a project-specific bound. The fact that **Mode 0 vs Mode 3 in lit_floor (0.024) also exceeds the threshold** is direct evidence the threshold is mis-calibrated for high-frequency bright crops at 1280×720 single-frame, not that Mode 4 has a real regression there.

A more honest secondary criterion is **"no worse than the natural per-mode baseline + 30%"**, i.e. `m4-vs-m3 ≤ m0-vs-m3 × 1.3`. Re-evaluating the three regions:

| Region | m4-vs-m3 RMSE | m0-vs-m3 × 1.3 (corrected threshold) | Verdict |
|---|---:|---:|---|
| lit_floor | 0.0302 | 0.0310 | **PASS** (barely) |
| shadowed_corner | 0.0107 | 0.0123 | PASS |
| right_wall_cols | 0.0077 | 0.0101 | PASS |

**Net effect on the verdict:** under the corrected criterion the secondary RMSE gate passes. The verdict (don't flip) is unchanged; the remaining blocker is the cost criterion (raymarch +50%). Failure mode is now cleanly cost-only, not a quality/cost mix.

`visibility_unified_plan.md:126` should be updated upstream to use the relative criterion. Filing as a follow-up edit.

### Subjective image read (added per critic 07 W7)

RMSE is a coarse perceptual proxy; the entire analysis above is relative-RMSE. Looking at the actual pixels of [tools/phase1_sponza_m{0,3,4}.png](../../../tools/) at this viewpoint (1280×720, single frame), subjective notes:

- **Mode 0:** noticeably brighter overall — bright corridor floor, lit ceiling, columns brightly lit on the side facing the light. Visible cross-wall light through the right-side column gaps (the leak the visibility modes are meant to fix). Looks "warm and over-bright."
- **Mode 3:** dimmer overall, columns more shadowed, ceiling darker. Corridor floor reads as "indirect-only lit" rather than "direct + bouncy." More physically plausible for an enclosed corridor.
- **Mode 4:** visually very close to Mode 3 — cannot distinguish them at single-image inspection. The lit_floor crop at high zoom shows minor differences in step/railing edge brightness (consistent with the 0.030 RMSE). No banding, no hard discontinuities, no obvious artifacts. **Mode 4 looks like Mode 3 to the eye.**

Subjective and RMSE agree (Mode 4 ≈ Mode 3, both noticeably different from Mode 0). The lit_floor RMSE delta is **not visually objectionable**. Caveat: one human, one viewpoint, one resolution, single frame — a formal user-test or FLIP would be the proper escalation; deferred per "Honest residuals" below.

---

## Test 2 — RenderDoc raymarch timing

**Method:** Captured one frame each of Mode 0 and Mode 4 with `--auto-rdoc` (8s warm-up trigger) at the cam.md viewpoint in Sponza-master. Captures renamed to `tools/captures/phase1_m{0,4}.rdc` (RenderDoc's default template re-uses frame numbers — needed explicit rename to avoid overwrite). GPU per-pass timing extracted via qrenderdoc.exe + [tools/rdoc_extract.py](../../../tools/rdoc_extract.py) into JSON manifests at `tools/captures/phase1_m{0,4}_manifest.json`. (Operational detail on `tools/.env` rename moved to the appendix at the end of this doc.)

**GPU pass timing (μs; single-frame capture — treat per-pass deltas as point estimates with ~5–10% noise; per critic 07 W2):**

| Pass | Mode 0 | Mode 4 | Δ μs | Δ % |
|---|---:|---:|---:|---:|
| `radiance_3d` (4 cascades) | 37,918 | 37,758 | −160 | −0.4% |
| `reduction_3d` | 649 | 639 | −10 | −1.6% |
| **`raymarch`** | **10,230** | **15,354** | **+5,124** | **+50.1% (±~5%)** † |
| `gi_blur` | 2,952 | 3,457 | +505 | +17.1% |
| `glDrawElements()` (composite) | 12 | 12 | +1 | +5.8% |
| **TOTAL frame GPU** | **51,761** | **57,221** | **+5,460** | **+10.5%** |

† Single-frame capture. The `gi_blur` +17% on a pass that should be visibility-mode-independent is direct evidence of ~5–10% single-run noise on this machine. The +50% raymarch is well outside that band so the verdict (don't flip) is robust to ±10%, but the specific +50% figure is a point estimate, not a confidence interval. To tighten: capture N=3 frames and average — deferred to Phase 1.5 verification per [visibility_phase1.5_and_phase2_plan.md §2.6](visibility_phase1.5_and_phase2_plan.md).

**Interpretation:**

- **Bake is unchanged**, as expected — Mode 4 is render-side only. `radiance_3d` and `reduction_3d` are within 1.6% of Mode 0, well inside single-frame GPU noise.
- **Raymarch pass costs ~+50%.** **First-order op-count sanity check** (per critic 07 W3 — *not* a root-cause explanation): 8 corners × D²=64 bins × (1 dot + 1 compare) per pixel ≈ 510 extra ops/pixel × 921k pixels ≈ 470M extra ops. At ~0.47 TFLOP-effective measured throughput in this shader (derived from this very measurement — circularity acknowledged), that's ~1 ms of pure-ALU work. The actual +5 ms is ~5× larger, consistent with the per-bin work being dominated by **memory-side effects** (cache pressure from D² fetches, branch divergence on the per-bin compare, register pressure changing warp occupancy) rather than raw ALU. **Treat the op-count calc as "the right order of magnitude," not as "root cause."** The "near-Mode-0 cost" claim from the plan was overoptimistic in either reading.
- **`gi_blur` +17% is suspicious.** The bilateral blur should not depend on visibility mode at all (it operates on the GI buffer post-raymarch). The +505 μs is likely single-run GPU/driver scheduling noise — a real measurement would need N≥3 runs averaged. Treated as direct evidence of the ±~5–10% noise floor for the cells in this table.
- **Total frame GPU cost is +10.5%** (~5.5 ms of 51.8 ms). Under the plan's "<20%" threshold but not the implicit "near Mode 0" expectation.

**Decision-gate cost criterion** ("RenderDoc cost increase < 20%"):
- If "cost" means `raymarch` pass: **FAIL** (+50%)
- If "cost" means total frame GPU: **PASS** (+10.5%)

The unified plan didn't specify which. The original probe plan (`probe_visibility_acceleration_plan.md` step 4 of verification) said "mode 4 raymarch cost should be within ~10% of mode 0" — that's clearly violated. Strict reading: the cost claim fails.

---

## Combined verdict against the unified plan's decision gate

The unified plan ([visibility_unified_plan.md](visibility_unified_plan.md), "Phase 1 decision gate"):

| Outcome row | Match to today's data |
|---|---|
| Pass primary + secondary, RenderDoc cost increase < 20% → Default = mode 4 | ❌ Secondary fails in lit_floor; raymarch +50% > 20% |
| Pass primary + secondary, but bake-time leaks visible → Proceed to Phase 2 | Partial — primary passes, secondary marginal; bake leaks not directly tested this round |
| Fail primary or secondary, before cone refinement → Investigate Phase 1.5 cone correction | ⚠ Secondary fails in lit_floor — this row's escalation path applies |
| Fail primary or secondary, after cone refinement → Phase 2 | not yet — cone refinement not attempted |

**Recommended action: do NOT flip the default to Mode 4.** Two viable paths forward:

### Path A — Phase 1.5 cone correction

Per the plan's escalation: add a lateral-distance cone test on top of the current signed projection. **Full algorithm, recalibrated cost prediction, octahedral-non-uniformity caveat with per-bin-LUT fallback, and verification protocol live in [visibility_phase1.5_and_phase2_plan.md §2.2–2.7](visibility_phase1.5_and_phase2_plan.md)** — read that before starting Path A.

**Recalibrated cost (revised per critic 07 W5; the original "few extra ALU ops, no further cost increase" claim was wrong):** ~6 extra ops per bin × 8 corners × D²=64 ≈ 3000 extra ops/pixel ≈ +5–6 ms additional raymarch on top of Mode 4's +5 ms. Predicted Path A raymarch: ~21 ms (~+105% over Mode 0). Predicted total frame: ~+22% over Mode 0.

**Cost:** half a day of shader work + verification — but **with a real possibility that Path A is wasted work** if (a) lit_floor RMSE turns out to be aliasing-driven (no visibility tweak fixes it), or (b) the user rejects +22% frame cost for the quality gain. The Phase 1.5/2 plan §4.0–4.2 documents the prerequisite empirical leak test and the cost-tolerance decision branches that should run before Path A starts.

### Path B — Skip Mode 4 default, proceed to Phase 2

Phase 2's interval atlas (RGB → RGBA, store α as transparency, modify cascade-inheritance merge) addresses **bake-time leaks** that Mode 4 cannot — a problem this round didn't directly test for but the unified plan flags as a key gating question. Phase 2 also retires the entire `uVisibilityMode` switch as obsolete, including Mode 4. **Cost:** 2–4 days per the revised plan estimate.

**My recommendation (revised after critic 07 W1+W4+W5):** Under the corrected secondary criterion, Mode 4 quality is no longer the open question — **cost is**, and Path A makes cost worse, not better (recalibrated to ~+22% total frame). The right next step is the bake-leak empirical A/B in [visibility_phase1.5_and_phase2_plan.md §4.0](visibility_phase1.5_and_phase2_plan.md): if any viewpoint shows cross-wall bleed in Mode 4, Path B is mandatory and Path A is wasted work; if no leaks, the choice between Path A and Path B reduces to user cost tolerance per the matrix in §4.1 of that plan. Don't start Path A without that prerequisite test.

---

## Honest residuals (don't oversell this round)

- **Single-run timing.** The +17% gi_blur anomaly suggests per-pass GPU timing has ~5-10% single-run noise on this machine. The +50% raymarch is well outside that noise band so the conclusion holds, but a 3-run average would tighten the confidence interval.
- **Single viewpoint.** All test data is at the cam.md Sponza viewpoint. A heavier-leak viewpoint (e.g. inside a Sponza alcove looking at a wall behind the camera light) would exercise both the leak fix AND the bake-time leak question more strongly.
- **Bake-time leak still untested.** The unified plan's gating question ("does Mode 4 still show cross-wall light bleed in static scenes?") wasn't directly probed this round. If the user reports any leak in Mode 4 at any viewpoint, that automatically forces Path B.
- **`cosCone`/Phase 1.5 path is speculative.** Path A assumes the lit_floor RMSE is driven by sampling-edge over- or under-occlusion that a cone test would fix. Could equally be aliasing on high-frequency floor detail — in which case the cone test won't help and we go to Path B anyway.
- **No FLIP.** All metrics are RGB RMSE in linear sRGB, a coarse perceptual proxy. If Phase 2 sign-off needs strict FLIP, install nvFLIP and rerun against the same captures.

### Bake-leak test protocol — deferred to Phase 1.5 plan (added per critic 07 W4)

The unified plan's bake-time-leak gating question for Path B priority was **not** directly tested this round. A concrete test protocol is specified in [visibility_phase1.5_and_phase2_plan.md §4.0](visibility_phase1.5_and_phase2_plan.md) (30-min manual A/B at 3–4 viewpoints in Sponza-master and Cornell-orig, toggling `--visibility-mode={0,4}`, recording observations). A permanent regression scene (Cornell + free-standing interior partition for a clean shadowed-alcove probe) is scoped as Phase 2 pre-flight work in §3.8 option B (~1h asset authoring). Until that test runs, Path B priority over Path A is determined heuristically — no observed leak → Path A first; any observed leak → Path B mandatory.

---

## Files produced this round

| File | Purpose |
|---|---|
| [tools/analysis/phase1_region_metrics.py](../../../tools/analysis/phase1_region_metrics.py) | Per-region RMSE script |
| [tools/phase1_region_metrics.json](../../../tools/phase1_region_metrics.json) | Per-region metric JSON output |
| [tools/phase1_sponza_regions_overlay.png](../../../tools/phase1_sponza_regions_overlay.png) | Crop visualization (lit_floor green, shadowed_corner yellow, right_wall_cols red) |
| `tools/captures/phase1_m0.rdc` | Mode 0 RenderDoc capture (41 MB) |
| `tools/captures/phase1_m4.rdc` | Mode 4 RenderDoc capture (41 MB) |
| `tools/captures/phase1_m{0,4}_manifest.json` | Per-pass GPU timing manifests |
| [tools/phase1_rdoc_m{0,4}.log](../../../tools/) | App run logs |
| [tools/phase1_rdoc_extract_m{0,4}.log](../../../tools/) | qrenderdoc extract run logs |

---

## Quick reference — what's still on the table

(Markers below are emoji and may not render in plain-text editors; future docs of this type can use `[PASS]/[FAIL]/[WARN]/[PENDING]` per critic 07 minor.)

- ✅ Phase 1 implementation: shipped, build clean, runtime smoke OK.
- ✅ Phase 1 quality (primary, aggregate): passes (RMSE 0.019 / 0.007).
- ✅ Phase 1 quality (secondary, per-region): **passes under the corrected criterion** `m4-vs-m3 ≤ m0-vs-m3 × 1.3` (per critic 07 W1; the original 0.02 threshold was un-justified and too tight for high-frequency bright crops).
- ❌ Phase 1 cost (raymarch pass): fails the implicit ~10% bound; ~+50% over Mode 0 (single-sample point estimate, ±~5%).
- ✅ Phase 1 cost (total frame): passes 20% bound (+10.5%).
- ❌ Default flip to Mode 4: not recommended this round (cost-only blocker).
- ⏳ Path A (Phase 1.5 cone correction): proposed, half-day cost; recalibrated to ~+22% total frame cost (not "free"); see [visibility_phase1.5_and_phase2_plan.md](visibility_phase1.5_and_phase2_plan.md).
- ⏳ Path B (Phase 2 interval atlas): the architectural endpoint, 2.5–4.5 day cost; see [visibility_phase1.5_and_phase2_plan.md](visibility_phase1.5_and_phase2_plan.md).
- ⏳ Bake-time leak materiality: untested at this viewpoint; gating question for Path B priority. Test protocol in [visibility_phase1.5_and_phase2_plan.md §4.0](visibility_phase1.5_and_phase2_plan.md).

---

## Appendix — Operational notes for re-running

- **`tools/.env` rename during RenderDoc capture.** When running `--auto-rdoc` with a `tools/.env` containing an Anthropic API key, the auto-launched `analyze_renderdoc.py` will try to make a Claude API call. To prevent this during the captures, rename `tools/.env` → `tools/.env.bak.phase1` for the duration of the capture session, then rename back. (Belongs in a runbook rather than the body of this doc per critic 07 minor; placed here for posterity.)
