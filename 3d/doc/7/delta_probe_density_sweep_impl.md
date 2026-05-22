# (delta) Spatial probe density sweep — Implementation Notes

Companion to [mbrc_v20_pre_measurement_report.md](mbrc_v20_pre_measurement_report.md) §15, follow-on to [alpha_merge_sweep_impl.md](alpha_merge_sweep_impl.md). Documents the (δ) spatial-probe-density discriminator that the (α) rejection (§14) promoted to sole leading candidate, and the **DELTA_REJECT** outcome that exhausts the named-hypothesis tree (γ → β → α → δ all out).

Date: 2026-05-22 afternoon. Build: Release, MSVC, clean (zero engine work; `--cascade-c0-res=` already shipped from Step 12 scaling experiment).

## 1. Scope landed

- **Zero engine work** — `--cascade-c0-res=N` CLI flag, `Demo3D::setCascadeC0Res()` setter, and the destroy/init/cascadeReady=false/historyNeedsSeed=true invalidation chain were all already in [main3d.cpp:540](../../src/main3d.cpp#L540) and [demo3d.cpp:7151](../../src/demo3d.cpp#L7151). The (α) sweep's cerebrum DNR ("read shader for already-shipped toggles first") proved itself a second time: this sweep took ~0 min of engine work instead of the §14.7 estimate of "~30 min CLI wiring".
- [tools/v20_pre_measurement/delta_probe_density_sweep.ps1](../../tools/v20_pre_measurement/delta_probe_density_sweep.ps1) — 16-capture driver (4 N values × 2 cams × 2 modes).
- [tools/v20_pre_measurement/analyze_delta_probe_density.py](../../tools/v20_pre_measurement/analyze_delta_probe_density.py) — same classifier as analyze_alpha_merge.py (SAT=0.55, LUMA=0.05) plus the one-sided leverage detector that (α) C6 self-critique recommended.
- [tools/v20_pre_measurement/delta_probe_density_results.json](../../tools/v20_pre_measurement/delta_probe_density_results.json) — raw 16-capture JSON.

Total: 0 min engine work, 2.7 min sweep, ~30 min analysis + this doc.

## 2. Pre-sweep self-critique

### 2.1. The (α) DNR paid for itself again

The (α) impl doc said engine estimates for measurement discriminators must include a shader-read step because "many features ship with GUI checkboxes never connected to CLI." This time the lookup was even cheaper — the CLI flag *already exists*, shipped during the unrelated Step 12 scaling experiment. The (δ) plan estimated ~30 min of CLI wiring; reality was zero, because the CLI was wired during an unrelated phase and forgotten about.

**Promoted lesson:** the cerebrum search before estimating engine work should grep CLI parsers and existing setters, not just shader uniforms. The full "is this knob exposed" check is three places: shader uniform, GUI checkbox, CLI parser.

### 2.2. Pre-sweep md5 sanity check (per bug-234)

Captured cam0 m19 three ways before the full sweep:
- N=16: md5 `CCF284BF…`
- N=32: md5 `55B37903…`
- N=64: md5 `514A6F6A…`

Three distinct hashes confirmed `--cascade-c0-res=` actually changes the rendered output. No silent-fail risk.

### 2.3. The 4-point grid {16, 32, 48, 64} mirrors GUI choices

The cerebrum entry for "Cascade Architecture" lists C0 probe resolution as "8/16/24/32(default)/48/64 selectable at runtime". The sweep skipped N=8 (likely degenerate, ~8 probes per dimension) and N=24 (between N=16 and N=32, would only matter for fine-grained search if the verdict were WEAK). The 4-point grid captures the dominant lever (half / baseline / 1.5× / 2×).

Cost considerations: N=64 means 8× the probe-grid volume vs N=32. The cascade atlas memory scales accordingly. Cornell-orig-alcove handled it without OOM; sponza-scale scenes would need re-validation.

## 3. Quantitative result — B1 (Δ-band area, mode 19)

| cam | N=16 | N=32 (default) | N=48 | N=64 |
|---|---:|---:|---:|---:|
| cam0 | 26.54% (+0.0%) | 26.53% ref | 27.90% (+5.2%) | 27.33% (+3.0%) |
| cam2 | 19.51% (−4.5%) | 20.43% ref | 19.66% (−3.8%) | 19.75% (−3.3%) |

**Verdict per pre-committed rule: DELTA_REJECT** — all N within ±10% on both cams. The analyzer correctly emits the REJECT branch.

### 3.1. Per-axis isolation reading

- **cam0**: nearly flat in N. Halving the probe count (N=16) gives identical Δ-area. Doubling (N=64) gives +3.0%; 1.5× (N=48) gives +5.2%. The mild positive trend at higher N is the *opposite* of what an "undersampling cure" would predict.
- **cam2**: also nearly flat, with a *slight* negative trend at every non-default N. Even halving (N=16) gives −4.5% — meaning the default N=32 is not at a local optimum, but the gradient is shallow enough (~5% across 4× volume range) to not matter.

The interesting *non-result* is the cross-cam disagreement on the sign of the higher-N trend (cam0 +5.2% at N=48; cam2 −3.8% at the same N). This is a third example of cam0/cam2 sensitivity asymmetry — but unlike (α), no toggle exceeds the verdict band.

### 3.2. What mode 18 also shows (informational, not in B1 rule)

| cam | N=16 m18 | N=32 m18 | N=48 m18 | N=64 m18 |
|---|---:|---:|---:|---:|
| cam0 | 25.87% (+2.4%) | 25.26% ref | 26.76% (+5.9%) | 26.80% (+6.1%) |
| cam2 | 24.23% (−0.5%) | 24.36% ref | 21.62% (**−11.2%**) | 21.14% (**−13.2%**) |

Mode 18 (cascade_total − PT_total, includes direct lighting) shows cam2 *does* respond meaningfully to higher N — N=48 reduces total Δ by 11.2%, N=64 by 13.2%. But mode 19 (GI-only) shows no comparable movement. **Inference**: the higher-N benefit is in the *direct-lighting* component, not the GI component. More C0 probes resolve direct-light projection edges more accurately, but the GI integration retains the same asymmetric pattern.

This is a B1-rule corner case — the rule was specified on mode 19 alone because mode 19 isolates the GI integration question, which is what the named-hypothesis tree is about. Mode 18 changes are valuable diagnostic info but don't change the verdict.

## 4. Visual cross-check confirms the verdict

Inspected cam2 mode-19 at N=16, N=32, N=64 (8× volume range in probe density) and cam0 mode-19 at N=64:

- **cam2 N=16 vs N=32 vs N=64**: all three images look essentially identical. Same blue spill pattern on the right wall (concentrated against the partition), same red lid trim, same dim pinkish back wall. The blue stripe's *spatial extent* and *saturation* are visually indistinguishable across the 8× volume range. Subtle differences exist (cam2 N=16 has slightly more blue overall, consistent with classifier −4.5%), but no qualitative change in the asymmetric Δ pattern shape.
- **cam0 N=64**: looks essentially the same as cam0 baseline (M0) from prior sweeps — same blue stripe near partition, same red lid, same pink right wall. The +3.0% classifier reading is invisible to the eye.

**Reading**: the cam2 residual Δ pattern is **architecturally invariant to probe density**. It is not caused by under-sampled probe spacing; doubling the probe count does not reduce it. This is a strong rejection — not just a numerical near-miss, but a visual identity across a wide N range.

## 5. The named-hypothesis tree is exhausted

Final hypothesis status, all 4 named candidates out:

| Hypothesis | Status | Evidence |
|---|---|---|
| (γ) angular under-sampling | **REJECTED 2026-05-21** | [cascade_config_sweep_impl.md](cascade_config_sweep_impl.md): D=8→D=16 only −1.0% / −1.4% on cam0/cam2 (need ≥50% to be STRONG_GAMMA). |
| (β) MB-gain | **LEVERAGE NOT CURE 2026-05-22 AM** | [mb_gain_sweep_impl.md](mb_gain_sweep_impl.md): g=2.0 INCREASES Δ-area by +363.4% / +213.6% — wrong sign, larger magnitude. |
| (α) merge-time directional weighting | **LEVERAGE WRONG DIR 2026-05-22 PM** | [alpha_merge_sweep_impl.md](alpha_merge_sweep_impl.md): turning directional-merge OFF *increases* cam2 Δ by +14.6%-19.6% — the merge weighting is doing useful work. |
| (δ) spatial probe density | **REJECT 2026-05-22 PM (this doc)** | All N ∈ {16, 32, 48, 64} within ±10% on both cams. 8× volume range produces visually identical mode-19 output. |

The residual asymmetric cascade-vs-PT Δ pattern that the original full sweep identified (§3.5 in [mbrc_v20_pre_measurement_report.md](mbrc_v20_pre_measurement_report.md)) is **not the result of any single tunable parameter** in the engine as currently architected. Every named knob with leverage either (a) is already at its optimum (α merge, default settings), (b) trades one pathology for another (β gain), or (c) has no leverage at all (γ angular, δ spatial density).

### 5.1. What this means strategically

The MBRC v2.0 measurement-first methodology has paid off in exactly the way the cerebrum entry "Commit decision rules BEFORE running the discriminator sweep" predicted: each pre-committed sweep produced a clean, defensible verdict; the cumulative result is a well-founded *negative answer* to "is this a tuning problem?".

Three forward paths follow:

1. **Accept the residual and ship MBRC v2.0 with cascades at default settings.** Document the residual as the *cascade architecture's intrinsic floor* and rely on the hybrid PT-correction (already shipped, v1.3.1) to close the gap perceptually. Per the [project_mbrc_v20_decisions](../../../C:/Users/XINDONG/.claude/projects/d--GitRepo-My-radiance-cascades-demo/memory/project_mbrc_v20_decisions.md) goal of "hybrid retirement", this is the goal-aligned path *only if* hybrid can be retired despite the cascade residual.
2. **Investigate (ε) per-direction-bin upper-cascade sampling fetch geometry.** The (α) §14.4 observation — bilinear neutral, directional-bin lookup not neutral — suggested the *which texel* matters more than the *blend across it*. (δ) doesn't disprove this; it just rules out a different axis. (ε) needs a new instrumentation pass (per-bin fetch geometry visualization, not just an A/B toggle).
3. **Pivot to honest HDR metrics.** All 4 sweeps used LDR PNG + saturation-band classification. The pre-tonemap radiance ratio (B2 metric) was deferred in [mb_gain_sweep_impl.md](mb_gain_sweep_impl.md) §2.1 because mode 22 (PT-GI-only) doesn't exist. Adding tinyexr + mode 22 (~3-4h) unlocks per-pixel HDR-radiance ratios and may reveal that the LDR-PNG Δ-area floor is itself a tonemap-saturation artifact rather than a real radiance difference.

The recommendation (§7 below) ranks path 3 highest: until the metric is HDR-honest, we cannot distinguish "cascade architecture has an irreducible 20% floor" from "LDR-PNG visualization clips at a 20% floor". The first is a fundamental finding; the second is a measurement artifact.

## 6. Self-critique

### C1. (δ) is the only "clean REJECT" of the four — what does that mean?

(γ) was rejected on a small ≤2% reduction relative to ≥50% STRONG bar. (β) was rejected as "leverage but wrong direction" with +363% / +213%. (α) was MIXED + WRONG_DIR with +14-20%. **(δ) is the only sweep where the data sits flat within ±10% on both cams across the full N range.** This is a different *kind* of rejection: not "the lever points the wrong way" but "the lever doesn't move the output at all."

This matters because the named-hypothesis tree was constructed in §11 of the report as "things that could be wrong with the cascade". A flat REJECT on probe density says probe density is not even *related* to the Δ pattern. This is the strongest possible negative signal — it eliminates a class of fix, not just a parameter value.

### C2. The 4-point grid skipped N=24

Cerebrum lists 8/16/24/32/48/64. The sweep used {16, 32, 48, 64}. N=24 (between 16 and 32) could have shown finer-grained behavior if the data had warranted it; given the data is flat, the finer grid would not have changed the verdict. Tagged §7.3 as optional.

### C3. The mode 18 result (§3.2) is a real finding that doesn't fit the B1 rule

cam2 mode-18 reduction of 11-13% at N=48/64 is meaningful — direct lighting *is* sensitive to probe density, just not the GI integration. This is informational rather than verdict-changing because the named-hypothesis tree was about GI accuracy. But for the v2.0 ship decision, this is a hint that higher-N might give a visual win even with the same GI residual, if direct-light edges are visually important on the scene. Tagged §7.4 for a re-test under hybrid+higher-N to see if perceptual quality improves.

### C4. No bug-230 fix; single-seed gating untriggered

The pre-committed rule said bug-230 (single-seed concern) becomes mandatory only on WEAK_DELTA. Verdict is REJECT, so bug-230 stays deferred. But: if the future (ε) sweep lands in WEAK band, bug-230 must be fixed first (already noted in cerebrum 2026-05-21). Adding to §7.5 as a reminder.

### C5. Sponza re-validation not done

The (δ) sweep ran cornell-orig-alcove only (the v2.0 lock-in scene per [project_mbrc_v20_decisions](../../../C:/Users/XINDONG/.claude/projects/d--GitRepo-My-radiance-cascades-demo/memory/project_mbrc_v20_decisions.md)). Sponza has very different geometry (large interior with many small-detail occluders) and would likely show different probe-density sensitivity. The "cornell-only" decision in the v2.0 lock-in means this is out of scope, but worth documenting that the REJECT verdict is scene-specific to cornell-orig-alcove and would need re-running on any future scene.

### C6. The "DELTA_REJECT → exit named-hypothesis tree" verdict assumes the named hypotheses cover the space

The hypothesis tree (α, β, γ, δ) was constructed in §11 of the report as a brainstorm. There may be additional hypotheses we didn't name. Examples:
- **(ε) per-direction-bin fetch geometry** — flagged in (α) §8.2 but not added to the formal tree until now.
- **The smoothstep blend-zone math** (radiance_3d.comp:771-775) — no toggle exists; would need shader edits to A/B.
- **`uGIStrength` uniform** — the merge formula multiplies `upperDir.rgb * (1.0 - l) * aFactor * uGIStrength`. If this is currently default-1.0 and is a legacy from earlier dev, varying it might be the missing axis.
- **Cascade count itself** — fewer/more cascades might produce different falloff behavior; not tested.

"Exhausted named-hypothesis tree" should be read as "exhausted the *enumerated* tree at session start". §7 below adds the new candidates.

### C7. The classifier (LDR PNG + saturation band) is the same across all 4 sweeps and may carry a systematic bias

All four sweeps measure the same metric (mode 19 Δ-band area on tonemapped LDR captures). If this metric has a known floor at ~20% (because the colormap divisor=0.2 saturates whenever |cascade-PT|>0.2 in radiance space), then anything that drives the radiance difference below 0.2 is invisible — the metric only sees "saturated blue" or "saturated red" or "white". This is a classic post-hoc concern but in this case it's load-bearing: the (δ) flat result might just mean "we already hit the metric's floor; nothing we tune can dip below it because the metric doesn't see below it." The HDR-EXR follow-up (§7.1) is the only way to falsify this concern.

## 7. Open / deferred (next session candidates)

### 7.1. HDR-EXR honest metric — **RECOMMENDED NEXT (highest priority)**

The four-sweep cumulative REJECT story has a confounding hypothesis (C7): the LDR-PNG Δ-area metric may be saturated and flat by construction. Until this is falsified, "(δ) flat" is consistent with both "(δ) really has no leverage" and "(δ) does have leverage but it's all sub-tonemap-floor". Without distinguishing these, any future cascade work is built on uncertain measurement ground.

Path:
- Add tinyexr to engine build (~30 min CMake)
- Add render mode 22 = PT-GI-only output (mirror mode 17 = cascade-GI-only) — ~30 min shader work
- Add `--screenshot-exr` CLI flag that dumps mode 16 + mode 17 + mode 22 as side-by-side EXRs
- New analyzer reads EXR floats, computes `(cascadeGI - ptGI) / max(ptGI, eps)` per pixel, plots histogram + per-pixel quantile statistics
- Re-run a subset of the (α), (β), (δ) sweeps with HDR analysis to see if the post-tonemap floor was hiding signal

Cost: ~4-5h. Yields: definitive "is the 20% LDR-floor real or measurement artifact" answer.

### 7.2. (ε) per-direction-bin upper-cascade sampling fetch geometry — INVESTIGATIVE

Flagged in (α) §8.2 based on the bilinear-neutral / directional-bin-not-neutral observation. Needs a new diagnostic mode (per-bin fetch coordinate visualization) before an A/B can be designed. Not a parameter sweep yet; more like "instrumentation pass first".

### 7.3. Finer (δ) grid (N=8, N=24) — OPTIONAL

If the user wants exact probe-density optimum for v2.0 ship, sweep N ∈ {8, 16, 24, 32, 48, 64} on cam2 mode-19 only = 6 captures, ~1 min. Will not change REJECT verdict; locates the marginal optimum more precisely.

### 7.4. (δ) + hybrid combined re-test — INFORMATIONAL

Run N ∈ {32, 48, 64} × hybrid {0, 1} × cam{0, 2} × mode {0, 18, 19} on cornell-orig-alcove. Tests whether higher-N + hybrid produces visually better output even with the same mode-19 GI residual (since mode-18 direct-light Δ does respond to N). 12 captures, ~3 min. Tagged informational because it doesn't address the named-hypothesis tree — it asks "is there a ship-quality win in tuning N for hybrid users?".

### 7.5. bug-230 fix — UNCHANGED PRE-CONDITION

Stays deferred. Becomes mandatory if a future (ε) sweep or HDR re-run lands in WEAK band on any metric.

### 7.6. New brainstorm: hypotheses not yet on the tree

Per C6 above:
- **smoothstep blend-zone math** — needs a `uUseSmoothstepBlend` toggle (~1h shader work)
- **`uGIStrength` uniform sweep** — likely just rescales output; probably uninteresting but cheap to verify
- **cascade count sweep** — vary cascadeCount ∈ {2, 3, 4 (default), 5}; ~30 min CLI wiring if not already exposed

## 8. Files touched

| File | Change | Lines |
|---|---|---|
| [tools/v20_pre_measurement/delta_probe_density_sweep.ps1](../../tools/v20_pre_measurement/delta_probe_density_sweep.ps1) | NEW — 16-capture driver | 79 |
| [tools/v20_pre_measurement/analyze_delta_probe_density.py](../../tools/v20_pre_measurement/analyze_delta_probe_density.py) | NEW — analyzer with bidirectional + one-sided detection | 178 |
| [tools/v20_pre_measurement/delta_probe_density_results.json](../../tools/v20_pre_measurement/delta_probe_density_results.json) | NEW — raw 16-capture JSON | ~200 |
| doc/7/mbrc_v20_pre_measurement_report.md | §15 added (pending separate commit) | TBD |
| [.wolf/cerebrum.md](../../.wolf/cerebrum.md) | DNR addendum: cerebrum-toggle-search must cover CLI parsers + setters, not just shader uniforms | +1 entry |
| [.wolf/memory.md](../../.wolf/memory.md) | session log entries | +N |

Engine: zero changes. `--cascade-c0-res=` already shipped during Step 12 scaling experiment.
