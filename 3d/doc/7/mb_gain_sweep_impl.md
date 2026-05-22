# MB-gain sweep — Implementation Notes

Companion to [mbrc_v20_pre_measurement_report.md](mbrc_v20_pre_measurement_report.md) (pending §13) and follow-on to [cascade_config_sweep_impl.md](cascade_config_sweep_impl.md). Documents the (β) MB-gain discriminator sweep, the bug-234 fix that made the sweep meaningful, and the surprise result that demanded a verdict-rule rewrite mid-flight.

Date: 2026-05-22 morning. Build: Release, MSVC, clean (one-line fix to `Demo3D::render`).

## 1. Scope landed

- **bug-234 fix** ([src/demo3d.cpp:984-994](../../src/demo3d.cpp#L984-L994)) — 4-line conditional that forces per-frame cascade re-bake when `measurementCamera >= 0 && useMultiBounce`. Without it, MB temporal feedback is silently OFF in every headless measurement run. See §3.
- [tools/v20_pre_measurement/mb_gain_sweep.ps1](../../tools/v20_pre_measurement/mb_gain_sweep.ps1) — 20-capture driver (5 gains × 2 cams × 2 modes). Mirrors [cascade_config_sweep.ps1](../../tools/v20_pre_measurement/cascade_config_sweep.ps1) but with `multiBounceGain` axis and `--use-multi-bounce=1` required.
- [tools/v20_pre_measurement/analyze_mb_gain.py](../../tools/v20_pre_measurement/analyze_mb_gain.py) — same classifier as analyze_cascade_config.py (SAT=0.55, LUMA=0.05) plus mean-foreground-luma column as a B2-lite proxy.
- [tools/v20_pre_measurement/mb_gain_results.json](../../tools/v20_pre_measurement/mb_gain_results.json) — raw 20-capture JSON.

Total: ~30 min engine work (bug-234 investigation + fix + rebuild + re-test), 5.5 min sweep, ~45 min analysis + this doc.

## 2. Pre-sweep self-critique on the prior plan

Three issues caught BEFORE the sweep ran, by reading existing code rather than trusting the plan:

### 2.1. B2 (floorRatio) is not directly constructible

The improved plan in [cascade_config_sweep_impl.md §8.1](cascade_config_sweep_impl.md) defined B2 = per-pixel cascade-GI / PT-GI in the under-illuminated foreground. Investigation showed:

- Cascade GI is available standalone via render mode 17 (`modeColor = indirectColor` at [raymarch.frag:1000](../../res/shaders/raymarch.frag#L1000)).
- PT GI is computed *inside* the mode 19 and mode 20 shaders as `max(ptFull - ptDirect, 0)` ([raymarch.frag:817](../../res/shaders/raymarch.frag#L817)), but is NEVER exposed as a standalone output. There is no symmetric "mode 22 = PT GI only".
- Even if mode 22 were added (~30 min shader work), PNG screenshots go through ACES tonemap + sRGB gamma, so per-pixel-luma ratios are not the HDR-radiance ratios B2 claimed to measure. Honest B2 needs EXR dump (deferred in §8.2 of the prior doc).

**Decision:** Ship B1 only. Add a `mean_fg_luma` column to the analyzer as a "B2-lite" sanity number — it answers *"does gain actually change overall mode-19 luminance"* without claiming to be the HDR ratio. Document the B2 deferral clearly so a future session knows the metric is missing, not omitted.

### 2.2. The sweep matrix was specified before the engine wiring was verified

The 5×2×2 matrix in §8.1 assumed `--use-multi-bounce=1` + `--multi-bounce-gain=X` would compose cleanly. Section 3 below documents the silent failure mode this assumption hid.

### 2.3. Single-seed concern was real but not in the way I expected

The plan said "MB feedback accumulates noise temporally → single-seed concern sharper for (β)". Correct in principle. But the actual single-seed failure mode (bug-234) was *no MB feedback at all*, not noise-amplification. The seed concern is downstream of the rebake concern.

## 3. bug-234 — the discriminator that almost ran on nothing

### 3.1. Symptom

Pre-sweep sanity: capture `cam0 m19` at gain=1.0 and gain=2.0 with everything else fixed. Compare md5.

```
26005a6c2e82b8f4523c028e8be5a1db *sanity_g100.png
26005a6c2e82b8f4523c028e8be5a1db *sanity_g200.png
```

**Bit-identical.** Same class as bug-212 from the v1.3.1 NEE precedent: a CLI flag that parses cleanly, fires its setter, prints its log line, but has zero effect on the render. If this had not been caught by a sanity hash check, the full sweep would have produced 20 captures of effectively-the-same image and the analyzer would have output a clean "BETA_REJECT delta=0%" verdict for an entirely fictional reason.

### 3.2. Root cause

Three things had to align for MB feedback to fire in `radiance_3d.comp:547`:

```glsl
if (uUseMultiBounce != 0 && uHasPrevFrame != 0) {
    color += albedo * stochSample * uMultiBounceGain;
}
```

The C++ gate at [demo3d.cpp:2556](../../src/demo3d.cpp#L2556):

```cpp
bool hasFeedback = useMultiBounce && c0HistTex != 0 && !historyNeedsSeed;
```

In headless measurement mode:

- Frame 0: `cascadeReady=false` (set by CLI setters), `historyNeedsSeed=true` (also set). Bake runs but `hasFeedback=false` → MB gated OFF this single bake.
- After frame 0: `cascadeReady=true`, `historyNeedsSeed=false` (cleared at end of `updateRadianceCascades` line 2447).
- Frames 1..N: `cascadeReady=true`. No bake. No further chance for MB to fire.

Why no rebake-trigger fires after frame 0: probe jitter advance (line 974) is the only per-frame `cascadeReady=false` invalidator. With `measurementCamera >= 0` (line 962), jitter is pinned to zero. No advance, no invalidation, no bake.

The Phase MB GUI tooltip says "each frame's bake includes previous frame's indirect" — which is true *with jitter on*, but silently false in measurement mode. Phase MB was developed against the jittered interactive path; the headless measurement path was never the design target.

### 3.3. Fix

Four lines in [src/demo3d.cpp:986-994](../../src/demo3d.cpp#L986-L994):

```cpp
if (measurementCamera >= 0 && useMultiBounce) {
    cascadeReady = false;
}
```

Verified post-fix: md5(`sanity_g100`)=`2fd0a272..`, md5(`sanity_g200`)=`5118d014..`. Distinct. Also distinct from the pre-fix `26005a6c..` value, ruling out the alternative hypothesis "MB was always on but the gain knob was the broken one".

Cost: ~5-15s per 512-frame capture at scaled-D=8 cornell (bake ~10-30ms × 512 frames). The 20-capture sweep landed in 5.5 min, fully acceptable.

### 3.4. Why this took 30 minutes and not 30 seconds

The setter at [demo3d.h:670](../../src/demo3d.h#L670) sets cascadeReady=false correctly. The CLI parser at [main3d.cpp:417](../../src/main3d.cpp#L417) calls the setter correctly. The shader uniform at [demo3d.cpp:2558](../../src/demo3d.cpp#L2558) writes the gain value correctly. The shader gate at [radiance_3d.comp:547](../../res/shaders/radiance_3d.comp#L547) reads both uniforms correctly. **Every individual component was correct in isolation.** The bug was in the temporal interaction between the bake-trigger system and the MB-feedback gate — a property of the *control flow*, not any single function.

Lesson worth a cerebrum entry: when a feature works in the GUI but produces identical output in headless measurement runs, the failure is almost certainly in the "what triggers a frame to do work" layer, not the "did the work do the right thing" layer.

## 4. Quantitative result — B1 (Δ-band area, mode 19)

| cam | g=0.5 | g=1.0 | g=1.5 | g=2.0 | g=3.0 | (g2−g1)/g1 |
|---|---:|---:|---:|---:|---:|---:|
| cam0 | 24.33% | 21.55% | **56.44%** | **99.86%** | 99.89% | **+363.4%** |
| cam2 | 21.27% | 22.87% | **30.22%** | **71.72%** | 71.75% | **+213.6%** |

(All numbers are total saturated Δ-band area = blue% + red% on mode 19.)

Color breakdown (blue% / red%) reveals the *character* of the change:

| cam,gain | blue% | red% | reading |
|---|---:|---:|---|
| cam0, g=0.5 | 24.14% | 0.19% | almost pure under-illumination |
| cam0, g=1.0 | 19.67% | 1.88% | the §3.5 finding (mostly blue) |
| cam0, g=1.5 | 2.46%  | 53.98% | pattern FLIPPED to mostly red |
| cam0, g=2.0 | 0.07%  | 99.79% | runaway over-illumination |
| cam0, g=3.0 | 0.07%  | 99.81% | pegged (cannot saturate further) |

Same pattern on cam2 (less extreme magnitude — only ~71% red at g=2 because cam2 sees more pixels far enough from the partition that even runaway MB doesn't exceed the divisor=0.2 threshold there).

**Verdict per pre-committed rule:** the rule was specified as "STRONG_BETA if gain=2.0 *reduces* mode-19 Δ-area by ≥30% on both cams". The empirical reduction is **+363.4% and +213.6%** — opposite-signed and out-of-band on both axes. The pre-committed VERDICT enum has no branch for this case; the analyzer falls through to "MIXED -- requires manual inspection".

This is the *interesting* failure mode of pre-committed rules: writing them as unidirectional ("reduce by ≥30%") presumes the direction of effect is known. When the effect is the opposite sign and several times the magnitude, the rule labels it MIXED even though the data is unambiguous. The right label here is **BETA_REJECT_AS_GLOBAL_CURE** with an explanatory note.

## 5. Visual cross-check confirms the character

Inspected cam0 mode-19 at g=0.5, 1.0, 1.5, 2.0; cam2 at g=1.0 and g=2.0:

- **g=0.5**: heavily blue across the entire visible volume — cascade is more under-illuminated than the default, as expected (less feedback → less indirect energy → cascade GI floor sits lower than PT).
- **g=1.0 (engine default, matches the §3.5 finding)**: structured asymmetric blue (left wall, partition, alcove gap) + small red regions (right wall).
- **g=1.5**: visual pattern *reorganizes* — most of the room is now red (over), with a vertical blue stripe near the partition gap where it was strongest under-illuminated at g=1.0.
- **g=2.0**: ENTIRE visible foreground saturated red. Tiny blue speckles near the floor that survive the runaway.
- **g=3.0**: indistinguishable from g=2.0 (already past saturation).

**Three findings the visuals add to the numbers:**

1. **Strong nonlinear leverage.** Going from g=1.0 to g=2.0 doesn't double the brightness — it pegs the heatmap divisor. The Phase MB GUI tooltip predicts gain=2.0 → +9.9% scene brightness vs OFF. The mode-19 sees |cascade-PT| > 0.2 (divisor) across nearly the entire foreground, which is several times the +9.9% prediction. The tooltip's empirical numbers are from a different scene or predate cascade-bake changes; either way, the relationship between gain and cascade-GI-vs-PT-Δ is not a simple linear scale.
2. **Pattern reorganization at g=1.5**, not just intensity shift. The blue stripe near the partition gap at g=1.5 occupies roughly the same spatial extent as the strong blue region at g=1.0, while the surrounding pixels have all flipped to red. If MB-gain were a pure global multiplier of cascade GI, the |Δ| pattern should shift uniformly in luminance space — the *shape* should not reorganize. The fact that it does reorganize suggests MB feedback interacts with the merge-weighting path nontrivially (potential evidence for (α)).
3. **The U-shape has its minimum at or below g=1.0** on cam2 (g=0.5 has lower total% than g=1.0 there), and at or near g=1.0 on cam0. The current default is already close to the local Δ-area optimum on this scene — *not* the under-tuned value the plan presumed.

## 6. Decision-tree update

Revised hypothesis tree as of 2026-05-22 morning:

- **(α) merge-time directional weighting** — *promoted to leading candidate*. The pattern reorganization at g=1.5 (§5 finding 2) is consistent with MB-gain x merge-weight interaction. Direct test still requires the isotropic-merge A/B flag in [radiance_3d.comp](../../res/shaders/radiance_3d.comp) (~2-3h engine work).
- **(β) MB-gain** — *demoted to "knob has leverage but is not a cure"*. The gain affects cascade GI dramatically but cannot shrink the asymmetric Δ pattern at any tested value: gain<1 keeps under-illumination, gain>1.5 trades it for over-illumination. The default g=1.0 is already near the local Δ-area optimum on cornell-orig-alcove. Not eliminated as a contributor; eliminated as the single global fix.
- **(γ) angular under-sampling** — REJECTED 2026-05-21 ([cascade_config_sweep_impl.md](cascade_config_sweep_impl.md)).
- **(δ) spatial probe density / smoothstep blending** — *unchanged; still untested*. Add `--cascade-c0-res=N` CLI to discriminate after (α).

## 7. Self-critique

### C1. The B1 verdict rule was malformed

"STRONG_BETA: gain=2.0 reduces Δ-area by ≥30%" embeds a directional assumption (gain↑ → Δ↓). The data shows gain↑ → Δ↑ at every tested gain ≥1.5. The right rule is **bidirectional**: "(β) is a cure if there EXISTS a tested gain where Δ-area on both cams is ≤X% of the gain=1.0 baseline." With X=70%, no tested gain qualifies, so BETA_REJECT_AS_GLOBAL_CURE. Document this rule-shape fix in the cerebrum: pre-commit rules must enumerate the failure modes including "knob has leverage but wrong direction", not assume the proposed direction.

### C2. The verdict label is overloaded

"BETA_REJECT" as printed by the analyzer is too narrow — it suggests (β) has no relation to the Δ pattern. The data shows the opposite: (β) has THE STRONGEST leverage on the Δ pattern of any axis tested so far. The correct nuanced verdict is something like "BETA_LEVERAGE_CONFIRMED_BUT_NOT_A_GLOBAL_CURE". The analyzer's enum doesn't have a slot for this; the human-readable §13 of the report carries the nuance.

### C3. The sweep matrix missed g=1.1, g=1.2, g=1.3, g=1.4

The 5-value spacing was {0.5, 1.0, 1.5, 2.0, 3.0}. The interesting transition happens between 1.0 and 1.5 (where cam0 jumps from 21.55% to 56.44%, and the visual pattern flips). A finer-grained sweep at {1.0, 1.1, 1.2, 1.3, 1.4, 1.5} would let us locate the Δ-area minimum more precisely AND see whether the pattern-reorganization-not-shift behavior persists smoothly. Not done because: (a) the dominant finding (g≥1.5 saturates) is robust to grid spacing, (b) the global verdict (BETA not a cure) doesn't change, (c) ~5 more minutes is not free but is cheap if the user wants it. Tagged as deferred §8.2.

### C4. mean_fg_luma is the wrong B2-lite proxy

Mode 19 encodes Δ via bipolar colormap: white = Δ near 0, saturated red = positive Δ, saturated blue = negative Δ. The luma of "saturated red" = `0.2126*1 + 0.7152*0.2 + 0.0722*0` ≈ 0.36. White ≈ 1.0. So mean_fg_luma *decreases* as Δ-band area increases — the trend in the table (0.77 → 0.36 as gain goes 1.0 → 2.0) is just the colormap saturating, not the underlying brightness dropping. Reading mean_fg_luma as "B2-lite brightness signal" is wrong; it's "B1-restated-in-luma-space". Replace in a future sweep with mode-17 cascade-GI absolute luma vs mode-16 PT-full absolute luma.

### C5. cam2 saturation level (~72% red at g=2) differs from cam0 (~100%) — interesting but unexplained

cam2's geometry exposes more pixels that survive the runaway. Three plausible reasons: (a) cam2 frames more "deep" pixels far from light sources where even runaway MB doesn't exceed divisor=0.2; (b) cam2 has more background letterbox proportionally; (c) cam2's first-hit surfaces include more red-wall pixels that already match PT closely. Not investigated. Worth a one-paragraph follow-up if the next sweep's verdict depends on cam2's specifics.

### C6. The 5-point sweep does NOT discriminate (α) from "(β)+(α) interaction"

§5 finding 2 (pattern reorganization at g=1.5) is consistent with two stories:
- Story A: MB-gain and merge-weighting interact nontrivially. Promotes (α).
- Story B: MB-gain alone reorganizes the cascade GI distribution differently in different spatial bins because the convergence rate of the geometric series depends on local albedo and bounce geometry.

Discriminating A from B requires the isotropic-merge A/B flag (the (α) discriminator). Until then, the visual-reorganization evidence is suggestive, not conclusive.

### C7. The B2 deferral is unsatisfying

§2.1 documents why B2 is hard. §8.2 (below) sketches the path. But until B2 lands, the (β) verdict is purely a Δ-area metric, which conflates "cascade approaches PT" with "cascade overshoots PT into the opposite Δ band". A true magnitude-toward-PT metric is what lets you say "gain=1.0 leaves a 25% energy gap and gain=2.0 closes that gap". With B1 alone, we can only say "gain=2.0 produces a wholly different but equally large Δ band".

### C8. bug-234 fix scope was minimal — possibly too narrow

The 4-line fix forces `cascadeReady=false` when `measurementCamera>=0 && useMultiBounce`. It does NOT consider:
- Hybrid sweep mode + MB ON: should hybrid also force per-frame rebakes? (Not tested; out of scope for (β).)
- Other measurement-mode flags besides `--measurement-camera` (e.g., `--measurement-cameras-file` alone with no `--measurement-camera`): probably benign because the latter doesn't set `measurementCamera`.
- The per-frame rebake cost on larger scenes (sponza): could push capture time from 5s to 30s per capture. Acceptable for cornell; might need rate-limiting for sponza.

## 8. Open / deferred (next session candidates)

### 8.1. (α) merge-mode A/B — RECOMMENDED NEXT

The pattern-reorganization finding at g=1.5 plus the (β) demotion both point at (α) as the leading candidate. Direct test needs ~2-3h engine work:

- Add `--cascade-isotropic-merge=0|1` CLI flag (analogue of `--cascade-scaled-dir-res=`)
- Add `uUseIsotropicMerge` uniform to [radiance_3d.comp](../../res/shaders/radiance_3d.comp) — when true, merge weights are uniform across directional bins instead of cosine-weighted (or whatever the current per-direction weighting is — read the shader first)
- Sweep at cam{0,2} × mode{18,19} × merge{cosine, isotropic} × hybrid{0} × gain{1.0} = 8 captures (~2 min)
- Pre-committed decision rule (write BEFORE running per §6.1 of cascade_config_sweep_impl.md): if isotropic-merge reduces mode-19 Δ-area on BOTH cams by ≥20% relative to cosine-merge → ALPHA_CONFIRMED → ship isotropic merge OR investigate the specific weighting bug; if ≤5% → ALPHA_REJECT → pivot to (δ); else WEAK_ALPHA.

### 8.2. Finer-grained (β) sweep — OPTIONAL

If the user wants exact Δ-area minimum, run g ∈ {0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5} × cam{0,2} × mode{19} = 16 captures (~4 min). Will not change the global verdict; locates the optimum more precisely.

### 8.3. B2 metric (true magnitude toward PT) — INFRASTRUCTURE WORK

Two paths:
- **Cheap (LDR proxy)**: add render mode 22 = PT-GI-only (mirror mode 17). ~30 min shader work. Compute `cascadeLuma17 / ptLuma22` per pixel; document the post-tonemap bias caveat.
- **Honest (HDR)**: tinyexr add to engine for mode 16 and a hypothetical mode 22 EXR dump; analyzer reads EXR floats and computes the radiance ratio. ~3-4h.

### 8.4. (δ) spatial probe density discriminator

Add `--cascade-c0-res=N` CLI flag. Sweep N ∈ {16, 32, 64} (current default = 32 for cornell). 12 captures. Cost ~2 min run + ~30 min CLI wiring. Defer until (α) reports.

### 8.5. bug-230 fix

Still open from cascade-config era. Promoted-to-mandatory threshold from cascade_config_sweep §8.2 was "(β) sweep lands in WEAK band". The (β) sweep landed in MIXED-but-clearly-rejected — not the WEAK band. So bug-230 stays deferred until a future sweep actually needs variance bounding.

### 8.6. Re-test bug-234 fix interaction with hybrid+MB combo

The fix only triggers on `useMultiBounce`. If hybrid is also active, the per-frame rebake will happen but the hybrid PT-correction will still EMA-blend in its own one-sample-per-frame contribution. Variance in cascade+hybrid combined output may be higher than cascade-only. Worth a 4-capture sanity sweep before the next hybrid-related measurement run.

## 9. Files touched

| File | Change | Lines |
|---|---|---|
| [src/demo3d.cpp](../../src/demo3d.cpp) | bug-234 fix: force cascade rebake when measurement + MB both active | +4 + 9 comment |
| [tools/v20_pre_measurement/mb_gain_sweep.ps1](../../tools/v20_pre_measurement/mb_gain_sweep.ps1) | NEW — 20-capture driver | 84 |
| [tools/v20_pre_measurement/analyze_mb_gain.py](../../tools/v20_pre_measurement/analyze_mb_gain.py) | NEW — analyzer with B1 + B2-lite + verdict logic | 165 |
| [tools/v20_pre_measurement/mb_gain_results.json](../../tools/v20_pre_measurement/mb_gain_results.json) | NEW — raw 20-capture JSON | 240 |
| [.wolf/buglog.json](../../.wolf/buglog.json) | bug-234 entry (full root-cause + fix + verification) | +18 |
| doc/7/mbrc_v20_pre_measurement_report.md | §13 added (pending separate commit) | TBD |
| [.wolf/cerebrum.md](../../.wolf/cerebrum.md) | DNR addendum: "pre-committed rules must enumerate failure modes, not just success direction" + Key Learning: "GUI-vs-headless control-flow divergence is the most likely silent-fail surface" | +2 entries |
