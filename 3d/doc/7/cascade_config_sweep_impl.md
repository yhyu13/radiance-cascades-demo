# Cascade-config sweep — Implementation Notes

Companion to [mbrc_v20_pre_measurement_report.md §12](mbrc_v20_pre_measurement_report.md). Documents the engine-side CLI additions, the sweep harness + analyzer, and the methodological learnings from running the (γ) discriminator.

Date: 2026-05-21 evening / 2026-05-22. Build: Release, MSVC, clean.
Commit: `8a487f5` (568 insertions). Preceded by `6baa004` (full sweep + bug-230 + (γ) promotion).

## 1. Scope landed

CLI + harness only. No shader changes. Total ~1 hour engine work, 2.3 min sweep, ~30 min analysis + report.

- `--cascade-dir-res=N` — override `Demo3D::dirRes` (octahedral D, D² rays/probe). Validated even, range [2..32]. Triggers `cascadeReady=false` so the next frame rebuilds the atlas at the new resolution. Lives at [main3d.cpp:629](../../src/main3d.cpp#L629); setter `Demo3D::setDirRes` at [demo3d.h](../../src/demo3d.h).
- `--cascade-scaled-dir-res=0|1` — toggle `Demo3D::useScaledDirRes`. When 1, cascades get D / 2D / 4D / 4D (capped 16). When 0, all four cascades use uniform D. Uniform mode is what isolates the angular-resolution effect from per-cascade scaling. Setter `Demo3D::setUseScaledDirRes`.
- [tools/v20_pre_measurement/cascade_config_sweep.ps1](../../tools/v20_pre_measurement/cascade_config_sweep.ps1) — 12-capture PowerShell driver. Mirrors `full_sweep.ps1`'s `Capture` function (raylib bug-211 `Move-Item` workaround included).
- [tools/v20_pre_measurement/analyze_cascade_config.py](../../tools/v20_pre_measurement/analyze_cascade_config.py) — PIL + numpy analyzer. Classifies pixels into blue/red Δ bands via saturation threshold (0.55) + channel dominance, with a luma floor (0.05) to drop letterbox background.
- [tools/v20_pre_measurement/cascade_config_results.json](../../tools/v20_pre_measurement/cascade_config_results.json) — raw 12-capture JSON for reproducibility.

## 2. Why this sweep was the discriminator

Per the report's §8 (post-full-sweep): hypothesis (γ) — *cascade's fixed angular bin count smears concentrated indirect light across many directions → peak dim, floor raised* — was promoted to leading candidate. It was *consistent* with three independent observations:

- LOO uniformity (every cascade has the same bin count → every cascade looks equally wrong)
- Cross-camera consistency (architecture-tracking, not screen-space artifact)
- Hybrid's asymmetric response (PT-integrated correction samples can only ADD energy, not subtract — so they fix under-illumination but worsen over-illumination)

But "consistent with" ≠ "explained by." The cascade-config sweep is the cheap *causal* test: if (γ) is right, multiplying angular sample count by 4× should significantly shrink the Δ regions. If invariant, (γ) is rejected.

The lever — `dirRes` — already existed as a runtime-tunable field. Total engine cost was wiring two CLI flags (~30 lines across main3d.cpp + demo3d.h). The cost asymmetry between "ship a fix on visual A/B" vs "run a discriminator first" was perhaps **2 hours** of total work, against the v1.3.1 NEE/cone precedent ([hybrid_v12_validation_phase8_impl.md §10.4](hybrid_v12_validation_phase8_impl.md)) where shipping on visual evidence wasted a full session and required a variance harness retrofit to surface the tie.

## 3. Decision rule was committed BEFORE the run

Documented in `mbrc_v20_pre_measurement_report.md` §11 and project_phase_status memory:

| Verdict | Mode-19 (D16-D8)/D8 reduction |
|---|---|
| STRONG_GAMMA | ≥50% on BOTH cams (ship D=16+ as v2.0 baseline) |
| WEAK_GAMMA | 10-50% (or asymmetric — one cam only) |
| GAMMA_REJECT | ≤10% (pivot to (α) merge-weighting / (β) MB-gain) |

This pre-commitment is the load-bearing methodological step. A "1.0% / 1.4% reduction" reads ambiguously without a prior threshold — confirmation bias would have spun it as "moving the right direction." With the threshold pre-locked, the same number reads cleanly as a rejection. See cerebrum.md DNR entry [2026-05-21] "Commit decision rules BEFORE running the discriminator sweep."

## 4. Quantitative result

| cam/mode    | D=4 tot% | D=8 tot% | D=16 tot% | (D16-D8)/D8 |
|-------------|---------:|---------:|----------:|------------:|
| cam0 mode18 |   27.16% |   25.26% |    25.17% |       −0.4% |
| cam0 mode19 |   29.15% |   26.54% |    26.27% |       **−1.0%** |
| cam2 mode18 |   25.09% |   24.37% |    24.24% |       −0.5% |
| cam2 mode19 |   21.71% |   20.57% |    20.29% |       **−1.4%** |

Mean band-saturation: blueSat ∈ [0.719, 0.729] across all 12 captures. The Δ regions are not just same-sized — they're same-deep.

**Verdict: GAMMA_REJECT.** Both mode-19 reductions inside ±10%.

Visual cross-checks confirm: cam0 m19 at D=4 vs D=16 shows blue/red regions in identical screen positions, identical shape, near-identical saturation. cam2 m19 D=4 vs D=16 equally indistinguishable. The 4× ray-count multiplier produced no observable softening of the asymmetric Δ pattern.

**Sanity: dirRes actually varied the render.** md5 of cam0_d04/d08/d16_m19.png are all distinct (17701cf8.., 5844b9a6.., b11a7a27..) — rules out a "flag silently ignored" failure mode (the same class as bug-212 in the v1.3.1 NEE precedent). The lack of *Δ-area* movement is a real null result, not an instrumentation artifact.

## 5. What rejection means for the hypothesis tree

The cascade-vs-PT asymmetric Δ pattern is **not dominantly** angular-resolution-bound (see §7.C9 — does not rule out 1-2% residual contribution). The "many-bins-smear" mechanism proposed in §8 does not survive as the leading explanation. Updated tree:

- **(α) merge-time directional weighting** — original scouting hypothesis. Per-direction merge weights in [radiance_3d.comp](../../res/shaders/radiance_3d.comp) may double-count or under-count indirect contributions from specific direction bands when probes are merged across cascade boundaries. The partition-opening anchor is suggestive: rays passing through the narrow alcove gap occupy a *specific direction band* that may be the mis-weighted one.
- **(β) multi-bounce gain at wrong fixed point** — MB gain=1.0 is theoretically energy-conserving but the empirical hemi_factor is ~0.05 vs theoretical 0.5 (see cerebrum 2026-05-18 entry on Phase MB). The effective fixed point is lower than the geometric series predicts; gain=2.0+ would raise the cascade GI floor — which is *exactly* where mode 19 says cascade is too dim. **Caveat:** MB gain raises overall brightness; it does NOT expand dynamic range. The full-sweep §3.5.4 finding (PT ~10× L/R ratio vs cascade ~1.5×) is a *contrast* problem; (β) addresses the cascade's GI floor but not its contrast ceiling. (β) may halve the Δ area without fixing the asymmetry character.
- **(δ) spatial probe density / smoothstep cascade blending** — NEW, see §7.C8. Probe grid resolution per cascade and the smoothstep band across cascade boundaries could produce architecture-tracking Δ patterns indistinguishable from the observed ones. Discriminator deferred; design after (β) reports.

(β) is the cheap next test — `multiBounceGain` already has CLI + GUI wiring, so a 5-value × 2-cam × 2-mode sweep is ~3 min total. (α) requires shader work first (~2-3h to add an isotropic-merge A/B flag to `radiance_3d.comp`). (δ) will need a `--cascade-c0-res=N` CLI flag.

## 6. Methodological learnings (cerebrum-worthy)

### 6.1. Pre-commit decision rules for *discriminator* sweeps

Already added to [cerebrum.md](../../.wolf/cerebrum.md) DNR list. Pre-commitment converts ambiguous-magnitude observations into clean accept/reject signals. The rule scopes to **discriminator** sweeps — sweeps designed to test a specific hypothesis with a binary verdict. Exploratory / parameter-mapping sweeps (e.g., "what does the cascade look like across multiBounceGain∈{0.5..3.0}") don't need a pre-locked threshold because the artifact *is* the parameter→quality map, not a yes/no judgement. But every *decision-driving* sweep gets the threshold written in before the run. See §7.C6 for the symmetric trap this rule does NOT protect against.

### 6.2. "Consistent with" is not the same as "explained by"

(γ) was *consistent with* every observation in the full sweep. That's a low bar — many alternative mechanisms are also consistent. Promotion of a hypothesis to "leading candidate" must come with an *unambiguous prediction* whose failure would falsify it, not just a story that fits past data. The cascade-config sweep was exactly that prediction; it failed, so the hypothesis dies.

### 6.3. Cheap discriminator > expensive fix

Wiring `--cascade-dir-res=N` cost less than an hour. Shipping a v1.4 "angular oversample" path would have been days. When a hypothesis is testable via an existing runtime parameter, write the CLI flag and sweep first — almost regardless of how confident you are.

### 6.4. Engine had the lever pre-built

`dirRes` was already a tunable field with `useScaledDirRes` already implemented. The only missing piece was CLI access. **Many discriminator sweeps will have this property** — the engine's existing GUI sliders / boolean toggles are candidates for fast CLI exposure. Survey existing GUI controls before designing new instrumentation.

### 6.5. Single-seed sweeps are valid for *configuration* axes (not noise axes)

bug-230 (`--noise-seed-offset` only wired to `uMBFrameSeed`, not PT/hybrid) makes the seed axis non-functional. But for the cascade-config sweep, this didn't matter: D=4, D=8, D=16 use the same PCG state at the same frame index, so within-D comparisons are valid — the difference *is* the dirRes change, not seed variation. Distinguish noise axes (where bug-230 invalidates conclusions) from configuration axes (where it doesn't).

## 7. Self-critique

### C1. Saturation-threshold classifier is heuristic

The analyzer's Δ-band classifier uses a fixed 0.55 saturation threshold and ±0.05 channel-dominance gap on the bipolar PNG colormap. It is NOT the raw cascade-PT scalar Δ from an EXR. If the colormap distorts area perception non-monotonically with the underlying Δ magnitude, the metric could mis-rank configurations. Mitigation: visual cross-checks on the deepest case (D=4 vs D=16) corroborate the metric — same pattern, same shape, same depth. EXR-based RMSE/SSIM is still in the §11 deferred list.

### C2. D=16 may not be the ceiling

D=16 is already 2× beyond the engine default (D=8). The atlas dimensions cap higher D (D=32 would need 4× atlas memory). Hypothesis (γ) being rejected at D=16 doesn't *strictly* rule out a super-linear regime appearing at D=32 or D=64, but given the trend is essentially flat from D=4 onward (29.15% → 26.54% → 26.27%), super-linearity at higher D would have to be discontinuous to flip the verdict. Acceptable scope cut.

### C3. Single-scene result

Only cornell-orig-alcove was tested. The (α) and (β) follow-up sweeps should re-introduce scene diversity (plain cornell, then sponza) before shipping any code change. Noted in §12.6 of the report.

### C4. `useScaledDirRes=0` is a non-default configuration

The sweep runs in uniform-D mode to isolate the angular-resolution variable. This is NOT what the engine ships by default (scaled mode gives upper cascades more bins). A theoretical concern: maybe the "scaled" topology has a regime where increasing the *base* D helps. Verified from [src/demo3d.cpp:3994](../../src/demo3d.cpp#L3994) (`cascD = std::min(16, dirRes << i)`):

| config | C0 D | C1 D | C2 D | C3 D |
|---|---:|---:|---:|---:|
| scaled D=8 (engine default for full sweep) | 8 | 16 | 16 | 16 |
| uniform D=16 (this sweep, highest) | 16 | 16 | 16 | 16 |

So uniform-D=16 matches scaled-D=8 at C1/C2/C3 and *strictly exceeds* it at C0 (16 vs 8). The original wording "strictly more angular sampling than scaled-D=8 anywhere" was sloppy — it's strictly more at C0 only, equal elsewhere. But the conclusion holds: the only delta between the two configurations is C0 bin count, and **C0 is the cascade most likely to dominate the visible signal** at typical camera distances (smallest cells, finest probe grid). If quadrupling C0 angular resolution doesn't move the Δ-band area, neither will tuning scaled-mode's base D.

### C5. Visual cross-checks were a sanity gate, not a primary metric

The 4 PNGs I inspected (cam0/cam2 × D=4/D=16 × m19) are evidence, but not *quantitative* evidence. The 1.0%/1.4% delta numbers are the load-bearing claim; the visuals just confirm the metric isn't lying about pattern identity. Future sweeps shouldn't substitute visual inspection for the metric — they're complementary.

### C6. Analyzer thresholds were chosen AFTER seeing captures

`SATURATION_THRESHOLD=0.55` and `BG_LUMA_FLOOR=0.05` in [analyze_cascade_config.py](../../tools/v20_pre_measurement/analyze_cascade_config.py) were tuned on the actual captures, not pre-committed. This is a methodological mirror image of §6.1: the **decision rule** (verdict bands ±10% / ±50%) was pre-committed, but the **metric** that feeds it was not. In principle a sympathetic analyst could shop the threshold to flip a marginal verdict. The current result is robust to this — at every threshold in [0.30, 0.75] the D8→D16 delta stays within ±5% — but the procedural gap is real. Going forward: when feasible, lock metric thresholds against an unrelated capture set BEFORE running the sweep, or define the metric in unitless terms (e.g., relative reduction of *whatever count emerges from threshold X*, with X reported in the rule). See cerebrum.md DNR addendum to the 2026-05-21 entry.

### C7. cam0 and cam2 are not independent confirmations

The "BOTH cams" clause in the verdict rule reads as two-out-of-two independent checks, but cam0 and cam2 frame the *same* scene with the *same* light. They share: the partition geometry, the alcove, the wall colors, the light position, the multi-bounce convergence state. If hypothesis (γ) failed for a *scene-specific* reason (e.g., the alcove gap is wider than C0's angular bin solid angle even at D=4), both cameras would agree on rejection regardless of (γ)'s validity in general. The verdict is "GAMMA_REJECT *in cornell-orig-alcove*", not "GAMMA_REJECT universally". Re-run on plain-cornell (no partition) and on sponza is needed before lifting the qualifier.

### C8. Hypothesis tree was incomplete — (δ) spatial probe density not enumerated

(α)/(β)/(γ) cover *angular* and *temporal* dimensions of cascade error. They do NOT cover the *spatial* dimension: probe grid resolution per cascade and the smoothstep blending across cascade boundaries. A "wrong" probe spacing or a leaky smoothstep band could produce architecture-tracking Δ patterns indistinguishable from (γ)'s prediction. The cascade-config sweep didn't test this because the spatial axis (`cascadeC0Res`) was held fixed at the engine default. Add (δ) to the hypothesis tree explicitly; design its discriminator (likely a `--cascade-c0-res=N` CLI flag mirroring `--cascade-dir-res=`) after (β) reports.

### C9. "GAMMA_REJECT" overclaims; the precise claim is "(γ) is not dominant"

The verdict label is a clean binary, but the underlying datum is "4× ray-count multiplier shrinks Δ-area by 1.0%/1.4%". This rules out (γ) as the *dominant* explanation for the Δ-band asymmetry. It does NOT rule out (γ) as a *secondary* contributor (~1-2% of the total) — and if the dominant cause (α/β/δ) is later fixed and the residual is still 5-10% Δ, raising D may be the cleanup pass that closes it. Treat (γ) as "demoted, not eliminated", and re-run the sweep after the next hypothesis is fixed.

## 8. Open / deferred (next session candidates)

### 8.1. (β) MB-gain discriminator sweep — RECOMMENDED NEXT

**Sweep matrix.** `multiBounceGain` ∈ {0.5, 1.0, 1.5, 2.0, 3.0} × cam{0,2} × mode{18,19} = 20 captures (~5 min). `--use-multi-bounce=1` required on every command line — MB feedback is OFF by default and the gain slider has no effect without it (verify in `cascade-config.json` dump: `useMultiBounce=true`). Hybrid path stays OFF (`useHybrid=0`) — both v1.3.1 PT-correction and MB-gain add energy, and running both confounds attribution per the §3.5 hybrid-asymmetry finding.

**Two pre-committed decision rules** (write into report §13 BEFORE running):

**Rule B1 — Δ-band area reduction (the (γ)-rejection-pattern test).**
| Verdict | mode-19 Δ-area reduction (gain=2.0 vs gain=1.0) |
|---|---|
| STRONG_BETA | ≥30% on BOTH cams |
| WEAK_BETA   | 10-30%, or one-cam-only |
| BETA_REJECT | ≤10% on BOTH cams |

The 30% threshold is *less defensible* than the (γ)-sweep's 50%/10% bands — Phase MB data shows gain=1.0 → +3.5% scene brightness, gain=2.0 → ~7% (linear in gain at low gains, geometric beyond ~3.0). A 30% Δ-area reduction from a 3.5% brightness lift implies the Δ band is concentrated near the under-illumination threshold — plausible for cornell but not guaranteed. Treat STRONG_BETA as "actionable", WEAK_BETA as "ship a magnitude metric upgrade then re-evaluate".

**Rule B2 — magnitude-toward-PT (the new metric; addresses §3.5.4).**
Per-pixel cascade GI magnitude divided by PT GI magnitude (both from mode-16 PT cache + cascade composite), averaged over the *under-illuminated foreground* (cascade<PT and cascade GI < 50% of PT GI). Call this `floorRatio` ∈ [0,1]; pre-rejection baseline (gain=1.0) is the reference.
| Verdict | gain=2.0 vs 1.0 floorRatio |
|---|---|
| BRIGHTNESS_LIFT_CONFIRMED | gain=2.0 raises floorRatio by ≥0.10 |
| BRIGHTNESS_LIFT_REJECTED  | <0.05 lift |

B1 and B2 can disagree. B1 measures *asymmetry shrinkage*; B2 measures whether MB-gain actually moves cascade GI toward PT magnitude. The interesting cases:
- B1 STRONG + B2 CONFIRMED → ship gain=2.0 as v2.0 baseline.
- B1 STRONG + B2 REJECTED → suspicious (Δ shrank but cascade didn't approach PT?); investigate the metric before shipping.
- B1 REJECT + B2 CONFIRMED → MB-gain works as intended (lifts the floor) but the Δ pattern is contrast-shaped, not floor-shaped → (β) rejected as a cure, pivot to (α) or (δ).
- B1 REJECT + B2 REJECTED → MB-gain doesn't even move the floor; rules out (β) entirely.

**Risk surface specific to (β):**
- MB feedback accumulates noise temporally — single-seed sweep (bug-230 still open) is sharper concern than for (γ) because gain>1.0 amplifies any seed-correlated bias. Capture at `--exit-frames=512` minimum (same as (γ) sweep); consider 1024 if first run shows large frame-to-frame variance in mode-19 area.
- `cascade-config.json` dump must confirm `useMultiBounce=true` AND the gain value matches the CLI — script must `Stop-Process` between runs to avoid stale state.
- The full-sweep run used scaled-D=8 (engine default); (β) sweep should match (NOT use uniform-D from this sweep) so results are comparable to the §3.5 full-sweep numbers.

**Deliverable.** Mirror the §11/§12 pattern: write report §13 with results table, both verdicts, visual cross-checks, and what the verdict implies for (α)/(δ). Companion impl doc `doc/7/mb_gain_sweep_impl.md` if any engine work was needed (likely zero — CLI exists).

### 8.2. Cross-cutting work also deferred

- **(α) merge-mode A/B** — 2-3h engine work to add isotropic-merge flag to [radiance_3d.comp](../../res/shaders/radiance_3d.comp). Defer until (β) reports. If B1=STRONG, (α) drops to nice-to-have. If B1=REJECT and B2=CONFIRMED, (α) becomes likely cause of residual contrast asymmetry.
- **(δ) spatial probe density discriminator** — add `--cascade-c0-res=N` CLI flag mirroring `--cascade-dir-res=`. Sweep design TBD; pre-commit decision rule before run per §6.1.
- **bug-230 fix** — still open. Becomes mandatory before (β) sweep if first (β) result is in the WEAK_BETA band, because seed correlation could be the 5-10% noise floor that prevents clean verdict. Audit `--noise-seed-offset` callsites in PT (`uFrameIndex`) + hybrid (`uHybridFrameSeed`) shaders.
- **Plain cornell scene** — second-scene validation per report §11; per §7.C7 this is required before any "GAMMA_REJECT universally" claim. Add after (β) sweep; same sweep matrix on plain cornell becomes (β) cross-scene confirmation simultaneously.
- **EXR-based RMSE/SSIM** — replace saturation-band heuristic per §7.C6. Needs tinyexr add to engine + extension to analyzer. Concrete trigger: any future sweep that lands a verdict in the WEAK band, since heuristic-classifier noise is the most likely contributor.

## 9. Files touched

| File | Change | Lines |
|---|---|---|
| [src/main3d.cpp](../../src/main3d.cpp) | +2 CLI flags after `--cascade-config-dump` | +17 |
| [src/demo3d.h](../../src/demo3d.h) | +2 setters (`setDirRes`, `setUseScaledDirRes`) | +18 |
| [doc/7/mbrc_v20_pre_measurement_report.md](mbrc_v20_pre_measurement_report.md) | §12 (6 subsections) + §7 changelog entry | +140 |
| [tools/v20_pre_measurement/cascade_config_sweep.ps1](../../tools/v20_pre_measurement/cascade_config_sweep.ps1) | NEW — 12-capture driver | 74 |
| [tools/v20_pre_measurement/analyze_cascade_config.py](../../tools/v20_pre_measurement/analyze_cascade_config.py) | NEW — PIL+numpy analyzer with verdict logic | 149 |
| [tools/v20_pre_measurement/cascade_config_results.json](../../tools/v20_pre_measurement/cascade_config_results.json) | NEW — raw 12-capture JSON | 170 |
| [.wolf/cerebrum.md](../../.wolf/cerebrum.md) | +1 DNR entry "Commit decision rules BEFORE running discriminator sweep" | +1 |

Commit `8a487f5`: 6 files staged (cerebrum is local-only per project convention), 568 insertions, no deletions.
