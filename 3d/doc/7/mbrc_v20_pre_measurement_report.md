# MBRC v2.0-pre — Measurement Report (Scouting Pass)

**Status:** Scouting / preliminary. Single camera, single seed, 192-frame warmup,
14-capture sweep. **This is NOT the full v2.0-pre measurement report** specified by
[mbrc_v20_pre_measurement_plan.md REV 2](mbrc_v20_pre_measurement_plan.md) — the full
report still owes 3 cameras × variance harness (M2) × per-bin PT convergence (H2) ×
hybrid-on baseline (G4/H3) × RMSE / SSIM numbers. The scouting pass exists to
(a) verify the instrumentation works end-to-end after [bug-227](#bug-227-pt-dispatch-gate-fix)
was fixed and (b) form an early hypothesis about what the full report should focus on.

Captures live at [tools/v20_pre_measurement/captures/](../../tools/v20_pre_measurement/captures/).
Sweep harness: [diag_sweep.ps1](../../tools/v20_pre_measurement/diag_sweep.ps1).

---

## 1. Setup

- **Scene:** `cornell-orig-alcove` (Cornell Box with alcove emitter — used because
  it is the asymmetric-shadow scene from the [v1.3.1 variance sweep](hybrid_v12_validation_phase8_impl.md))
- **Pipeline:** baseline non-hybrid cascade (`--use-hybrid=0`). Hybrid-on baseline
  not yet captured this pass (plan G4/H3 owes that).
- **Camera:** cam0 from [tools/v20_pre_measurement/cameras.json](../../tools/v20_pre_measurement/cameras.json),
  pinned via `--measurement-camera=0` (probe jitter zeroed, input suppressed).
- **Warmup:** 192 frames at default temporal-accumulation rate. Auto-capture burst
  disabled via `--auto-capture-delay=0` (otherwise it hijacks `raymarchRenderMode`
  to 0/3/6 and contaminates the capture).
- **PT reference:** ~192 frames of dual-dispatch PT (full + direct-only). At ~1 ray
  per pixel per frame ≈ 192 spp — **noisy at the per-pixel level**, adequate for
  gross asymmetry reads, not adequate for sign-of-difference at small Δ.
- **Heatmap divisor:** `uDeltaHeatmapDivisor = 0.2` (cornell-scale default). Deeply
  saturated blue/red therefore means |Δluminance| ≥ 0.2 — about half the typical
  Cornell direct-lit luminance.
- **Excluded:** multi-seed averaging, multi-camera averaging, cascade-config sweep,
  variance harness, EXR dumps. All deferred to the full report.

---

## <a name="bug-227-pt-dispatch-gate-fix"></a>2. Diagnostic-toolchain bug found and fixed (bug-227)

Before any RC-quality reading was possible, this scouting pass surfaced a real bug in
the diagnostic instrumentation itself:

> The PT-reference dispatch gate at [demo3d.cpp:1244](../../src/demo3d.cpp#L1244)
> included only modes 16 / 18 / 19. **Mode 20 (Error-Decomposition Heatmap) reads
> both `uPtAccum` and `uPtDirectAccum` but never triggered the dispatch.** Result:
> `uPtAccumValid = 0` for every mode-20 capture, the shader's fallback set
> `ptTruth = vec3(0)`, every signed delta became `cascade - 0 > 0`, and all four
> sub-modes rendered **all-red regardless of cascade behavior**.

The pre-fix mode-20 captures spent ~1 session being mis-interpreted as "cascade is
uniformly brighter than PT everywhere," which contradicted mode 18 / mode 19 on the
same camera. That contradiction is what surfaced the bug.

**Fix:** extend the gate to include mode 20 ([demo3d.cpp:1244](../../src/demo3d.cpp#L1244),
shipped in commit 82e969e). After the fix:

- `md5(mode20_total) == md5(mode18_combined)` ✓ (sub-mode 0 ≡ mode 18 by formula).
- Sub-modes 1, 2, 3 produce distinct, sensible distributions.

**Cerebrum entry:** "PT-reference dispatch gates must list every render mode that
READS `uPtAccum / uPtDirectAccum`, not just modes that DISPLAY PT." See
[.wolf/cerebrum.md `2026-05-21` entry](../../.wolf/cerebrum.md). **Lesson for the
full report:** every diagnostic uniform must be greppable from the dispatch gate as
part of the same commit as the mode that consumes it.

---

## 3. What the 14 captures show

### 3.1 Composite + PT reference (sanity)

| Capture | What it shows |
|---|---|
| [cam0_mode00_composite.png](../../tools/v20_pre_measurement/captures/cam0_mode00_composite.png) | Cascade final composite. Red wall (LEFT) visible, green wall (RIGHT) visible, central back wall with alcove slits. Indirect-lit floor is roughly uniform across L/R. |
| [cam0_mode16_pt_ref.png](../../tools/v20_pre_measurement/captures/cam0_mode16_pt_ref.png) | PT reference. **Strongly asymmetric** — left half saturated red bleed from red wall onto back wall and floor; right half almost black, only the green wall itself visible with faint floor bleed. Deep contrast vs cascade composite. |

**Read:** the cascade has lost the dynamic range of the indirect lighting. Cornell-PT
has a ~10× brightness ratio between the red-bleed half and the green-bleed half; the
cascade composite has a ~1.5× ratio. The cascade does not look "wrong" until placed
next to PT — this is exactly the failure mode the [Mode 18 cerebrum entry](../../.wolf/cerebrum.md)
predicted ("white-looking averages can mask wrong composition").

### 3.2 Δ heatmaps (mode 18, mode 19, mode 20)

All three corroborate the same signature:

| Capture | LEFT half (red-wall side) | RIGHT half (green-wall side) | Reading |
|---|---|---|---|
| [cam0_mode18_combined.png](../../tools/v20_pre_measurement/captures/cam0_mode18_combined.png) | DEEP blue (`cascade − PT ≤ −0.2 lum`) | LIGHT pink (`cascade − PT ≈ +0.05 lum`) | Cascade total significantly dimmer L, slightly brighter R. |
| [cam0_mode19_gi_delta.png](../../tools/v20_pre_measurement/captures/cam0_mode19_gi_delta.png) | DEEP blue | LIGHT pink | **GI-only delta agrees with total** — the error IS in GI, not direct (mode 19 isolates GI by subtracting direct on both sides). |
| [cam0_mode20_total.png](../../tools/v20_pre_measurement/captures/cam0_mode20_total.png) | DEEP blue | LIGHT pink | Bit-identical to mode 18 (`md5 0386ee9a…`) ✓ — confirms post-bug-227 gate works. |
| [cam0_mode20_direct.png](../../tools/v20_pre_measurement/captures/cam0_mode20_direct.png) | Bright red (`cascade_direct > PT_direct`) | Light pink | Cascade's analytical direct over-illuminates vs PT's importance-sampled direct. Expected — different shadow approximations. |
| [cam0_mode20_indirect.png](../../tools/v20_pre_measurement/captures/cam0_mode20_indirect.png) | DEEP blue | LIGHT pink | Mirrors mode 19 (same formula). Confirms the GI under-integration is the dominant signed error. |
| [cam0_mode20_relative.png](../../tools/v20_pre_measurement/captures/cam0_mode20_relative.png) | Saturated red across the scene | (same) | Unipolar `|Δ|/PT` shows relative error > 100% almost everywhere — caveat: PT denominator at low spp is noisy, so the 1e-3 epsilon dominates dim pixels. Not diagnostic at this sample budget. |

**Headline finding (preliminary):** the cascade's GI is **asymmetrically dim on the
red-wall side**, magnitude on the order of half the local PT GI luminance. The
asymmetry is the diagnostic — a uniform dim would point to an exposure / gain bug;
an asymmetric dim points to a **directional / merge / albedo** issue. The pattern
correlates with the side of the scene that has the most color-saturated indirect
bounce (red wall reflects red light more saturatedly than the green wall reflects
green at this camera).

### 3.3 Leave-one-out attribution (mode 18 c0..c3)

| Capture | Pattern vs baseline mode 18 |
|---|---|
| [cam0_mode18_loo_c0.png](../../tools/v20_pre_measurement/captures/cam0_mode18_loo_c0.png) | Near-identical (LEFT blue / RIGHT pink) |
| [cam0_mode18_loo_c1.png](../../tools/v20_pre_measurement/captures/cam0_mode18_loo_c1.png) | Near-identical |
| [cam0_mode18_loo_c2.png](../../tools/v20_pre_measurement/captures/cam0_mode18_loo_c2.png) | Near-identical |
| [cam0_mode18_loo_c3.png](../../tools/v20_pre_measurement/captures/cam0_mode18_loo_c3.png) | Near-identical |

**Headline finding:** removing any single cascade (C0 through C3) leaves the
asymmetric L-dim/R-bright signature essentially unchanged. **No single cascade
dominates the error.** Per-cascade tuning (e.g. "bump C2 angular resolution"
or "tighten C0 leak gate") is unlikely to move this needle by itself.

**Caveats:** LOO at single camera + single seed cannot distinguish "no contribution"
from "small contribution drowned in PT noise." The full report's per-bin convergence
check (plan §2.3 / H2) needs to bound PT noise per pixel before LOO is fully
trustworthy.

### 3.4 Cascade dominance (mode 21)

[cam0_mode21_dominance.png](../../tools/v20_pre_measurement/captures/cam0_mode21_dominance.png)
bins each pixel by which cascade's spatial bracket contains the camera-hit
distance. The image is uniform-ish — most of the visible scene falls in a single
cascade's bracket at this camera distance, which **limits the diagnostic value of
mode 21 at this camera alone**. The plan's three-camera methodology
([§2.6](mbrc_v20_pre_measurement_plan.md#26-camera-positions-three)) is the right
setup to exercise mode 21 — at least one camera should be placed close enough to
the back wall that C0/C1 dominance differs across the frame.

---

## 4. Preliminary hypothesis (subject to full-report falsification)

The combination of:

1. Asymmetric L-dim / R-bright in mode 18 / 19 / 20-sub2 (agreement across three
   independent delta computations is informative).
2. Cascade-uniform LOO (no single level dominates).
3. PT direct ≠ cascade direct (mode 20 sub-1) — expected, **not** the source of
   the GI gap because mode 19 already factors direct out.

…points away from per-cascade leak gating or angular-resolution-of-a-single-level
issues, and toward an **architectural** mechanism that touches every cascade
uniformly. Two candidates within v2.0 scope:

- **(α) Merge-time directional weighting biases against saturated colored bounces.**
  When a cascade merges upper-cascade directional bins, the `wcos × atlas.α` weight
  ([raymarch.frag sampleProbeDir](../../res/shaders/raymarch.frag)) may
  systematically under-weight the directions carrying the brightest red bleed,
  because those directions also correspond to surfaces likeliest to be flagged as
  partially-occluded. The asymmetry would arise because the red bleed has a
  preferred direction (off the red wall, downward / toward floor / forward) that
  intersects the alcove geometry, while the green-side bleed direction lies in
  open space.
- **(β) Multi-bounce gain is in equilibrium but at the wrong fixed point.** The
  v1.3 hybrid sweep ([§10.4](hybrid_v12_validation_phase8_impl.md)) noted that the
  default `multiBounceGain` lands at a fixed point dimmer than PT on cornell-orig.
  The asymmetric L-dim/R-bright pattern is what a single-gain global multiplier
  applied to an already-asymmetric one-bounce result would look like.

Either is plausible; the captures here cannot distinguish them. The full report
needs: (i) hybrid-on baseline so we can subtract out the hybrid's compensation,
(ii) per-bin convergence check on the alcove pixels specifically, (iii) a
second/third camera that views the red wall more directly (cam1/cam2 from the
[cameras.json](../../tools/v20_pre_measurement/cameras.json)).

---

## 5. What this scouting report CANNOT support

For completeness and to keep future readers from over-extrapolating:

- **No RMSE / SSIM / numbers.** All findings are qualitative reads of bipolar
  heatmaps. The plan's quantitative pass (Q3 / Q4 / Q5) still owes.
- **Single camera.** L/R asymmetry is camera-specific; the same scene at a
  different camera could show a different polarity. The plan's three-camera
  methodology is required before "asymmetric dim" is a scene-level claim.
- **Single seed.** 192 PT samples on a single PCG seed has visible PT noise that
  shows up as pixel-level mottle in the captures. Some of the per-pixel sign
  observations are not stable across seeds. Mode 19's gross L-dim/R-bright
  pattern is large enough to survive seed noise; mode 20-sub-3 (relative error)
  is dominated by it.
- **No cascade-config sweep.** The plan's "vary D, vary C0 probe count, vary
  cascade count" axis is what disambiguates hypothesis (α) from (β). Not done.
- **No hybrid-on baseline.** Plan §2.7 / G4 / H3. Need it to know how much of
  the L-dim signal hybrid already compensates for, vs how much survives.

---

## 6. Recommended next action

The scouting pass justifies promoting one item to the full report's critical path:

**Run the full v2.0-pre methodology with current instrumentation BEFORE any
RC-side fix.** Specifically:

1. **3 cameras × 3 seeds × 512+ PT samples** to bound per-pixel PT noise and
   confirm the L-dim/R-bright asymmetry is scene-level, not camera-artifact.
2. **Hybrid-on baseline at the same cameras / seeds / samples** ([plan §2.7](mbrc_v20_pre_measurement_plan.md#27-hybrid-on-baseline-rev-2--g4--h3-flagged))
   so the report can quantify "how much of the gap does hybrid already close."
3. **Cascade-config sweep** ([plan §2.3-§2.4](mbrc_v20_pre_measurement_plan.md#24-cascade-rc-own-variance-noise-floor-rev-2--m2--m5))
   — vary D ∈ {4, 8, 12} and C0 probe count ∈ {32³, 48³, 64³}. If neither lever
   moves the L-dim asymmetry, hypothesis (α) and (β) are both probably wrong and
   the v2.0 plan needs a third candidate.
4. **EXR dump + offline RMSE / SSIM** (plan §2.5 / L1) so future regressions
   have numeric anchors, not just heatmap eyeballs.

This is approximately 2× the current sweep effort — within the [plan REV 2
effort estimate](mbrc_v20_pre_measurement_plan.md#72-revised-effort-rev-2--post-critic-06)
of ~16h. The scouting pass took ~4h including the bug-227 detour, leaving room.

**Do NOT** start tuning RC levers yet. The [v1.3.1 NEE/cone tie](hybrid_v12_validation_phase8_impl.md)
established the precedent that single-camera visual A/B is insufficient evidence to
ship a fix; the same standard applies to v2.0a-c selection.

---

## 7. Changelog

- 2026-05-21 — Scouting report drafted from 14-capture single-camera sweep.
  Documented bug-227 (PT dispatch gate missing mode 20) and the L-dim/R-bright
  cascade-uniform finding. Recommended full-report execution before any RC fix.
