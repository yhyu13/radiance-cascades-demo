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

## 5. What rejection means for the hypothesis tree

The cascade-vs-PT asymmetric Δ pattern is **not** angular-resolution-bound. The "many-bins-smear" mechanism proposed in §8 doesn't survive the data. The remaining hypotheses revert to:

- **(α) merge-time directional weighting** — original scouting hypothesis. Per-direction merge weights in [radiance_3d.comp](../../res/shaders/radiance_3d.comp) may double-count or under-count indirect contributions from specific direction bands when probes are merged across cascade boundaries. The partition-opening anchor is suggestive: rays passing through the narrow alcove gap occupy a *specific direction band* that may be the mis-weighted one.
- **(β) multi-bounce gain at wrong fixed point** — MB gain=1.0 is theoretically energy-conserving but the empirical hemi_factor is ~0.05 vs theoretical 0.5 (see cerebrum 2026-05-18 entry on Phase MB). The effective fixed point is lower than the geometric series predicts; gain=2.0 or higher would raise the cascade GI floor — which is *exactly* where mode 19 says cascade is too dim.

(β) is the cheap next test — `multiBounceGain` already has CLI + GUI wiring, so a 5-value × 2-cam × 2-mode sweep is ~3 min total. (α) requires shader work first (~2-3h to add an isotropic-merge A/B flag to `radiance_3d.comp`).

## 6. Methodological learnings (cerebrum-worthy)

### 6.1. Pre-commit decision rules, always

Already added to [cerebrum.md](../../.wolf/cerebrum.md) DNR list. Pre-commitment converts ambiguous-magnitude observations into clean accept/reject signals. Future hypothesis-test sweeps: write STRONG / WEAK / REJECT thresholds INTO the report or memory BEFORE the sweep runs, never after.

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

The sweep runs in uniform-D mode to isolate the angular-resolution variable. This is NOT what the engine ships by default (scaled mode gives upper cascades more bins). A theoretical concern: maybe the "scaled" topology has a regime where increasing the *base* D helps. Not tested. Acceptable because: (a) scaled mode at D=8 already gives upper cascades 16 bins each (= D=16 uniform's upper cascades), and the full sweep used scaled-D=8, so the m19 Δ-band area there (~26%) is already in the "high effective angular resolution" regime; (b) the uniform-D=16 test floods every cascade with 256 rays/probe — strictly more angular sampling than scaled-D=8 anywhere. If even *that* doesn't move the needle, scaled-mode tuning of D won't either.

### C5. Visual cross-checks were a sanity gate, not a primary metric

The 4 PNGs I inspected (cam0/cam2 × D=4/D=16 × m19) are evidence, but not *quantitative* evidence. The 1.0%/1.4% delta numbers are the load-bearing claim; the visuals just confirm the metric isn't lying about pattern identity. Future sweeps shouldn't substitute visual inspection for the metric — they're complementary.

## 8. Open / deferred (next session candidates)

- **(β) MB-gain discriminator sweep** — cheap, recommended next per §12.7. `multiBounceGain` ∈ {0.5, 1.0, 1.5, 2.0, 3.0} × cam{0,2} × mode{18,19} = 10 captures. Decision rule (pre-commit before run): gain=2.0 reduces mode-19 blue area by ≥30% on BOTH cams = BETA_CONFIRM; ≤10% = BETA_REJECT.
- **(α) merge-mode A/B** — requires 2-3h engine work to add an isotropic-merge flag to [radiance_3d.comp](../../res/shaders/radiance_3d.comp) before the sweep. Defer until (β) result known; if (β) confirms, (α) becomes lower priority.
- **bug-230 fix** — still open. Becomes a regression-detector for (β) sweep if (β) confirms; otherwise still needed before per-pixel PT noise bounding.
- **Plain cornell scene** — second-scene validation per report §11. Adds defensibility to the verdict.
- **EXR-based RMSE/SSIM** — eventually replace the saturation-band heuristic. Needs tinyexr add to engine + extension to analyzer.

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
