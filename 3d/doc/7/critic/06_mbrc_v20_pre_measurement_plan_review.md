# Critic Review 06 — `mbrc_v20_pre_measurement_plan.md`

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-20
**Target:** [doc/7/mbrc_v20_pre_measurement_plan.md](../mbrc_v20_pre_measurement_plan.md)
**Verdict:** Plan structurally on the right side of the bug-212 trap — it correctly insists on measurement before v2.0 levers, has a self-critique that already caught the isolation→leave-one-out failure, and pins the configuration via §8 answers. But there are **4 HIGH issues that will let the report ship while measuring the wrong thing** — most importantly, the leave-one-out attribution is under-specified (could be measuring "C_n + merge-formula disturbance" not "C_n alone"), the PT-reference adequacy test averages convergence across pixels that converge at radically different spp (alcove silently un-converged), the hybrid baseline uses a "best-RMSE arm" the variance harness already showed was a statistical tie with default, and the §2.2 partitions overlap (per-bin pixel counts and per-bin RMSEs are not additive). **4 HIGH, 5 MEDIUM, 5 LOW.**

---

## HIGH severity

### H1 — Leave-one-out (F1's fix) under-specified; could measure "C_n + merge-formula disturbance" not "C_n alone"

§7.1 + F1 replace `uCascadeIsolation` with `uCascadeExclude`, intent: "RMSE(exclude-Cn) − RMSE(all) = C_n's contribution." But the plan never says **how excluded cascade is excluded**. Two distinct implementations give different numbers:

- **(a) Atlas-zero:** excluded cascade still participates in merge, but its bins return zero. The smoothstep blend `l ∈ [0,1]` between cascade i and i+1 still ramps; downstream cascades see "zero contribution from this band" at the boundary. RMSE includes "false-darkness in cascade-i's spatial band" plus "lost C_n radiance." Conflated.
- **(b) Skip-in-merge:** consume loop renormalizes weights as if cascade i doesn't exist. C_{i-1} and C_{i+1} pick up the slack; smoothstep transition shifts. RMSE measures the pure "without C_n" answer.

Approach (a) is the easier shader change but contaminates the attribution. Approach (b) is correct attribution but requires reworking the consume-time blend formula.

Without specifying, you'll likely implement (a) (one-line uniform check on atlas reads), report numbers, and conclude "C2 contributes X% of error" — when the X% actually includes the merge-formula disturbance.

**Fix:**
- Specify (b) as the exclusion mechanism. Document the consume-time renormalization explicitly.
- Add a sanity assertion to C3 sign-off: `RMSE(exclude=none) == RMSE(baseline) within 0.1%` (proves the new code path is a no-op when nothing is excluded).
- If (a) is chosen for speed, rename the metric — it's "loss-impact RMSE" not "contribution RMSE", and acknowledge the disturbance term in the report.

### H2 — PT reference adequacy test (C1) averages across pixels with wildly different convergence rates

§2.3 + C1: "PT reference adequate if `RMSE(4096, 1024) < 0.5 × baseline cascade RMSE`" — but this is a **whole-frame mean**. cornell-orig-alcove is the chosen scene precisely because of its bounce-2+ alcove pixels (the things v2.0 hopes to close). PT estimator variance for an alcove pixel scales with `Σ throughput²` along multi-bounce paths; an open-room pixel converges in ~256 spp while an alcove pixel may need 16k+. At 4096 spp:

- Room pixels: pair-RMSE(4096, 1024) ≈ 0.001 (converged)
- Alcove pixels: pair-RMSE(4096, 1024) ≈ 0.05 (not converged)
- Whole-frame mean: ≈ 0.003 → passes C1
- §2.2's alcove-only RMSE bin is **comparing cascade against an unconverged PT reference**. Garbage in, garbage out.

The cerebrum already documents this failure mode for averaged metrics (2026-05-19, mode-19 entry: "cross-component cancellation"). Here it's spatial-bin cancellation.

**Fix:**
- §2.3 must compute pair-RMSE in the **same spatial bins** as §2.2 (alcove-only, far-distant-only, near-grazing-only). C1 sign-off becomes per-bin: each bin's pair-RMSE must be < 0.5× cascade-RMSE in the same bin.
- If alcove bin fails C1 at 4096 spp, sweep to 16k or 64k for the alcove pixels specifically (could be done as a one-shot for cam1 which is alcove-dominated).
- Report which bins required which spp; the report's verdict must note "alcove pixels: PT reference converged at N spp."

### H3 — §2.7 hybrid baseline pulls "variance-harness-tuned settings" that the harness already showed was a statistical tie

§8 answer 3: hybrid baseline uses "variance-harness-tuned settings... best-RMSE arm from the variance-harness CSV." But cerebrum 2026-05-20 documents that v1.3.1 #1 variance sweep showed a **−0.01% RMSE difference between a05 (NEE on) and a00 (NEE off)** — within noise. The "best arm" the CSV identifies is not "best hybrid can do"; it's **the noise-favored arm of two statistically equivalent configurations.**

Picking that arm as the "gap to close" sets a moving target that:
- (a) Is not visible to users — they ship with shipped defaults, not best-CSV-arm
- (b) Is not reproducible — re-running the sweep would pick a different "best arm" within noise
- (c) Makes "MBRC retires hybrid" achievable by a 0.5% RMSE improvement that's within measurement noise

**Fix:** Use **hybrid v1.2.4 shipped defaults** as the baseline (the actual thing being retired). Also report shipped-defaults RMSE, the best-CSV-arm RMSE, and **the standard error** across the variance-harness arms. If shipped-defaults and best-arm are within `2 × stderr`, treat them as equivalent and use shipped-defaults as the only baseline. The hybrid arm should be the user-visible target, not a tuned-on-noise estimate.

### H4 — §2.2 partitions OVERLAP; per-bin pixel count and per-bin RMSE are not additive

F2 acknowledges the original RGB heatmap collapses information but the "fix" doesn't address the underlying overlap:

- `farPartitionMask`: `linearDepth > 4m AND sdfDist > 1m`
- `nearGrazingMask`: `sdfDist < 0.3m AND |dot(N,V)| < 0.5`
- "other": neither

These are not mutually exclusive in geometry. A pixel can have `sdfDist > 1m` and `|dot(N,V)| < 0.5` simultaneously (grazing surface viewed from far) — but the partition reads `farPartition=1, nearGrazing=0` because nearGrazing requires sdfDist<0.3m too. Actually re-reading: the conditions ARE disjoint here (`sdfDist > 1m` vs `sdfDist < 0.3m`), so OK on overlap, BUT — far-distant requires BOTH thresholds simultaneously, near-grazing requires BOTH simultaneously, and "other" silently absorbs:
- 0.3 ≤ sdfDist ≤ 1m (mid-field) — large fraction of any indoor scene
- sdfDist > 1m AND linearDepth ≤ 4m (close-but-far-from-occluder)
- sdfDist < 0.3m AND |dot(N,V)| ≥ 0.5 (close-front-facing)

cornell-orig-alcove is ~3m on a side. `linearDepth > 4m` excludes essentially **the entire scene** at cam0 and cam1. The "far-distant" bin is empty or near-empty on the very scene this is supposed to diagnose. The result: §2.2 reports "far-distant RMSE = NaN (no pixels)" or "= 0 (one pixel)" and the verdict (C4) concludes "long-distance gap not dominant" — when the test never had any far-distant pixels to measure.

**Fix:**
- G3 partially helps (`farThreshold = 0.4 × sceneDiag`) but G3 was kept as a "gap action" not a structural fix; it's still essentially the same definitions with rescaled thresholds. The underlying issue is that **the partition definitions are independent of the cascade architecture they're diagnosing.** A "long-distance" failure mode in this codebase is *defined* by which cascade does the work — pixels whose dominant contributing cascade is C2/C3 (large probe spacing) vs C0/C1 (small probe spacing).
- Add a fourth diagnostic mode that asks the right question: "Which cascade's smoothstep band is this pixel currently in?" Color-bin by **dominant-cascade-index** (using the consume-time `l` blend factor — for each pixel, identify the cascade with max `(1 - l_i)·something_i`). That partitions pixels by the architectural feature being diagnosed.
- Recompute §2.2's "long-distance dominant" vs "short-distance dominant" verdict using cascade-dominance bins rather than scene-geometry bins. Then C4 becomes meaningful: "C2/C3-dominated pixels show 3× RMSE vs C0/C1-dominated → A2/C1 (long-distance levers) are the priority."

---

## MEDIUM severity

### M1 — Luminance-RMSE primary will hide color-channel-specific failure modes

§8 answer 2 + §7.1 pin luminance-RMSE primary, linear-RGB per-channel mean secondary. But cornell-orig-alcove's defining feature is **color bleed** (red wall → ceiling tinted red, green wall → red ceiling region tinted green-shifted). Cascade's known failure modes are often per-channel (cerebrum 2026-05-19: "PT GI brighter than cascade GI" was a mode-19 finding **invisible to luminance** because dark/lit pixels canceled). If cascade under-integrates the red bounce specifically, R-channel RMSE will be 3× G-channel RMSE — but luminance (0.2126·R + 0.7152·G + 0.0722·B) weights G heavily and will show only a 1.4× elevation.

**Fix:**
- Make the report require per-channel RMSE in the same table, not as a "secondary mention." If `max(R_rmse, G_rmse, B_rmse) > 1.5 × min(...)`, the report MUST flag color-channel-asymmetric error and propose corresponding lever investigation (this points at MB v2's diffuse-only-bounce, color-channel-correlated atlas storage, etc.).
- C5 sign-off: include "color-channel imbalance not >1.5× across channels" as a check, or "if it is, identify cause in verdict."

### M2 — 64-frame noise-floor measurement (F3 fix) doesn't specify independence-source for the 4 runs

F3 raises capture from 16 to 64 frames and "4 independent runs of 16 frames each." But what makes the 4 runs independent? Cascade RC's per-frame stochasticity comes from:
- Probe jitter (Halton, seeded by frame index)
- MC sampling for MB v2 (PCG, seeded by frame + probe + bin)
- Hybrid correction stochastic sampling (only if hybrid on — but §2.7 hybrid-on baseline is separate)

If "4 runs" means re-running the same frame range with the same seeds, they're **bit-identical** and inter-run RMSE = 0 (trivially passes C2). If 4 runs use different starting frame indices (frames 0-15, 16-31, 32-47, 48-63), they're consecutive, not independent — temporal EMA correlates them.

**Fix:** Specify "4 runs = 4 different PCG seed offsets injected at the cascade-bake RNG site; each run accumulates 16 frames starting from a clean cascade and clean MB-feedback state." Add CLI `--noise-seed-offset=N` for reproducibility.

### M3 — MBRC config under-specified

§2 captures "baseline" without locking the MBRC toggle state. Cascade has many runtime toggles, each ±1-10% on RMSE:
- `useMultiBounce` (MB v2 on/off) — cerebrum says +3.5%
- `useScaledDirRes` — affects which cascade has which bin count
- `useWeightedSample` (Phase 3) — default off but configurable
- `useHistoryClamp`, `useProbeJitter`, GI blur radius/sigmas
- `c0probeRes` (8/16/24/32/48/64) — default 32, but a different default changes the whole story

A change in any of these between report capture and v2.0a implementation will silently re-baseline. **Fix:** §3 must emit a "cascade-config.json" alongside the CSVs, listing every relevant toggle's value at capture time. v2.0a implementation reads this file at its own measurement time to confirm the same baseline.

### M4 — C5 ("verdict ties to v2.0a lever recommendation with quantitative justification") is unfalsifiable as written

What's the numeric rule for "gap (a) dominates"? Without a pre-registered threshold, the verdict is motivated reasoning. Three failure modes the current language permits:
- Far-distant RMSE = 0.040, near-grazing = 0.038, "other" = 0.080 → verdict says "neither dominates, replan" or "both balanced, ship A2+A3" depending on author's mood
- Same numbers + author bias toward A3 → "near-grazing matters because it's near user attention"
- Author wants to defer C1 (cubemap, 2 days) → "far-distant gap is within noise"

**Fix:** Pre-register the decision rule in §5:
- `(far_dist_RMSE) > (near_grazing_RMSE) + 2×noise_floor` → "long-distance dominates → C1 + A2 priority"
- `(near_grazing_RMSE) > (far_dist_RMSE) + 2×noise_floor` → "short-distance dominates → A3 + B2 priority"
- `|far − near| < 2×noise_floor` AND `(far + near) > 0.5 × baseline RMSE` → "both contribute → A2 + A3 + B2"
- `(far + near) < 0.5 × baseline RMSE` → "neither geographic gap dominates; replan §1"

### M5 — No measurement of cascade's converged-frame baseline (frame N where temporal EMA has settled)

The cascade's quality at frame 1 (cold start) is dramatically different from frame N=300 (temporal converged + MB stabilized). Cerebrum confirms MB v2 takes ~1/α frames to settle; at α=0.05 that's 20 frames. PT reference at 4096 spp is what — frame 4096 of accumulation? Are cascade and PT compared at matched temporal-state? §2 implies "render cascade at the same camera as PT, compute RMSE," but cascade's MB feedback at frame 1 is zero, at frame 50 is converged. **Fix:** Specify "cascade is captured at frame ≥ 100 (MB-EMA converged) with TAA / accumulator buffers warm. Capture frame number recorded in `cascade-config.json`."

---

## LOW severity

### L1 — PT cache "16-bit PNG (or EXR if HDR range demands it)" should default to EXR

cornell-orig's light source pixel and direct-fall-on bright walls routinely exceed 1.0 in linear-RGB. 16-bit PNG is sRGB-encoded LDR; converting linear HDR to it clips or compresses any pixel >1.0, contaminating the reference for §2.2's RMSE. EXR is the same disk footprint (~10 MB at 720p × float32) and has no clipping. **Fix:** Drop the PNG option; mandate EXR.

### L2 — §3 effort table and §7.2 revised effort double-document; reader may follow §3

§3 says "Day total: ~10h"; §7.2 revises to "12-13h" but §3 is unchanged. A reader could implement against §3 and skip the revisions. **Fix:** Replace §3's "Total: ~10h" line with a pointer "→ superseded by §7.2 (revised estimate accounts for F1+F4+F5 fixes)."

### L3 — Deliverable lists §4 and §7.3 both exist

Same problem: §4 lists 6 artifacts, §7.3 lists 7 (`hybrid_on_baseline.csv` added, isolation→loo renamed). Reader can follow §4 and miss `hybrid_on_baseline.csv`. **Fix:** Delete §4 entirely or mark it explicitly "(superseded by §7.3)."

### L4 — PT mode config not pinned

PT reference (mode 16) supports `uPtCascadeMatch ∈ {unbiased, cascade-match}`. The plan never says which the measurement uses. Per the PT impl (cerebrum 2026-05-18):
- Unbiased = true reference for §2.3 PT-convergence test
- Cascade-match = cascade's "converged target" for §2.1/§2.2/§2.7 RMSE

These are different captures. **Fix:** §2.5's "PT reference cache" must include both modes per camera (6 EXRs total: 2 modes × 3 cameras). §2.2 and §2.7 use cascade-match (apples-to-apples with what cascade tries to compute); §2.3 convergence test uses unbiased.

### L5 — `mbrc_quality_plan.md §9 answer 5` reference is ambiguous

§1 / §3 etc. cite "mbrc_quality_plan.md §1" but §9 in that doc is the open-questions section, with numbered answers. The text says "§9 answer 5" but the plan elsewhere says "§1." Likely both true (motivation in §1, deliverable shape in §9 a5), but a reader can't tell without opening the predecessor. **Fix:** in the report doc that will reference these, qualify "predecessor §1 (motivation) and §9 answer 5 (deliverable-form decision)."

---

## Severity summary

| ID  | Sev    | Issue                                                                                |
|-----|--------|--------------------------------------------------------------------------------------|
| H1  | HIGH   | Leave-one-out exclusion mechanism unspecified; risk of measuring merge disturbance   |
| H2  | HIGH   | PT-adequacy test averages across pixels with radically different convergence rates   |
| H3  | HIGH   | Hybrid baseline uses "best-CSV-arm" that variance harness showed = statistical tie   |
| H4  | HIGH   | §2.2 partitions miss most of cornell-orig-alcove pixels; wrong diagnostic feature    |
| M1  | MEDIUM | Luminance-primary RMSE hides color-channel-asymmetric failure modes                  |
| M2  | MEDIUM | 64-frame "4 independent runs" independence-source unspecified                         |
| M3  | MEDIUM | MBRC config (MB, useScaledDirRes, c0probeRes, etc.) not pinned in deliverable        |
| M4  | MEDIUM | C5 verdict rule unfalsifiable; needs pre-registered numeric decision thresholds      |
| M5  | MEDIUM | Cascade temporal-state at capture (frame N) not specified                            |
| L1  | LOW    | PT cache should default to EXR, not 16-bit PNG (clipping)                            |
| L2  | LOW    | §3 effort estimate superseded by §7.2 but not marked                                  |
| L3  | LOW    | §4 deliverable list superseded by §7.3 but not marked                                 |
| L4  | LOW    | PT-mode config (unbiased vs cascade-match) not pinned per measurement                |
| L5  | LOW    | §9-vs-§1 cross-reference to predecessor doc ambiguous                                |

---

## Top actions for plan revision (rev 2)

1. **H1 (must-fix before instrumentation):** specify leave-one-out as "skip in merge with weight renormalization" (option b); add sanity assertion `RMSE(exclude=none) == baseline ± 0.1%`.
2. **H2 (must-fix before §2.3 capture):** restructure §2.3 to per-spatial-bin PT convergence test; sweep alcove pixels to higher spp if needed (16k or 64k).
3. **H3 (must-fix before §2.7 capture):** use hybrid v1.2.4 shipped defaults as baseline; report tuned-arm separately with stderr.
4. **H4 (must-fix before §2.2 capture):** add cascade-dominance partition (which cascade's smoothstep band each pixel lives in); make this the primary §2.2 axis, geometry-based bins as secondary.
5. **M3-M4 (must-fix before sign-off):** emit `cascade-config.json` at every capture; pre-register the C5 numeric verdict rule in §5.
6. **L2-L3:** clean up superseded §3 and §4 (delete or mark superseded).

Then re-estimate: post-revision likely **2 days** (was 1.5 in §7.2). The extra half-day is alcove-only PT high-spp capture (~2h) + cascade-config.json plumbing (~30min) + per-spatial-bin convergence test (~1h) + cascade-dominance partition shader work (~1h) + verdict rule + report scaffolding (~1h).

---

## What the plan already gets right (don't change)

- The principle that no v2.0 feature code lands before a signed-off measurement report (anti-bug-212).
- §2.4 noise floor as meaningfulness threshold for RMSE deltas.
- §2.5 PT-reference cache (capture once, reuse) — exactly right.
- §8 explicit lockdown of open questions before instrumentation.
- §6 V/G/F self-critique format (the structural fixes that did land — F1 leave-one-out is correctly identified even if under-specified).
- §7.5 honest "what this still doesn't do" (defers SSIM/LPIPS to v2.0a; defers perf measurement; cornell-only).

The plan's core design — measure-then-feature, per-camera-position spreadsheet, sign-off criteria — is sound. The fixes are about pinning specifications tighter so that the report's numbers actually answer Q1-Q5 rather than appearing to.
