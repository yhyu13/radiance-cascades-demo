# Reply: MBRC v2.0-pre Measurement Plan — Critic 06

**Date:** 2026-05-20
**Status:** All 14 findings addressed. **H1, H2, H4 forced material methodology
changes**: leave-one-out becomes "skip-in-merge with weight renormalization"
(not atlas-zero); PT-adequacy is per-bin not whole-frame; primary partition axis
switches from scene-geometry thresholds to **cascade-dominance** (which cascade's
smoothstep band each pixel lives in). **H3 was overridden by user (2026-05-20)** —
variance-harness-tuned best arm stays as primary baseline, but the residual
concern (within-noise wins) is addressed by adding a 2-stderr-across-arms guard
band to the retirement decision rule. All five MEDIUM findings accepted; all
five LOW findings accepted. Plan revised to **rev 2**; revised effort **2 days**
(was 1.5).

---

## How each finding was addressed

### H1 (HIGH) — Leave-one-out exclusion mechanism unspecified

**Accepted.** Critic correctly identified that "atlas-zero" (uniform `uCascadeExclude`
gates atlas reads but consume-time blend still ramps) measures
`C_n loss + merge-formula disturbance` — a conflated quantity. "Skip-in-merge"
(consume loop renormalizes weights as if cascade i doesn't exist) measures pure
`without-C_n` and is the correct attribution.

**Fix in plan rev 2 §2.1 + §3:**
- Specify **skip-in-merge** as the exclusion mechanism. Document the consume-time
  smoothstep renormalization: when cascade i is excluded, the boundary between
  cascade i−1 and i+1 closes (cascade i+1's near band extends to cascade i−1's
  far band; smoothstep transitions skip the excluded bracket).
- Add sanity assertion to C3 sign-off: `RMSE(exclude=none) == RMSE(baseline) ± 0.1%`
  — proves the new code path is a no-op when nothing excluded.
- Atlas-zero rejected (would silently contaminate the attribution).

### H2 (HIGH) — PT-adequacy test averages across pixels with wildly different convergence

**Accepted.** Whole-frame mean RMSE between `spp=4096` and `spp=1024` is dominated
by fast-converging room pixels and silently passes C1 even when alcove (bounce-2+)
pixels haven't converged. cornell-orig-alcove was chosen *because* of those alcove
pixels — measuring against an unconverged reference there defeats the report's
purpose. Mirrors mode-19 "cross-component cancellation" failure (cerebrum 2026-05-19).

**Fix in plan rev 2 §2.3:**
- Per-bin convergence test: pair-RMSE computed within the same spatial bins used in
  §2.2 (cascade-dominance bins per H4). Each bin must independently satisfy
  `pair-RMSE(N, N/4) < 0.5 × cascade-RMSE-in-same-bin`.
- If alcove bin (likely C2-dominated or C3-dominated) fails at 4096 spp, sweep to
  **16k spp** for cam1 (alcove-dominated); 64k as fallback. cam0 and cam2 may
  remain at 4096 if their bins pass.
- Report records "PT reference converged at N spp for bin B" explicitly per the
  spreadsheet's per-camera-per-bin cells.
- C1 sign-off becomes per-bin, not whole-frame.

### H3 (HIGH) — Hybrid baseline pulls "best-CSV-arm" that variance harness showed is a tie

**OVERRIDDEN by user 2026-05-20** — critic concern partially addressed via stderr
guard, but primary baseline stays variance-tuned per user direction.

**Critic on the merits:** v1.3.1 #1 finding (cerebrum 2026-05-20) showed a00 vs
a05 RMSE Δ = −0.01% (TIE). Best-arm is therefore the noise-favored arm of
statistically-equivalent configurations. Using it as "the gap to retire" sets a
moving target invisible to users (who ship with defaults) and not reproducible
(re-running picks a different "best arm").

**User override rationale 2026-05-20:** The v2.0 retirement target is "what
hybrid is *capable of*" — the upper bound of the system being retired — not
"what hybrid ships with by default." Setting the target at the best-arm is the
*conservative* choice: if MBRC beats the best hybrid arm, it definitely beats
shipped defaults too. The reverse (target = shipped) lets MBRC declare victory
by matching a config nobody actually uses optimally.

**Critic residual concern addressed via stderr guard:** Plan rev 2 §2.7 records
`stderr across variance-harness arms` alongside the best-arm number, and the
decision rule for "MBRC retires hybrid" requires
`MBRC_RMSE < best_arm_RMSE − 2 × stderr_across_arms`. This prevents declaring
victory on a within-noise improvement (the failure mode the critic correctly
identified).

**Plan rev 2 §2.7 state:**
- Primary: variance-tuned best arm + stderr across arms (per user override)
- Secondary: shipped defaults (reported in same CSV)
- Decision rule: 2-stderr guard band

Implementation proceeds with §2.7 capture per user override; no further block.

### H4 (HIGH) — §2.2 partitions miss most cornell-orig-alcove pixels; wrong feature

**Accepted, primary axis switched.** Critic noted that `linearDepth > 4m` on a
3m-side scene leaves the far-distant bin empty, and the partitions are
architecturally orthogonal to what's being diagnosed. The actual question is
"which cascade does the work for this pixel," which is *defined* by the
consume-time smoothstep blend factors.

**Fix in plan rev 2 §2.2 + new §2.2.1:**
- **Primary partition axis: cascade-dominance.** For each pixel, compute
  `dominant_cascade = argmax_i (consume_weight_i)` from the consume-time
  smoothstep `(1 - l_i)`-style factors already computed. Bin pixels into
  C0-dominated / C1-dominated / C2-dominated / C3-dominated.
- Per-cascade-dominance bin: pixel count, RMSE, % of frame.
- C4 verdict reformulated: "C2/C3-dominated pixels show K× RMSE vs C0/C1-dominated
  → A2/C1 (long-distance levers) priority" (or the symmetric short-distance verdict).
- **Secondary partition axis: scene-geometry bins** (G3-rescaled thresholds:
  `farThreshold = 0.4 × sceneDiag`, `nearThreshold = 0.1 × sceneDiag`).
  Reported but no longer the basis for C4 verdict.
- New render mode 21 (or sub-mode of 20): cascade-dominance visualization
  (color-code pixels by dominant cascade index).

### M1 (MEDIUM) — Luminance-RMSE hides color-channel-asymmetric failures

**Accepted.** Cornell box is the color-bleed scene; cascade's known mode-19
finding ("PT brighter than cascade") was per-channel-asymmetric and would be
hidden by luminance alone. Per-channel R/G/B RMSE is now a co-primary table
column, not a "secondary mention." If `max(R,G,B) > 1.5 × min(R,G,B)`, the
report MUST flag color-channel-asymmetric error in the verdict.

**Note:** This co-exists with §8 answer 2 (luminance-RMSE primary as the
single-number ranking metric matching the v1.3.1 variance harness). Luminance
remains the primary scalar; per-channel is the diagnostic split that must always
be reported alongside.

**Fix:** Plan rev 2 §2 RMSE definition adds "per-channel RMSE always reported
alongside luminance." C5 sign-off adds color-asymmetry check.

### M2 (MEDIUM) — 64-frame "4 independent runs" independence-source unspecified

**Accepted.** Same-seed re-runs are bit-identical (inter-run RMSE = 0, false
pass). Consecutive frame ranges are temporally correlated (EMA carries forward).

**Fix in plan rev 2 §2.4:**
- "4 runs = 4 different PCG seed offsets injected at the cascade-bake RNG site
  (and at MB v2 stochastic sampler)."
- Each run starts from **clean cascade and clean MB-feedback state** (the
  cascade-clear flag + MB-clear flag toggled at run-start).
- New CLI `--noise-seed-offset=N` for reproducibility.
- Each run accumulates 64 frames before recording the final-frame RMSE.

### M3 (MEDIUM) — MBRC config not pinned in deliverable

**Accepted.** Cascade has many toggles each ±1-10% on RMSE; any silent change
between capture and v2.0a implementation re-baselines.

**Fix in plan rev 2 §3 + §4 + §7.3:**
- Emit `tools/v20_pre_measurement/results/cascade-config.json` at every capture.
- Records: `useMultiBounce`, `useScaledDirRes`, `useWeightedSample`,
  `useHistoryClamp`, `useProbeJitter`, `c0probeRes`, `baseInterval`,
  `dirRes`/scaled-dir-res schedule, GI blur sigmas, EMA α.
- v2.0a implementation reads this file at its own capture time and asserts
  equivalence (or documents the diff if intentional).

### M4 (MEDIUM) — C5 verdict rule unfalsifiable

**Accepted.** Pre-registered numeric decision rule per the critic's wording.

**Fix in plan rev 2 §5 (replaces narrative C5 with formal rule):**
- Let `nf = max(temporal_stdev, inter_run_rmse)` (noise floor per M2 fix).
- Let `R_long = RMSE in (C2 ∪ C3)-dominated bins`,
      `R_short = RMSE in (C0 ∪ C1)-dominated bins`,
      `R_baseline = RMSE all pixels`.
- **Rule 1:** `R_long > R_short + 2·nf` → "long-distance dominates → C1 + A2 priority"
- **Rule 2:** `R_short > R_long + 2·nf` → "short-distance dominates → A3 + B2 priority"
- **Rule 3:** `|R_long − R_short| < 2·nf` AND `(R_long + R_short) > 0.5·R_baseline`
  → "both contribute → A2 + A3 + B2 cluster"
- **Rule 4:** `(R_long + R_short) < 0.5·R_baseline` → "neither cascade-architectural
  gap dominates; replan mbrc_quality_plan.md §1"

### M5 (MEDIUM) — Cascade temporal state at capture not specified

**Accepted.** Cascade quality at frame 1 (cold MB feedback) vs frame 100 (MB-EMA
converged at α=0.05) differs materially. PT at 4096 spp is frame 4096 of
accumulation. Apples-to-apples requires matched temporal state.

**Fix in plan rev 2 §3:**
- Cascade captures wait until **frame ≥ 100** with MB-EMA + TAA + accumulator
  buffers warm.
- Capture frame number recorded in `cascade-config.json` (`captureFrame: 100`).
- Camera-pin (F4) plus this temporal-warm gate jointly define "the measurement state."

### L1 (LOW) — PT cache should default to EXR not 16-bit PNG

**Accepted.** Cornell-orig has light-source / direct-fall pixels > 1.0 linear-RGB.
16-bit PNG is sRGB-encoded LDR; clips at 1.0 and breaks downstream RMSE.

**Fix in plan rev 2 §2.5:** EXR (float32 RGB) mandated. PNG option dropped.
Disk footprint comparable (~10-15 MB at 720p × float32).

### L2 (LOW) — §3 effort estimate and §7.2 revised effort double-document

**Accepted.** Plan rev 2 §3 "Total: ~10h" replaced with pointer "→ superseded by
§7.2 (and now §7.2 in rev 2 = 2 days post-critic-06 fixes)."

### L3 (LOW) — §4 deliverable list and §7.3 double-document

**Accepted.** Plan rev 2 §4 explicitly marked "(superseded by §7.3 — see there
for authoritative list)."

### L4 (LOW) — PT mode (unbiased vs cascade-match) not pinned per measurement

**Accepted.** Per critic, the two PT modes serve different purposes:
- Unbiased PT (`uPtCascadeMatch=0`) = true reference for §2.3 PT convergence test.
- Cascade-match PT (`uPtCascadeMatch=1`) = cascade's "converged target" — the
  apples-to-apples reference for §2.1, §2.2, §2.7 RMSE.

**Fix in plan rev 2 §2.5:** Cache **6 EXRs total** per camera (2 PT modes × 3
cameras = 6, or 2 × 3 × multi-spp-tiers). §2.2 / §2.7 use cascade-match. §2.3
convergence test uses unbiased.

### L5 (LOW) — Predecessor cross-reference ambiguous

**Accepted.** Plan rev 2 references to `mbrc_quality_plan.md` qualified
explicitly: "§1 (motivation)" or "§9 answer 5 (deliverable-form decision)" or
"§9 answer 3 (hybrid baseline scope)" rather than bare "§9."

---

## Revised effort (rev 2)

Critic's estimate: **2 days** post-revision (was 1.5d in plan rev 1 §7.2). Breakdown:

- Original §3 instrumentation: ~7h
- H1 skip-in-merge consume rework: +1h (smoothstep renormalization is more invasive
  than uniform gating)
- H2 per-bin convergence test + alcove-high-spp capture: +2h (need to identify which
  bin needs which spp, possibly do a second 16k capture for cam1)
- H4 cascade-dominance partition shader work + new mode 21: +1.5h
- M3 cascade-config.json plumbing: +0.5h
- M4 pre-registered verdict rule + report scaffolding: +1h
- M5 frame-≥-100 gating + capture-frame recording: +0.25h
- L1 EXR writer (replaces 16-bit PNG): +0.5h
- L4 dual PT mode capture (2× the PT renders): +1h capture time
- Critique buffer: ~1h

**Total: ~16h = 2 days.** Within the user's original "good" sign-off on 1.5 days +
a half-day buffer for H1+H2+H4 specifically.

---

## What changed vs plan rev 1

| Section | Rev 1 | Rev 2 |
|---------|-------|-------|
| §2.1 | leave-one-out, mechanism unspecified | leave-one-out via **skip-in-merge with weight renormalization** (H1) |
| §2.2 | scene-geometry partitions (far-distant / near-grazing / other) as primary | **cascade-dominance partitions as primary** (H4); scene-geometry as secondary |
| §2.3 | whole-frame PT pair-RMSE | **per-bin pair-RMSE**; alcove may need 16k+ spp (H2) |
| §2.4 | 4 independent runs (independence-source unspecified) | 4 runs = 4 PCG seed offsets + clean cascade/MB state per run (M2) |
| §2.5 | 3 EXRs (one PT mode × 3 cameras) | **6 EXRs** (2 PT modes × 3 cameras) (L4) |
| §2.7 | hybrid baseline = variance-harness-tuned settings | **variance-tuned best arm stays as primary (user override 2026-05-20)** + shipped defaults reported as secondary; 2-stderr-across-arms guard band added to retirement decision rule |
| §3 | effort: ~10h | superseded by §7.2 (now 16h) (L2) |
| §3 | no `cascade-config.json` emitted | emits `cascade-config.json` (M3) |
| §3 | no cascade-temporal-state spec | frame ≥ 100 capture gate (M5) |
| §5 (C5) | narrative "verdict ties to lever recommendation" | **pre-registered numeric rule** (Rules 1-4) (M4) |
| §2 RMSE def | luminance-RMSE primary, per-channel secondary | luminance-RMSE primary; **per-channel always reported alongside** with 1.5× asymmetry flag (M1) |
| §4 | deliverable list | superseded by §7.3 (L3) |
| §7.2 effort | 12-13h | 16h (2 days) |
| §8 answer 3 | locked = variance-harness-tuned | UNCHANGED — user override 2026-05-20 reaffirmed; critic H3 rejected; stderr guard added to address residual within-noise concern |
| §2.5 format | "16-bit PNG (or EXR if needed)" | **EXR mandated** (L1) |

---

## H3 surfaced + resolved 2026-05-20

User overrode the critic. Variance-tuned best arm stays as primary baseline.

**User rationale:** v2.0 retirement target is "what hybrid is *capable of*" (the
upper bound of the system being retired), not "what hybrid ships with by
default." Beating the best arm is the conservative target — beating shipped is
weaker. If MBRC beats best-arm, it definitely beats shipped too.

**Critic residual concern addressed:** 2-stderr-across-arms guard band added to
the retirement decision rule in plan rev 2 §2.7. MBRC retirement is robust iff
`MBRC_RMSE < best_arm_RMSE − 2 × stderr_across_arms`. This prevents the
within-noise-win failure mode the critic correctly identified.

**Status:** §2.7 implementation unblocked. No further user input needed for
critic-06 disposition.

---

## What the plan already got right (per critic §"What the plan already gets right")

Unchanged in rev 2 — kept as-is:
- Measure-then-feature principle (anti-bug-212).
- §2.4 noise floor as RMSE-meaningfulness threshold.
- §2.5 PT cache (capture once, reuse).
- §8 explicit lockdown of open questions before instrumentation.
- §6 V/G/F self-critique format.
- §7.5 honest "what this still doesn't do."
