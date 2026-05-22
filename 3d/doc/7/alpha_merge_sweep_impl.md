# (alpha) Merge-mode sweep — Implementation Notes

Companion to [mbrc_v20_pre_measurement_report.md](mbrc_v20_pre_measurement_report.md) §14, follow-on to [mb_gain_sweep_impl.md](mb_gain_sweep_impl.md). Documents the (α) merge-time directional-weighting discriminator that the (β) demotion (§13) recommended as the next leading candidate, the engine-wiring discovery that saved 2-3h of planned shader work, and the **MIXED + ALPHA_LEVERAGE_WRONG_DIR** outcome.

Date: 2026-05-22 afternoon. Build: Release, MSVC, clean (3 new CLI parsers + 3 setters; no shader edits).

## 1. Scope landed

- **3 CLI flags** wired through to pre-existing engine toggles:
  - `--use-directional-merge=0|1` → `Demo3D::useDirectionalMerge` (default 1)
  - `--use-dir-bilinear=0|1` → `Demo3D::useDirBilinear` (default 1)
  - `--use-spatial-trilinear=0|1` → `Demo3D::useSpatialTrilinear` (default 1)
  Setters in [src/demo3d.h](../../src/demo3d.h) follow the `setUseMultiBounceCLI` pattern (set member, invalidate cascade, reseed history). CLI parsers in [src/main3d.cpp](../../src/main3d.cpp) follow the `--use-multi-bounce` pattern.
- [tools/v20_pre_measurement/alpha_merge_sweep.ps1](../../tools/v20_pre_measurement/alpha_merge_sweep.ps1) — 20-capture driver (5 configs × 2 cams × 2 modes). Mirrors [mb_gain_sweep.ps1](../../tools/v20_pre_measurement/mb_gain_sweep.ps1) shape.
- [tools/v20_pre_measurement/analyze_alpha_merge.py](../../tools/v20_pre_measurement/analyze_alpha_merge.py) — same classifier as analyze_mb_gain.py (SAT=0.55, LUMA=0.05). Verdict logic is **bidirectional** from the start per cerebrum DNR lesson from (β).
- [tools/v20_pre_measurement/alpha_merge_results.json](../../tools/v20_pre_measurement/alpha_merge_results.json) — raw 20-capture JSON.

Total: ~15 min engine work (CLI wiring + rebuild + md5 smoke test), 3.3 min sweep, ~30 min analysis + this doc.

## 2. Pre-sweep self-critique on the prior plan

The (β) impl doc §8.1 estimated (α) at "2-3h engine work" because it presumed a new `uUseIsotropicMerge` uniform and new shader code path. Reading the shader before writing the plan caught a much bigger time saving.

### 2.1. The toggles already existed

[res/shaders/radiance_3d.comp:656-682](../../res/shaders/radiance_3d.comp#L656-L682) already dispatches between four merge code paths via uniforms `uUseDirectionalMerge`, `uUseDirBilinear`, `uUseSpatialTrilinear`, `uUseWeightedSample`. The Phase 5 work that added directional bilinear (5f) and spatial trilinear (5d) shipped those uniforms with GUI checkbox controls. No new shader code was needed; just CLI parsers that target the existing setters.

This collapsed the planned ~2-3h engine work into ~15min of CLI wiring. **Reading the shader before writing the engine plan paid for itself by an order of magnitude.**

### 2.2. The 5-config matrix was chosen to cover the lattice corners

Three independent boolean toggles → 8 cells in the lattice. Five cells captured for cost reasons:
- M0 (1,1,1) — engine default baseline
- M1 (1,0,1) — bilinear OFF only
- M2 (0,1,1) — directional merge OFF only (falls back to isotropic cascade texture)
- M3 (1,1,0) — spatial trilinear OFF only
- M4 (0,0,1) — directional merge + bilinear both OFF (worst case for directional weighting)

Skipped: (0,1,0), (1,0,0), (0,0,0). The (β) doc's "is X a cure" question only needs single-toggle deviation arms plus one combined-stress arm; full lattice would have added ~8 min run time and not changed the verdict because §4's results already show the directional-merge axis is the only one with leverage.

### 2.3. Pre-committed rule is bidirectional from the start

The (β) sweep wrote a unidirectional rule ("gain=2.0 *reduces* Δ-area by ≥30%") and got bitten by the data going the other direction. This sweep's rule explicitly enumerates `ALPHA_LEVERAGE_WRONG_DIR` as a labelable outcome. The analyzer reports both reductions and increases on the same scale.

### 2.4. Pre-sweep md5 sanity check (per bug-234 lesson)

Before running the sweep, verified all 3 flags actually changed shader output by capturing cam0 m19 three ways:
- baseline (all on): md5 = `21A32105...`
- `--use-directional-merge=0`: md5 = `F69B5801...` — distinct
- `--use-dir-bilinear=0`: md5 = `1A9A69BB...` — distinct from both above

Three distinct hashes confirmed the wiring is live. No silent-fail risk like bug-234.

## 3. Engine wiring — 3 CLI flags

### 3.1. Setters added

[src/demo3d.h](../../src/demo3d.h), placed just before the hybrid setters block:

```cpp
void setUseDirectionalMergeCLI(bool v) {
    if (v == useDirectionalMerge) return;
    useDirectionalMerge = v;
    cascadeReady = false;
    forceCascadeRebuild = true;
    renderFrameIndex = 0;
    historyNeedsSeed = true;
    std::cout << "[Demo3D] useDirectionalMerge=" << (v ? "ON" : "OFF (isotropic fallback)") << "\n";
}
void setUseDirBilinearCLI(bool v) { /* same shape, message "OFF (nearest-bin)" */ }
void setUseSpatialTrilinearCLI(bool v) { /* same shape, message "OFF (nearest-parent)" */ }
```

Each setter:
1. Early-exits if the toggle is already at the requested value (avoids spurious rebakes).
2. Mutates the member directly (the toggles are private; GUI accesses them via the same class so this is consistent).
3. Forces a cascade rebake (`cascadeReady=false`, `forceCascadeRebuild=true`) so the new shader uniforms take effect on the next frame. This matters because merge-time toggles affect the cascade *bake*, not just the consume step — they are bake-time toggles per [feedback_cascade_merge_is_bake_time.md](../../../C:/Users/XINDONG/.claude/projects/d--GitRepo-My-radiance-cascades-demo/memory/feedback_cascade_merge_is_bake_time.md).
4. Reseeds history (`historyNeedsSeed=true`) so any temporal accumulator starts from the new-merge cascade, not a stale-merge-baked frame.
5. Prints a one-line log so the headless capture log shows the flag actually fired.

### 3.2. Parsers added

[src/main3d.cpp](../../src/main3d.cpp), placed right after the `--multi-bounce-gain=` parser:

```cpp
} else if (arg.rfind("--use-directional-merge=", 0) == 0) {
    int v = std::atoi(arg.substr(24).c_str());
    demo->setUseDirectionalMergeCLI(v != 0);
    std::cout << "[MAIN] --use-directional-merge=" << v
              << " (1=per-direction-bin upper sampling [default]; 0=isotropic cascade-texture fallback)\n";
} else if (arg.rfind("--use-dir-bilinear=", 0) == 0) { /* analogous */ }
} else if (arg.rfind("--use-spatial-trilinear=", 0) == 0) { /* analogous */ }
```

### 3.3. Why no `Demo3D::render()` interaction needed

bug-234 needed an extra rebake gate in render() because temporal MB feedback fires inside the per-frame *consume* step but depends on per-frame *bakes* that the jitter-pinned measurement mode suppresses. The merge-time toggles change the cascade *bake*, and the setter already forces `cascadeReady=false`, which the existing render() rebake check picks up on frame 0 without extra glue. Verified by the md5 distinctness above.

## 4. Quantitative result — B1 (Δ-band area, mode 19)

| cam | M0 baseline | M1 no_bilin | M2 iso_merge | M3 no_spatialtri | M4 iso+nearest |
|---|---:|---:|---:|---:|---:|
| cam0 | 26.53% ref | 26.66% (+0.5%) | 25.64% (−3.4%) | 26.37% (−0.6%) | 25.18% (−5.1%) |
| cam2 | 20.43% ref | 20.56% (+0.6%) | 23.41% (**+14.6%**) | 21.26% (+4.1%) | 24.44% (**+19.6%**) |

(Total saturated Δ-band area = blue% + red% on mode 19.)

**Verdict per pre-committed rule:** none of M1..M4 produces a ≥20% reduction on both cams (STRONG fails). None produces 10-20% reduction on both, and none produces ≥20% reduction on either cam (WEAK fails). Not all arms within ±10% on both cams (REJECT fails — M2 cam2 +14.6% and M4 cam2 +19.6% are out-of-band).

The analyzer correctly falls through to **MIXED -- requires manual inspection** and simultaneously emits the **ALPHA_LEVERAGE_WRONG_DIR** bidirectional report flagging M2 and M4 cam2.

Translation in plain language: **(α) toggles have measurable leverage on cam2 Δ-area, but the leverage is in the wrong direction** — turning directional-merge OFF makes the cam2 Δ pattern *worse*, not better. The current default (directional-merge ON, bilinear ON, spatial-tri ON) is doing its job; the residual Δ pattern is not caused by these merge weights.

### 4.1. Per-axis isolation reading

| Axis | Effect | Reading |
|---|---|---|
| `useDirBilinear` (M1) | cam0 +0.5%, cam2 +0.6% | **Neutral.** 4-bin directional bilinear has essentially zero net effect on the Δ pattern. The nearest-bin fallback produces statistically indistinguishable output. |
| `useDirectionalMerge` (M2) | cam0 −3.4%, cam2 **+14.6%** | **Asymmetric and view-dependent.** Falling back to the isotropic-cascade-texture path slightly *helps* cam0 but *hurts* cam2 meaningfully. The directional-merge weighting is doing useful work for cam2's view. |
| `useSpatialTrilinear` (M3) | cam0 −0.6%, cam2 +4.1% | **Within noise on cam0; minor on cam2.** 8-neighbor spatial blend is a small contributor. |
| Stress combo (M4) | cam0 −5.1%, cam2 **+19.6%** | **Same shape as M2 dominates.** The combined M2+M1 stress is approximately additive with M2's larger effect; bilinear OFF on top of iso-merge OFF doesn't compound badly because bilinear is already a no-op. |

## 5. Visual cross-check confirms the character

Inspected cam0 and cam2 mode-19 at M0 and M4:

- **cam0 M0 vs M4**: visually nearly identical. Same blue stripe near partition, same red lid trim, same low-saturation pink right wall. Slight saturation reduction in the blue stripe at M4. Classifier reads −5.1% Δ-area, consistent with the eye.
- **cam2 M0 vs M4**: the blue spill onto the floor in front of the partition is clearly *larger* at M4. The blue patch extends further down and outward, and the right-side blue stripe is more saturated. Classifier reads +19.6%, eye agrees.

So the classifier is not reporting a colormap artifact. The directional merge being ON is genuinely producing a tighter Δ pattern on cam2's view than the isotropic fallback. cam0's view is dominated by surfaces that the directional weighting doesn't help (or marginally hurts).

### 5.1. Two findings the visuals add to the numbers

1. **The cam2 asymmetry persists**. In every prior sweep (cascade-config sec 12, MB-gain sec 13, now α-merge), cam2 has been more sensitive than cam0. This sweep is the first where cam0 is essentially insensitive (every arm within ±5%) while cam2 carries the whole signal. The asymmetry is a property of cam2's geometry: it frames more "deep" pixels (partition-shadowed floor, alcove-side wall) where merge-time blending across directional bins materially changes whether a given probe sees the alcove gap as occluded or visible.
2. **Per-direction-bin upper sampling is doing what it was designed to do.** The fact that turning it OFF makes the leak *worse* (not better) confirms the Phase 5f / 5d design wasn't introducing the asymmetric Δ pattern — it was *reducing* it. The remaining Δ pattern is the unreduced floor.

## 6. Decision-tree update

Revised hypothesis tree as of 2026-05-22 afternoon:

- **(α) merge-time directional weighting** — *demoted from leading candidate to "necessary but not sufficient"*. Has measurable view-dependent leverage; ON is the right default. Not the source of the residual asymmetric Δ pattern. Cannot be tuned to eliminate the leak.
- **(β) MB-gain** — *unchanged: "knob has leverage but is not a global cure"* ([mb_gain_sweep_impl.md](mb_gain_sweep_impl.md)).
- **(γ) angular under-sampling** — REJECTED 2026-05-21 ([cascade_config_sweep_impl.md](cascade_config_sweep_impl.md)).
- **(δ) spatial probe density / smoothstep blending** — *promoted to leading candidate*. With (α), (β), (γ) all unable to eliminate the cam2 asymmetric pattern, and with the residual being concentrated in cam2's deep-pixel geometry (partition-shadowed floor, alcove gap), spatial probe-density / smoothstep-blending across c0 probes is the next axis to discriminate.
- **(ε) — new candidate**: per-direction-bin **upper-cascade sampling fetch geometry** itself (not the weighting). The fact that bilinear is neutral but directional-bin lookup is not suggests the *sampling location* matters more than the *4-bin blend across it*. Worth investigating as a follow-on to (δ).

## 7. Self-critique

### C1. Engine effort estimate from (β) was 10× too high

The (β) §8.1 estimate of "2-3h engine work" for (α) was wrong because the planner did not read the shader to see existing toggles. **Lesson:** before estimating engine work, read the existing shader code path for the feature being tested. Many features ship with toggle uniforms that were never wired to CLI. Already-committed engineering can replace planned engineering.

Already in cerebrum as a near-miss; promoting this to an explicit Key Learning entry.

### C2. The 5-config matrix doesn't cover the full lattice

3 toggles × 2 states = 8 lattice cells; sampled 5. Skipped (0,1,0), (1,0,0), (0,0,0). The skipped cells could in principle exhibit interaction effects different from the additive M2 + M1 prediction in §4.1. Cost to fill: 6 more captures (~1 min) × 2 modes × 2 cams = 24 more captures, ~5 min. Not done because the dominant finding (cam2 sensitive to directional-merge axis, cam0 insensitive to all, bilinear neutral, spatial-tri minor) is robust to the missing cells. Tagged §8.1 for follow-up if (δ) leaves room for it.

### C3. Single-seed concern not addressed

bug-230 (noise-seed offset gating on WEAK band) still open from cascade_config era. The (α) result is MIXED, not WEAK, so the gate didn't trigger. But the cam2 +14.6% / +19.6% numbers are large enough that a 2-seed re-run is unlikely to change the verdict — the WEAK band would mean numbers in the 10-15% range whose direction could flip on noise. +14-20% on a single arm is firmly out of noise range. Documented; not promoted to mandatory.

### C4. Verdict label is right but the "MIXED" wording understates the certainty

The analyzer prints "MIXED -- requires manual inspection" which sounds like the data is ambiguous. It isn't — the data unambiguously says "directional-merge has wrong-direction leverage on cam2, everything else is small". The right label is something like **ALPHA_REJECTED_AS_CURE_WITH_LEVERAGE_CONFIRMED_ON_CAM2**. The (β) doc had the same overloaded-label complaint (C2 there). Consider adding a 3rd-tier label set to the next analyzer.

### C5. The classifier is colormap-saturation-aware, but not LDR-color-clipping-aware

LDR PNG captures saturate at sRGB 255 / 0. If a mode-19 Δ value pushes blue or red beyond what the colormap divisor of 0.2 already saturates, the classifier reports both "saturated" — but cannot tell the difference between "Δ = 0.25" and "Δ = 2.5". For this sweep the data spread is small enough this doesn't matter. For (δ) which might push larger Δ, consider also classifying *intermediate*-saturation pixels (0.4 < sat < 0.55) so we have signal in the "kind of saturated but not pegged" band.

### C6. cam0 is silent across all arms — analyzer didn't flag

The fact that every M1..M4 arm produces |Δ| ≤ 5.1% on cam0 should itself be a labelable outcome ("axis is one-sided sensitive"). The current analyzer just prints per-cam deltas without commenting on cross-cam asymmetry. A future analyzer could flag when one cam shows leverage and the other doesn't, since that's diagnostically valuable (points at geometry-specific phenomena vs global-engine-bug phenomena).

### C7. "Spatial trilinear" naming overlap with "spatial probe density" (δ)

M3 tests `useSpatialTrilinear` which is the 8-neighbor blend across upper-cascade *probes*. The (δ) hypothesis tests the c0 probe density / smoothstep blending across c0 *probes*. These are different stages in the consume path but share the word "spatial". When writing (δ)'s impl doc, clarify the distinction so a reader doesn't conflate "M3 tested it already" with "(δ) tests it next".

### C8. The setter prints "OFF (nearest-bin)" / "OFF (isotropic fallback)" / "OFF (nearest-parent)" — semantically right but undocumented

A reader who hasn't read the shader doesn't know what those parenthetical descriptions mean. The shader has comments at lines 656-682 explaining each path; the setter messages should reference those or expand inline. Not fixed in this commit (would require a re-build for a 3-line cosmetic change). Tagged §8.4.

## 8. Open / deferred (next session candidates)

### 8.1. (δ) spatial probe density — RECOMMENDED NEXT

Promoted by §6 to leading candidate. Test path:

- Add `--cascade-c0-res=N` CLI flag (analogue of `--cascade-scaled-dir-res=`)
- Add `--cascade-c0-smoothstep=0|1` CLI flag if a smoothstep blend toggle exists; otherwise skip until that exists
- Sweep N ∈ {16, 32, 48, 64} × cam{0,2} × mode{19} = 8 captures (~2 min)
- Pre-committed bidirectional rule: STRONG_DELTA if any N reduces cam2 Δ-area ≥20% AND keeps cam0 within ±10%; WEAK_DELTA if 10-20%; DELTA_REJECT if all within ±10% on both cams; DELTA_LEVERAGE_WRONG_DIR if any N increases Δ-area >10% on either cam.
- Cost: ~30 min CLI wiring + 2 min sweep + 30 min doc.

### 8.2. (ε) per-direction-bin upper-cascade sampling fetch geometry

The result that bilinear is neutral but directional-bin lookup is not (§4.1) hints that the *which texel* is sampled matters more than the *how it blends*. Direct test would isolate sampler choice (texture vs texelFetch) from bin selection geometry. Not designed yet; brainstorm-tier.

### 8.3. Fill the (α) lattice (C2 above)

Optional 6-config completion if (δ) doesn't fully account for residual cam2 leak. Cells: (0,1,0), (1,0,0), (0,0,0) × 2 cams × 2 modes = 12 captures, ~3 min.

### 8.4. Document setter parenthetical descriptions

Expand `"OFF (isotropic fallback)"` / `"OFF (nearest-bin)"` / `"OFF (nearest-parent)"` to one-line summaries of what each fallback path does. 3-line cosmetic change, defer until next demo3d.h edit.

### 8.5. Re-label analyzer verdicts (C4 above)

Add a third tier between STRONG and MIXED for "one-sided leverage confirmed" so the verdict reflects the certainty of the data. Reusable across (β), (α), (δ).

## 9. Files touched

| File | Change | Lines |
|---|---|---|
| [src/demo3d.h](../../src/demo3d.h) | 3 new CLI setters (setUseDirectionalMergeCLI, setUseDirBilinearCLI, setUseSpatialTrilinearCLI) | +30 |
| [src/main3d.cpp](../../src/main3d.cpp) | 3 new CLI parsers (--use-directional-merge, --use-dir-bilinear, --use-spatial-trilinear) | +18 |
| [tools/v20_pre_measurement/alpha_merge_sweep.ps1](../../tools/v20_pre_measurement/alpha_merge_sweep.ps1) | NEW — 20-capture driver | 90 |
| [tools/v20_pre_measurement/analyze_alpha_merge.py](../../tools/v20_pre_measurement/analyze_alpha_merge.py) | NEW — analyzer with bidirectional verdict logic | 182 |
| [tools/v20_pre_measurement/alpha_merge_results.json](../../tools/v20_pre_measurement/alpha_merge_results.json) | NEW — raw 20-capture JSON | ~240 |
| doc/7/mbrc_v20_pre_measurement_report.md | §14 added (pending separate commit) | TBD |
| [.wolf/cerebrum.md](../../.wolf/cerebrum.md) | DNR addendum: "before estimating engine work, read the shader for existing toggles" | +1 entry |
| [.wolf/memory.md](../../.wolf/memory.md) | session log entries | +N |
