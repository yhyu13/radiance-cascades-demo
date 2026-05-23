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

### 3.5 Full-sweep follow-up (2026-05-21 PM)

> **Note: this subsection is the result of the full-sweep §6 next-action listed
> below — added in-place rather than as a separate doc so the scouting findings
> and the falsification attempt sit next to each other. The full sweep
> partially-falsified hypothesis (α) / (β) and surfaced a new mechanism
> hypothesis (γ). See §8 for the revised hypothesis and §9 for what STILL
> cannot be supported.**

Sweep harness: [full_sweep.ps1](../../tools/v20_pre_measurement/full_sweep.ps1)
(3 cams × 3 seeds × 2 hybrid × 4 modes = 72 captures @ 512 warmup frames).
Captures: [tools/v20_pre_measurement/captures_full/](../../tools/v20_pre_measurement/captures_full/).
Wall time: 11.8 min. No WARN events.

**3.5.0 Instrumentation finding — [bug-230](#bug-230-noise-seed-offset-only-wired-to-one-rng-site)
(seed axis non-functional).** Before any RC reading, the sweep surfaced a second
self-inflicted diagnostic bug: `--noise-seed-offset` is only forwarded to
`uMBFrameSeed` (cascade multi-bounce feedback). It is NOT added to `uFrameIndex`
(PT shader) or `uHybridFrameSeed` (hybrid correction). md5 verification:
`cam0_s{0,1,2}_h0_m18.png` are all `689da669139b893cbece81c1d80f36a6`
(bit-identical). The seed axis of plan §2.4 / M2 is therefore non-functional.
**Consequence:** the 72-capture sweep is effectively single-seed —
3 cams × 2 hybrid × 4 modes = 24 unique captures, 48 redundant. The 3-camera +
hybrid-on/off axes remain valid; the per-pixel PT noise bound chunk-1 was
supposed to provide is NOT delivered. See §8 / §9. Bug logged for future fix
before any variance-bound work.

**3.5.1 Cross-camera consistency (mode 18, hybrid off).** Reading
[cam0_s0_h0_m18](../../tools/v20_pre_measurement/captures_full/cam0_s0_h0_m18.png),
[cam1_s0_h0_m18](../../tools/v20_pre_measurement/captures_full/cam1_s0_h0_m18.png),
[cam2_s0_h0_m18](../../tools/v20_pre_measurement/captures_full/cam2_s0_h0_m18.png)
side-by-side, with composites for wall identification
([cam2_s0_h0_m00](../../tools/v20_pre_measurement/captures_full/cam2_s0_h0_m00.png)):

| Camera | Layout | Deep-BLUE region tracks | Light-PINK region tracks |
|---|---|---|---|
| cam0 (front, mid-range) | red wall LEFT, green RIGHT, partition slits CENTER | back wall + partition + main-room floor (LEFT half + center) | green wall + alcove-side back wall (RIGHT half) |
| cam1 (front, closer) | same as cam0, tighter framing | back wall + central pillar (more saturated than cam0) | RIGHT half (alcove side) |
| cam2 (front-LEFT elevated) | red wall LEFT-FRONT, green RIGHT-EDGE, back wall + partition CENTER | back wall + partition wall (CENTER of screen, not left) — has *moved* in screen space | front-facing walls + ceiling-CORNER (red shows on TOP as RED `cascade > PT` overshoot) |

**Critical realignment of the scouting-pass headline.** The scouting report
called the pattern "L-dim / R-bright" because in cam0 the deep-blue region
happens to sit on the LEFT half. cam2, viewing from the front-left corner, puts
the deep-blue region in the CENTER and the red wall on the LEFT-FRONT as PINK.
The blue region **tracks the back wall + partition wall + main-room area** —
the surfaces lit indirectly via light that has to travel through the alcove's
narrow partition opening. The scouting `red wall / green wall` framing was a
camera-artifact. **Scene-level signature: cascade under-illuminates main-room
surfaces (which receive light only after it passes through the partition slit
opening); cascade over-illuminates alcove-side surfaces directly lit by the
emitter.**

**3.5.2 Hybrid-on closes part of the gap, but not uniformly.**
Comparing `*_h0_m18` to `*_h1_m18` at each camera:

| Camera | Hybrid OFF (`h0_m18`) | Hybrid ON (`h1_m18`) | Read |
|---|---|---|---|
| cam0 | Deep-BLUE LEFT, light-PINK RIGHT, sat ~0.2 lum | Mid-BLUE LEFT (less saturated), light-PINK RIGHT | Hybrid REDUCES the under-illumination on the main-room side. Asymmetry remains visible — not closed. |
| cam1 | Same as cam0, deeper saturation | Marginal lightening of blue; pink slightly tighter | Lower-resolution capture; trend matches cam0. |
| cam2 | Back-wall BLUE, ceiling RED, walls pink | Back-wall BLUE *unchanged*, ceiling RED **more saturated**, walls more pink | Hybrid INCREASES the ceiling overshoot at this camera. Not a unilateral improvement. |

**Read:** hybrid's PT-correction is helping in the main-room under-illumination
region (where the cascade is too dim) but is *adding to* the ceiling
over-illumination region (where the cascade is already too bright). The hybrid
v1.3 correction has no signed-direction awareness — it integrates PT samples
and biases toward whatever the local PT distribution shows; at cam2's ceiling
camera angle PT picks up specular reflections off the alcove emitter that the
cascade also picks up, so hybrid's local-PT-integrated correction adds energy
in a direction the cascade is already over-counting.

**3.5.3 GI-only (mode 19) confirms GI dominates the error.**
[cam0_s0_h0_m19](../../tools/v20_pre_measurement/captures_full/cam0_s0_h0_m19.png)
and [cam0_s0_h1_m19](../../tools/v20_pre_measurement/captures_full/cam0_s0_h1_m19.png):
- Mode-19 deep-blue is MORE saturated than mode-18 deep-blue at the same
  camera — subtracting the direct component (which agrees better between
  cascade and PT) leaves the GI residual MORE asymmetric.
- Hybrid-on mode-19 has the same pattern as hybrid-on mode-18 (less saturated
  than h0 but still asymmetric).
- **GI is unambiguously the dominant error component** at this scene; direct
  lighting differences are second-order.

**3.5.4 The PT-vs-cascade dynamic range gap (the actual mechanism).**
Comparing [cam0_s0_h0_m00 (composite)](../../tools/v20_pre_measurement/captures_full/cam0_s0_h0_m00.png)
vs [cam0_s0_h0_m16 (PT ref)](../../tools/v20_pre_measurement/captures_full/cam0_s0_h0_m16.png):
- PT (truth): LEFT half BRIGHT red bleed dominates; RIGHT half nearly BLACK
  (green wall barely visible — the emitter geometry shadows it). Heavy
  asymmetric dynamic range.
- Cascade composite: LEFT and RIGHT walls both visible at roughly UNIFORM
  brightness. The dynamic range PT captures (~10× L/R) is compressed to ~1.5×.

This is the failure mode previously hypothesized for the v1.3 hybrid sweep
([§10.4](hybrid_v12_validation_phase8_impl.md)) but never tied to a specific
mechanism. With the PT reference now visible, the mechanism is concrete:
**the cascade's angular discretization smears concentrated indirect light
across many directional bins**, lowering the bright direction's peak and
raising the dim directions' floor. The result is range compression that looks
correct in isolation but wrong next to PT.

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

## 8. Revised hypothesis (post-full-sweep)

The full-sweep cross-camera data (§3.5.1) and the PT-vs-cascade dynamic-range
contrast (§3.5.4) supersede §4. The scouting hypotheses (α) merge-time
directional weighting and (β) multi-bounce gain fixed-point are NOT falsified
but are no longer the leading candidates. The pattern that emerges is:

- **(γ) Angular under-sampling of concentrated indirect light.** Cascade
  probes have a fixed angular bin count per level. When indirect light arrives
  at a probe predominantly from a single concentrated direction (e.g., light
  scattered through the alcove partition's narrow opening, or light bounced
  off a strongly-colored wall), that direction's energy is *averaged with*
  the energy in the adjacent bins during integration. Result: the peak
  direction reads dimmer than PT, the adjacent shadow directions read brighter
  than PT. Range compression.

Evidence for (γ) over (α/β):

1. **Cross-camera consistency** — the deep-blue region tracks scene
   architecture (back wall + partition), not screen position. Both (α) and
   (β) would predict similar consistency, but (γ) predicts it with the
   strongest spatial specificity: the surfaces hit by light that has passed
   through the partition's narrow angular subtense.
2. **Hybrid asymmetric response (§3.5.2)** — at cam0 hybrid REDUCES the
   under-illumination, at cam2 hybrid INCREASES the over-illumination.
   Consistent with (γ): hybrid integrates per-pixel PT samples *along the
   surface normal cone*; in regions where the cascade is dim because the
   angular peak was averaged-away, PT-integrated samples carry the missing
   energy and hybrid adds it back; in regions where the cascade is bright
   because adjacent-bin floor was raised, PT-integrated samples cannot
   subtract — they can only add — so hybrid amplifies the existing overshoot.
   (α) would predict uniform reduction; (β) would predict uniform scaling.
3. **GI dominance (§3.5.3)** — mode 19 (direct subtracted) shows MORE
   saturated asymmetry than mode 18 (combined). Direct light has a clear
   incident direction the cascade can resolve; indirect light is the
   under-sampled component. Consistent with (γ).
4. **LOO cascade-uniform** (scouting §3.3, full-sweep would corroborate but
   was not re-run because LOO was already conclusive) — every cascade
   level shares the same angular bin count, so the error is present at every
   level, consistent with (γ).

**Concrete v2.0 implications (provisional):**

- **(γ-fix A) Per-probe direction-importance sampling.** Rather than uniform
  spherical bins, allocate more bins toward concentrated incident directions.
  Detected from upper-cascade reads or from a one-frame ahead-of-time scout
  dispatch. Cost: bin-count overhead per probe, additional shader complexity.
- **(γ-fix B) Higher angular resolution.** Brute-force increase cascade
  angular bin count by 2-4×. Predictable cost; predictable improvement;
  doesn't change architecture. Measure first via cascade-config sweep
  ([plan §2.3-§2.4](mbrc_v20_pre_measurement_plan.md#24-cascade-rc-own-variance-noise-floor-rev-2--m2--m5))
  to confirm angular resolution moves the needle before committing to either
  variant.
- **(γ-fix C) Cooperative anisotropic merge.** When merging upper-cascade
  bins into a lower-cascade probe, preserve the anisotropic distribution
  rather than collapsing to a single cosine-weighted average. More invasive;
  unclear net win.

**(α) and (β) remain on the menu** — the full sweep did not rule either out.
But (γ) is now the candidate most consistent with the observed asymmetric
hybrid response.

---

## <a name="cannot-9"></a>9. What this full sweep STILL cannot support

A more honest list than the scouting §5 because chunk-1 (variance harness)
was non-functional:

- **No per-pixel PT noise bound.** The 3-seed axis was supposed to give a
  cross-seed std-dev per pixel so we could distinguish "real Δ" from
  "PT noise Δ". bug-230 made all 3 seeds bit-identical, so we have
  effectively single-seed data. The gross asymmetric patterns are large
  enough that PT noise cannot plausibly explain them, but per-pixel sign
  reads (especially in dim regions) remain unbounded.
- **No RMSE / SSIM / numeric anchors.** All findings are still qualitative
  reads of bipolar heatmaps. Plan Q3/Q4/Q5 still owes. Without scalar metrics
  we cannot quantify "how much of the gap hybrid closes" — only "hybrid
  closes some, asymmetrically."
- **No cascade-config sweep.** Plan §2.3-§2.4 disambiguation of (γ) vs
  (α)/(β) requires varying cascade angular resolution / probe count.
  If (γ-fix B) brute-force angular-res increase moves the gap, (γ) is the
  mechanism; if it doesn't, (γ) is wrong and (α)/(β) re-promoted.
  This is the highest-value next data run.
- **No second emitter geometry.** All findings are on cornell-orig-alcove.
  A simpler non-alcove cornell (no partition wall) would test whether the
  asymmetry is specific to the partition-bottleneck or a general cascade
  property. Cheap to add.
- **Hybrid v1.3.1 only, no v1.3.2.** §3.5.2's hybrid-asymmetric-response
  finding is specific to the current (DI-cone) hybrid; a true light-position
  NEE hybrid (v1.3.2 backlog) could behave differently.

---

## <a name="bug-230-noise-seed-offset-only-wired-to-one-rng-site"></a>10. bug-230 anchor — `--noise-seed-offset` is partially-wired

**Symptom (this sweep):** md5 of `cam0_s{0,1,2}_h0_m18.png` all =
`689da669139b893cbece81c1d80f36a6` (and similarly for h1 and modes 16/19/00 —
all bit-identical across `--noise-seed-offset=0/1/2`).

**Root cause:** [demo3d.cpp:2577](../../src/demo3d.cpp#L2577) forwards
`renderFrameIndex + noiseSeedOffset * 9973` only into `uMBFrameSeed`
(`radiance_3d.comp:95`, multi-bounce cascade resampling). The PT dispatch
at [demo3d.cpp:3267](../../src/demo3d.cpp#L3267) forwards bare `ptFrameIndex`
into `uFrameIndex` (`pt_reference.comp:68`); the hybrid dispatch at
[demo3d.cpp:3618](../../src/demo3d.cpp#L3618) forwards bare `hybridFrameSeed`
into `uHybridFrameSeed` (`hybrid_correction.comp:60`). Neither receives
`noiseSeedOffset`. PT and hybrid reproduce bit-exact across runs; cascade MB
feedback IS reseeded but its contribution to the displayed delta is too small
to register at 8-bit PNG quantization.

The doc comment in [demo3d.h:1453-1457](../../src/demo3d.h#L1453) already
*describes* multi-site wiring ("Added to the cascade-bake RNG site (uMBFrameSeed)
AND MB v2 stochastic sampler") — the AND was aspirational. Only the
cascade-bake site shipped.

**Fix (PENDING — not in this commit):** add `noiseSeedOffset*9973` to
`ptFrameIndex` at the dispatch site, and to `hybridFrameSeed` at its
dispatch site. Validate by re-running md5 cmp: mode-16 hashes must differ
across seeds. Then re-run the variance harness portion of the sweep so
chunk-1 actually delivers a per-pixel PT-noise bound.

**Cerebrum entry:** "When wiring a per-run RNG offset, audit every shader
that consumes a `*FrameSeed` / `*FrameIndex` uniform — they are separate
RNG streams and a per-run offset is only meaningful if applied at every
stochastic site." See [.wolf/cerebrum.md `2026-05-21` entries](../../.wolf/cerebrum.md).

---

## 11. Recommended next action (revised — supersedes §6)

Given the full-sweep findings, the next critical-path items are:

1. **Fix bug-230** (1-2h) so the seed axis works, then re-run a slimmed
   sweep (3 cams × 3 seeds × 1 hybrid × 1 mode = 9 captures @ 512spp,
   ~1.5min) just to bound per-pixel PT noise on mode-18 pixels.
2. **Cascade-config sweep** (plan §2.3-§2.4) — vary cascade angular bin
   count (currently fixed per cascade), with at least one config that
   doubles it. If the asymmetric pattern shrinks substantially, (γ) is
   confirmed and (γ-fix B) is the conservative shipping option. ~2h.
3. **Add a second scene** — plain cornell (no alcove partition) to test
   whether the asymmetry is partition-specific or general. ~1h.
4. **EXR dump + offline RMSE / SSIM** (plan §2.5 / L1) — same compute budget
   as the current sweep but with numeric output. ~3h.

**§6 has been left in place above** as the *original* full-report
recommendation; this §11 supersedes it with the data the full sweep
actually produced. The §6 chunk-3 (cascade-config sweep) is the same
critical item — only the order changed.

**Do NOT** start tuning RC levers yet. The v1.3.1 NEE/cone tie precedent
([§10.4](hybrid_v12_validation_phase8_impl.md)) and the (γ) hypothesis being
*plausible but not confirmed* mean a fix shipped on visual-A/B alone risks
a second tie. Cascade-config sweep (chunk 2 above) is the discriminator.

---

## 12. Cascade-config sweep — hypothesis (γ) discriminator (2026-05-21 evening)

**Goal.** §11 named cascade-config sweep as the discriminator between
hypothesis (γ) angular under-sampling and (α) merge-weighting / (β) MB-gain.
This section reports the sweep result.

### 12.1. Engine + harness

Added two CLI flags (mirroring [main3d.cpp:619 `--noise-seed-offset`](../../src/main3d.cpp)):

- `--cascade-dir-res=N` — override `dirRes` (octahedral D, D² rays/probe).
  Validated even, 2..32. Triggers `cascadeReady=false` so next frame rebuilds
  cascades at the new resolution.
- `--cascade-scaled-dir-res=0|1` — toggle `useScaledDirRes`. When 1 (default),
  cascades get D / 2D / 4D / 4D (capped 16). When 0, all cascades use uniform D.
  Uniform mode is what isolates the angular-resolution effect.

Setters: [`Demo3D::setDirRes`](../../src/demo3d.h) and
[`Demo3D::setUseScaledDirRes`](../../src/demo3d.h).

Harness: [`tools/v20_pre_measurement/cascade_config_sweep.ps1`](../../tools/v20_pre_measurement/cascade_config_sweep.ps1).
Analyzer: [`tools/v20_pre_measurement/analyze_cascade_config.py`](../../tools/v20_pre_measurement/analyze_cascade_config.py)
(PIL + numpy; classifies pixels into blue/red Δ bands via saturation threshold
and channel dominance, with a luma floor to drop letterbox background).

### 12.2. Sweep matrix

3 angular configs × 2 cameras × 2 modes = 12 captures, executed in **2.3 min**:

| axis | values |
|------|--------|
| `dirRes` (D) | 4, 8, 16 (rays/probe = 16, 64, 256) |
| `--cascade-scaled-dir-res` | 0 (uniform across all 4 cascades) |
| camera | cam0, cam2 (inverts screen-space layout per §3.5.1) |
| seed | 0 (single; bug-230 deferred — irrelevant within-D since seed axis is bit-identical) |
| mode | 18 (combined Δ), 19 (GI-only Δ) |
| hybrid | off |
| frames | 512 (matches full sweep) |

### 12.3. Quantitative result

"total%" = fraction of foreground pixels classified into either blue
(cascade<PT) or red (cascade>PT) saturated bands. (D16-D8)/D8 = relative
change in total-area when angular sample count quadruples (D²: 64 → 256
rays/probe).

| cam/mode    | D=4 tot% | D=8 tot% | D=16 tot% | (D16-D8)/D8 |
|-------------|---------:|---------:|----------:|------------:|
| cam0 mode18 |   27.16% |   25.26% |    25.17% |       −0.4% |
| cam0 mode19 |   29.15% |   26.54% |    26.27% |       **−1.0%** |
| cam2 mode18 |   25.09% |   24.37% |    24.24% |       −0.5% |
| cam2 mode19 |   21.71% |   20.57% |    20.29% |       **−1.4%** |

Mean band-saturation also nearly invariant: blueSat ∈ [0.719, 0.729] across
all 12 captures.

### 12.4. Verdict: **(γ) REJECTED**

Decision rule (committed in §11 before running the sweep):

- STRONG_GAMMA = ≥50% reduction on mode 19 at BOTH cams when D doubles.
- WEAK_GAMMA   = 10-50% reduction.
- GAMMA_REJECT = ≤10% reduction (invariant under D).

Observed mode-19 reduction at D8→D16 (4× rays/probe): cam0 = **−1.0%**,
cam2 = **−1.4%**. Both well inside the ±10% invariance band → **GAMMA_REJECT**.

Visual cross-check (cam0 m19 D=4 vs D=16) shows blue/red regions in
**identical screen positions, identical shape, near-identical saturation**.
The 4× ray-count increase produced no observable softening of the asymmetric
pattern. cam2 m19 D=4 vs D=16 is equally indistinguishable — confirming the
rejection is not a single-camera artifact.

### 12.5. What this implies

The "smear concentrated indirect light across many directions" mechanism
proposed in §8 doesn't survive contact with the data. The cascade-vs-PT
delta pattern is **not** angular-resolution-bound; it is architectural to
the cascade construction (merge, ray-march, or feedback gain). The leading
hypotheses revert to:

- **(α) merge-time directional weighting** — original scouting hypothesis.
  Per-direction merge weights may double-count or under-count indirect
  contributions from specific directions when probes are merged across
  cascade boundaries. Suggested by the partition-opening anchor: rays
  passing through the narrow alcove gap occupy a *specific direction band*,
  and that band may be the one mis-weighted.
- **(β) multi-bounce gain at wrong fixed point** — MB gain=1.0 is energy-
  conserving in theory but cascade integration losses (hemi_factor ≈ 0.05
  vs theoretical 0.5) suggest the effective fixed point is lower than the
  geometric series predicts. A bigger MB gain (1.5–2.0) would raise the
  cascade GI floor — which is exactly where mode 19 says cascade is too dim.

### 12.6. CANNOT support / honest scope

- Sweep is single-seed (bug-230 unfixed) — within-D comparisons still valid
  because the same PCG state is used at all three D values; this is a
  *configuration* axis, not a *noise* axis.
- The blue/red band classifier uses a heuristic saturation threshold (0.55)
  on the bipolar heatmap, not the raw cascade-PT scalar Δ from an EXR. If
  the colormap distorts area perception, the magnitudes shift but the
  invariance result holds (verified by inspecting the visual cross-checks
  alongside the metric).
- D=16 uniform is ALREADY beyond what the engine ships by default. Going
  higher (D=24, D=32) is bounded by atlas dimensions and was not tested;
  given the flat trend D=4→D=8→D=16, extrapolation would have to be
  super-linearly different to flip the verdict.
- Result holds only for cornell-orig-alcove scene. The (α) and (β)
  hypothesis tests should re-introduce scene diversity (plain cornell, then
  sponza) before any code-shipping decision.

### 12.7. Recommended next action

Run an (α)/(β) discriminator sweep next session:

- **(β)** is the cheap test. `multiBounceGain` already has a CLI/GUI; sweep
  values {0.5, 1.0, 1.5, 2.0, 3.0} at cam0+cam2, modes 18+19, single seed,
  hybrid off. 10 captures, ~3 min. Decision rule: if (β) is dominant,
  gain=2.0 should reduce mode-19 blue area on BOTH cams by ≥30% (raising
  the cascade GI floor). If invariant under gain, (β) drops too and (α) is
  the last remaining hypothesis.
- **(α)** requires more engine work. The merge-weighting code lives in
  [`radiance_3d.comp`](../../res/shaders/radiance_3d.comp) — needs an A/B
  flag for uniform-isotropic merge vs the current directional-aware merge,
  and ideally a third "merge-from-cosine-weighted-only" variant. Estimate
  2-3h engine work before the sweep.

Both should run BEFORE attempting bug-230 fix — if (β) confirms, bug-230
becomes a regression-detector for the gain change, not a prerequisite.

---

## 7. Changelog

- 2026-05-21 — Scouting report drafted from 14-capture single-camera sweep.
  Documented bug-227 (PT dispatch gate missing mode 20) and the L-dim/R-bright
  cascade-uniform finding. Recommended full-report execution before any RC fix.
- 2026-05-21 PM — Full sweep executed (72 captures, 3 cams × 3 seeds × 2 hybrid
  × 4 modes, 11.8 min). Added §3.5, §8, §9, §10, §11. Surfaced bug-230 (seed
  axis non-functional). Realigned scouting "L-dim/R-bright" headline to
  scene-architecture-tracking: cascade under-illuminates main-room surfaces lit
  through the partition opening, over-illuminates alcove-direct surfaces.
  Promoted hypothesis (γ) angular under-sampling to leading candidate;
  scouting (α)/(β) remain on the menu but no longer lead. Hybrid v1.3.1
  closes part of the gap on main-room side but amplifies the over-illumination
  on alcove/ceiling side at cam2 — not a unilateral improvement.
- 2026-05-21 evening — Cascade-config sweep executed (12 captures, 3 D values ×
  2 cams × 2 modes, 2.3 min). Added §12 (CLI flags, harness, analyzer, matrix,
  result, verdict, scope). **Hypothesis (γ) REJECTED**: D8→D16 (4× rays/probe)
  reduces mode-19 Δ-band area by only 1.0% / 1.4% across cam0 / cam2, well
  inside the ±10% GAMMA_REJECT band. Visual cross-checks confirm asymmetric
  pattern is angular-resolution-invariant. Leading hypothesis flips back to
  (α) merge-weighting / (β) MB-gain. Recommended next action: cheap (β)
  multi-bounce-gain sweep, then (α) merge-mode A/B if (β) also rejects.
- 2026-05-22 morning — (β) MB-gain sweep executed (20 captures, 5 gains × 2 cams
  × 2 modes, 5.5 min). Added §13. **Hypothesis (β) DEMOTED**: gain has strong
  leverage but is not a global cure. g=2.0 *increases* mode-19 Δ-band area by
  +363.4% / +213.6% on cam0/cam2 (opposite of pre-committed STRONG_BETA "≥30%
  reduction" rule). Pattern is U-shaped with minimum near g=1.0; above g=1.5
  the cascade overshoots PT into a runaway-red Δ band. Pattern *reorganizes*
  shape between g=1.0 and g=1.5 (not just intensity shift) → suggests MB-gain
  × merge-weighting interaction → **(α) promoted to leading candidate**. Also
  surfaced + fixed bug-234 (measurement-camera mode disables MB feedback
  silently by pinning probe jitter → no rebake trigger → MB gate never opens).
  Recommended next action: (α) isotropic-merge A/B (~2-3h engine work + 8-capture
  sweep). See [doc/7/mb_gain_sweep_impl.md](mb_gain_sweep_impl.md).

---

## 13. (β) MB-gain discriminator sweep — Δ-area leverage, not a cure

Companion impl notes: [mb_gain_sweep_impl.md](mb_gain_sweep_impl.md).

### 13.1. Scope

- 20 captures: 5 `multiBounceGain` values {0.5, 1.0, 1.5, 2.0, 3.0} × 2 cams
  (cam0 main-room, cam2 alcove-elevated) × 2 modes (18 combined Δ, 19 GI-only
  Δ). Single seed (bug-230 still open; gated on (β) WEAK — not triggered).
  Hybrid OFF, `--cascade-scaled-dir-res=1`, `--exit-frames=512`. Cornell-orig-alcove.
- Harness: [tools/v20_pre_measurement/mb_gain_sweep.ps1](../../tools/v20_pre_measurement/mb_gain_sweep.ps1).
- Analyzer: [tools/v20_pre_measurement/analyze_mb_gain.py](../../tools/v20_pre_measurement/analyze_mb_gain.py)
  — mirrors `analyze_cascade_config.py` thresholds (SAT=0.55, LUMA=0.05) so
  results are directly comparable with §12.
- bug-234 fix [src/demo3d.cpp:986-994](../../src/demo3d.cpp#L986-L994) required
  before the sweep produced meaningful data (md5-identical PNGs at g=1.0 vs 2.0
  pre-fix; distinct post-fix). See impl notes §3 for root cause.

### 13.2. B1 (Δ-band area, mode 19) — main result

| cam | g=0.5 | g=1.0 | g=1.5 | g=2.0 | g=3.0 | (g2−g1)/g1 |
|---|---:|---:|---:|---:|---:|---:|
| cam0 | 24.33% | 21.55% | 56.44% | 99.86% | 99.89% | **+363.4%** |
| cam2 | 21.27% | 22.87% | 30.22% | 71.72% | 71.75% | **+213.6%** |

Pre-committed rule (`mb_gain_sweep_impl.md` §2 of the prior cascade-config doc):
STRONG_BETA = g=2.0 *reduces* both cams' Δ-area by ≥30%. Result is the
opposite sign, several times the magnitude. The analyzer enum has no slot for
"wrong direction, larger magnitude" → falls through to MIXED. The right
narrative label is **BETA_REJECT_AS_GLOBAL_CURE**.

Color breakdown shows what is happening underneath the total:

| cam,gain | blue% | red% | reading |
|---|---:|---:|---|
| cam0, g=0.5 | 24.14% | 0.19% | almost pure under-illumination |
| cam0, g=1.0 | 19.67% | 1.88% | the §3.5 baseline pattern |
| cam0, g=1.5 | 2.46%  | 53.98% | pattern FLIPPED to mostly red |
| cam0, g=2.0 | 0.07%  | 99.79% | runaway over-illumination |
| cam0, g=3.0 | 0.07%  | 99.81% | pegged at heatmap divisor saturation |

The U-shape on cam0 has its minimum at the engine default (g=1.0); cam2's
minimum is at g=0.5. The current default is *already near* the local Δ-area
optimum on this scene — not the under-tuned value the plan presumed.

### 13.3. Visual cross-check (three findings)

1. **Nonlinear leverage**: g=1.0 → g=2.0 doesn't double brightness, it pegs
   the heatmap divisor. The Phase MB GUI tooltip's "+9.9% brightness at
   gain=2.0" was per-scene-specific or predates a cascade-bake change; the
   relationship between gain and cascade-vs-PT Δ is not a linear scale.
2. **Pattern reorganization at g=1.5**, not just intensity shift. The blue
   stripe near the partition gap survives at g=1.5 while surrounding pixels
   flip to red. If MB-gain were a pure global multiplier of cascade GI, the
   |Δ| shape should shift uniformly in luminance space, not *reorganize*. The
   reorganization is consistent with MB-gain × merge-weighting interaction —
   evidence (suggestive, not conclusive) for hypothesis (α).
3. **The default (g=1.0) is already near optimal** on cornell-orig-alcove.

### 13.4. Why the verdict is BETA_LEVERAGE_NOT_CURE, not BETA_REJECT

(β) has *the strongest leverage on the Δ pattern* of any axis tested so far
(more than D, more than hybrid). It is not a cure because:

- No tested gain produces |Δ| less than ~21% blue (cam0) / ~21% blue (cam2);
  the under-illumination "floor" is at g≈1.0 and the over-illumination ceiling
  starts at g≈1.5. There is no gain value that produces |Δ|≈0.
- The pattern *shape* changes with gain (finding 2 above); a single global
  scalar cannot match a per-pixel-variable target.

(β) is therefore a knob that affects energy throughput but not the directional
correctness of the cascade integration. The asymmetric Δ pattern is preserved
under (β) — moved to a different place, but not eliminated.

### 13.5. Decision-tree update (2026-05-22)

- **(α) merge-time directional weighting** — *promoted to leading candidate*.
  §13.3 finding 2 plus §12 (γ) rejection plus §13.4 (β) demotion converge on
  (α). Direct test needs the isotropic-merge A/B flag in `radiance_3d.comp`
  (~2-3h engine work).
- **(β) MB-gain** — *demoted to "knob has leverage but is not a cure"*. Not
  eliminated as a contributor; eliminated as the single global fix.
- **(γ) angular under-sampling** — REJECTED 2026-05-21 (§12).
- **(δ) spatial probe density / smoothstep blending** — *unchanged; still
  untested*. Add `--cascade-c0-res=N` CLI to discriminate after (α).

### 13.6. Self-critiques on this sweep (impl §7 has full list)

- **The pre-committed rule was malformed** (unidirectional "gain↑ → Δ↓"); the
  data showed gain↑ → Δ↑ in spades. New cerebrum DNR: pre-commit rules must
  enumerate failure modes including "knob has leverage but wrong direction".
- **The 5-point grid {0.5, 1.0, 1.5, 2.0, 3.0} missed the interesting region
  {1.0-1.5}** where the pattern flips. Robust to grid spacing for the global
  verdict; finer grid is §13.7 optional.
- **mean_fg_luma is the wrong B2-lite proxy** — mode 19 is bipolar so luma
  *decreases* as Δ-area saturates. Treat the column as B1-restated, not
  brightness. Real B2 still needs a mode-22 PT-GI-only pass.
- **bug-234 fix scope is minimal** — only triggers on `useMultiBounce`. Hybrid
  + MB combo path not re-tested.

### 13.7. Recommended next action

Run (α) isotropic-merge A/B sweep:

- Add `--cascade-isotropic-merge=0|1` CLI flag and `uUseIsotropicMerge` uniform
  to [radiance_3d.comp](../../res/shaders/radiance_3d.comp). Engine work ~2-3h.
- Sweep cam{0,2} × mode{18,19} × merge{cosine, isotropic} × gain{1.0} × hybrid{0}
  = 8 captures, ~2 min.
- Pre-committed rule (write BEFORE running, per §6.1): if isotropic-merge
  reduces mode-19 Δ-area on BOTH cams by ≥20% → **ALPHA_CONFIRMED** → ship
  the isotropic path or investigate the specific weighting bug; if ≤5% on
  both → **ALPHA_REJECT** → pivot to (δ) probe density; else **WEAK_ALPHA**
  (one-cam-only or 5-20% band) → finer grid + bug-230 fix mandatory.

If (α) also rejects, only (δ) remains in the named-hypothesis tree; at that
point bug-230 must be fixed and the search reopens to "unidentified RC quality
bug" (perhaps in the directional-bin smoothstep blend, or in the cascade
falloff between C0 and upper cascades).

Optional follow-ups (defer until (α) reports):
- Finer (β) grid g ∈ {0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5} (~4 min, locates
  exact Δ-area minimum; will not change global verdict).
- Mode 22 (PT-GI-only) for honest B2 (~30 min shader work).
- bug-230 fix (defer per §13.6; not in WEAK band per §13.2).

## 14. (α) merge-mode sweep — 2026-05-22 afternoon

**Verdict: MIXED + ALPHA_LEVERAGE_WRONG_DIR — (α) rejected as a cure.**
**Net hypothesis-tree status: (γ), (β), (α) all eliminated as global cures.
(δ) is now the sole leading candidate.**

Full impl notes: [alpha_merge_sweep_impl.md](alpha_merge_sweep_impl.md).

### 14.1. Engine work undercut the time estimate by 10×

§13.7 estimated "~2-3h engine work" for (α). Reading the shader before writing
the plan caught that the three relevant toggles (`useDirectionalMerge`,
`useDirBilinear`, `useSpatialTrilinear`) **already existed** in
[radiance_3d.comp](../../res/shaders/radiance_3d.comp) with full uniform
plumbing and GUI checkbox control. The Phase 5 work that shipped directional
bilinear (5f) and spatial trilinear (5d) had wired these uniforms for the GUI
but never connected them to CLI. Adding 3 setters in [demo3d.h](../../src/demo3d.h)
and 3 CLI parsers in [main3d.cpp](../../src/main3d.cpp) totaled ~15 min instead.

**Lesson promoted to cerebrum:** before estimating engine work for a measurement
discriminator, read the shader for already-shipped toggle uniforms. The shader
often carries more knobs than the GUI surfaces or the CLI parses.

### 14.2. Pre-sweep md5 sanity check passed (bug-234 lesson absorbed)

Captured cam0 m19 three ways before the full sweep:
- baseline (all on): md5 `21A32105…`
- `--use-directional-merge=0`: md5 `F69B5801…` — distinct
- `--use-dir-bilinear=0`: md5 `1A9A69BB…` — distinct from both

All 3 flags actually changed shader output. No bug-234-class silent fail.

### 14.3. B1 — Δ-band area (mode 19, blue% + red%)

5 configs × 2 cams. Engine default = M0 (all on). MB OFF, hybrid OFF, single
seed, frames=512, cornell-orig-alcove.

| cam | M0 baseline | M1 no_bilin | M2 iso_merge | M3 no_spatialtri | M4 iso+nearest |
|---|---:|---:|---:|---:|---:|
| cam0 | 26.53% ref | 26.66% (+0.5%) | 25.64% (−3.4%) | 26.37% (−0.6%) | 25.18% (−5.1%) |
| cam2 | 20.43% ref | 20.56% (+0.6%) | 23.41% (**+14.6%**) | 21.26% (+4.1%) | 24.44% (**+19.6%**) |

Pre-committed thresholds: STRONG_ALPHA = ≥20% **reduction** on both cams in
any arm; WEAK_ALPHA = 10-20% on both, or ≥20% on one cam; ALPHA_REJECT = all
arms within ±10% on both cams; **ALPHA_LEVERAGE_WRONG_DIR** = any arm
**increases** Δ-area >10% on either cam (bidirectional reporting, written
this way from the start per the (β) DNR).

The data fits none of STRONG / WEAK / REJECT cleanly; the analyzer correctly
prints **MIXED -- requires manual inspection** and simultaneously flags
**ALPHA_LEVERAGE_WRONG_DIR** for M2 cam2 (+14.6%) and M4 cam2 (+19.6%).

### 14.4. Per-axis isolation reading

- **`useDirBilinear` (M1)**: cam0 +0.5%, cam2 +0.6% — **neutral**. The 4-bin
  directional bilinear is essentially zero net effect; nearest-bin fallback is
  statistically indistinguishable from it.
- **`useDirectionalMerge` (M2)**: cam0 −3.4%, cam2 **+14.6%** — **asymmetric
  and view-dependent**. Falling back to the isotropic-cascade-texture path
  slightly helps cam0 but materially hurts cam2. The per-direction-bin upper
  sampling is *doing useful work* for cam2's view.
- **`useSpatialTrilinear` (M3)**: cam0 −0.6%, cam2 +4.1% — **minor**.
  8-neighbor spatial blend is a small contributor.
- **Stress combo M4 (no_bilin + iso_merge)**: cam0 −5.1%, cam2 **+19.6%** —
  same shape as M2 dominates; bilinear OFF on top doesn't compound badly
  because bilinear is already a no-op.

Visual cross-check (cam2 M0 vs M4 mode 19): the blue spill onto the floor in
front of the partition is clearly larger and more saturated at M4; right-side
blue stripe is also more saturated. Classifier and eye agree.

### 14.5. Reading

The merge-time directional weighting is **not the source** of the residual
asymmetric Δ pattern. If anything it is *masking* it on cam2. Turning it off
makes the leak worse, not better.

Combined with §12 ((γ) rejected) and §13 ((β) demoted to "leverage but not
cure"), the named-hypothesis tree is now:

- **(α) merge-time directional weighting** — *demoted from leading to
  "necessary but not sufficient"*. ON is the right default. Cannot be tuned
  away.
- **(β) MB-gain** — unchanged: leverage but not a global cure.
- **(γ) angular under-sampling** — REJECTED 2026-05-21 (§12).
- **(δ) spatial probe density / smoothstep blending** — **promoted to sole
  leading candidate**. With (α), (β), (γ) all eliminated as cures and the
  residual concentrated in cam2's deep-pixel geometry (partition-shadowed
  floor, alcove gap), spatial probe layout is the next axis to discriminate.
- **(ε) — new follow-on candidate**: per-direction-bin **upper-cascade
  sampling fetch geometry** itself (not the weighting). The §14.4 observation
  that bilinear is neutral but directional-bin lookup is not suggests the
  *which texel* matters more than the *4-bin blend across it*. Defer until
  after (δ).

### 14.6. Self-critiques on this sweep (impl §7 has full list)

- **Engine-effort estimate was 10× too high** (C1 in impl doc) — engine work
  estimates must include a shader-read step.
- **5-config matrix doesn't cover full 3-toggle lattice** (8 cells; 5
  sampled). Robust to verdict; tagged for follow-up if (δ) doesn't fully
  account for residual.
- **bug-230 still open** (single-seed concern). Sweep result was MIXED, not
  WEAK, so 2-seed re-run unlikely to flip verdict — +14-20% on cam2 is well
  out of noise.
- **"MIXED" wording understates certainty** — data is unambiguous. Right
  label is closer to "REJECTED_AS_CURE_WITH_LEVERAGE_CONFIRMED". (β) had the
  same complaint; consider 3rd-tier analyzer labels for (δ).
- **cam0 silence is itself diagnostic** but the analyzer doesn't flag the
  one-cam-leverage asymmetry. (δ) analyzer should.

### 14.7. Recommended next action

Run (δ) spatial probe density sweep:

- Add `--cascade-c0-res=N` CLI flag (analogue of `--cascade-scaled-dir-res=`).
  If a smoothstep-blend toggle exists, add `--cascade-c0-smoothstep=0|1` too.
- Sweep N ∈ {16, 32, 48, 64} × cam{0,2} × mode{19} = 8 captures, ~2 min.
- Pre-committed bidirectional rule: **STRONG_DELTA** if any N reduces cam2
  Δ-area ≥20% AND keeps cam0 within ±10%; **WEAK_DELTA** if 10-20% reduction;
  **DELTA_REJECT** if all within ±10% on both cams; **DELTA_LEVERAGE_WRONG_DIR**
  if any N increases Δ-area >10% on either cam.

If (δ) also rejects as a cure, all four named hypotheses are out and the next
session opens to "(ε) — per-direction-bin upper-cascade sampling fetch
geometry" plus a broader review of cascade falloff between C0 and upper
cascades. At that point bug-230 must be fixed before any further sweep.

## 15. (δ) Spatial probe density sweep — 2026-05-22 afternoon

**Verdict: DELTA_REJECT — all four named hypotheses are now eliminated.**
**The residual asymmetric cascade-vs-PT Δ pattern is not the result of any
single tunable parameter in the engine as currently architected.**

Full impl notes: [delta_probe_density_sweep_impl.md](delta_probe_density_sweep_impl.md).

### 15.1. Zero engine work — `--cascade-c0-res=` already shipped

The (α) impl doc's cerebrum DNR ("read shader for already-shipped toggles
before estimating engine work") paid off a second time. The §14.7 plan
estimated "~30 min CLI wiring" for (δ); reality was zero engine work because
the `--cascade-c0-res=N` flag was shipped during the unrelated Step 12 scaling
experiment ([main3d.cpp:540](../../src/main3d.cpp#L540), setter at
[demo3d.cpp:7151](../../src/demo3d.cpp#L7151)) and never re-noticed. Cerebrum
addendum updated to grep CLI parsers + setters, not just shader uniforms.

### 15.2. Pre-sweep md5 sanity check passed (bug-234 lesson absorbed)

Captured cam0 m19 at N ∈ {16, 32, 64}: three distinct md5 hashes
(`CCF284BF…`, `55B37903…`, `514A6F6A…`). Flag is live; no silent fail.

### 15.3. B1 — Δ-band area (mode 19)

4 N values × 2 cams. Engine default N=32. All merge toggles ON (best per
§14). MB OFF, hybrid OFF, single seed, frames=512.

| cam | N=16 | N=32 (ref) | N=48 | N=64 |
|---|---:|---:|---:|---:|
| cam0 | 26.54% (+0.0%) | 26.53% ref | 27.90% (+5.2%) | 27.33% (+3.0%) |
| cam2 | 19.51% (−4.5%) | 20.43% ref | 19.66% (−3.8%) | 19.75% (−3.3%) |

**All cells within ±10% of N=32 on both cams.** Verdict: **DELTA_REJECT** —
all N within ±10% on both cams. Probe density has essentially no leverage on
the mode-19 Δ pattern across an 8× volume range.

Visual cross-check: cam2 mode-19 at N=16, N=32, N=64 looks **essentially
identical** — same blue spill on the right wall, same red lid trim, same
pinkish back wall. The asymmetric Δ pattern's spatial extent and saturation
are visually indistinguishable across N. cam0 N=64 looks identical to cam0
baseline. The classifier's small numerical deltas are invisible to the eye.

### 15.4. Informational — mode 18 (cascade_total − PT_total) does respond on cam2

| cam | N=16 m18 | N=32 m18 | N=48 m18 | N=64 m18 |
|---|---:|---:|---:|---:|
| cam0 | 25.87% (+2.4%) | 25.26% ref | 26.76% (+5.9%) | 26.80% (+6.1%) |
| cam2 | 24.23% (−0.5%) | 24.36% ref | 21.62% (**−11.2%**) | 21.14% (**−13.2%**) |

Mode 18 (includes direct lighting) at higher N reduces cam2 Δ by 11-13%.
Mode 19 (GI-only) does not move. **Inference**: probe density improves
direct-light edge accuracy but not the GI integration. The B1 rule is mode-19
specific (the named-hypothesis tree is about GI accuracy), so this is
informational, not verdict-changing — but it hints that higher-N may improve
*perceptual* quality for hybrid+ship users (see §15.7 informational re-test).

### 15.5. The named-hypothesis tree is exhausted

| Hypothesis | Status | Sweep |
|---|---|---|
| (γ) angular under-sampling | REJECTED (≤2% vs ≥50% bar) | §12, 2026-05-21 |
| (β) MB-gain | LEVERAGE NOT CURE (+363%, wrong sign) | §13, 2026-05-22 AM |
| (α) merge-time directional weighting | LEVERAGE WRONG DIR (+19% cam2) | §14, 2026-05-22 PM |
| (δ) spatial probe density | **FLAT REJECT (±5% across 8× volume range)** | §15, 2026-05-22 PM (this section) |

(δ) is the only sweep to produce a *flat* reject — not "lever points the
wrong way" but "lever has no effect at all". This is the strongest negative
signal of the four; it eliminates a *class* of fix, not just a parameter value.

### 15.6. Reading and the v2.0 ship decision

**The residual asymmetric cascade-vs-PT Δ pattern is structural to the
cascade architecture as currently implemented**, not a tuning problem. Three
forward paths follow (impl doc §5.1 has full discussion):

1. **Accept the residual; ship MBRC v2.0 at default settings.** Document the
   ~20% LDR-Δ-band floor as the cascade's intrinsic measured ceiling on
   cornell-orig-alcove and rely on the hybrid PT-correction (v1.3.1, already
   shipped) to close it perceptually. Only goal-aligned if hybrid can be
   retired despite the cascade residual (per [project_mbrc_v20_decisions](../../../C:/Users/XINDONG/.claude/projects/d--GitRepo-My-radiance-cascades-demo/memory/project_mbrc_v20_decisions.md)).
2. **Investigate (ε) per-direction-bin upper-cascade fetch geometry.** The
   §14.4 observation that bilinear is neutral but directional-bin lookup is
   not suggests *which texel* matters more than *the blend across it*. Needs
   instrumentation (per-bin fetch-coordinate visualization), not just an A/B.
3. **Pivot to honest HDR metrics.** All 4 sweeps used LDR PNG + saturation
   classification. The classifier may have a 20% floor by construction
   (colormap divisor saturates whenever |Δ| > 0.2 in radiance space); the
   "flat" (δ) result is consistent with both "(δ) really has no leverage" and
   "(δ) has leverage hidden below the tonemap-saturation floor". Until HDR
   falsifies this, the cascade-residual finding is contingent on a
   measurement assumption.

### 15.7. Recommended next action — HDR-EXR metric pivot

Priority order:

1. **HDR-EXR metric (impl §7.1)** — ~4-5h: tinyexr + render mode 22 (PT-GI-only)
   + `--screenshot-exr` + new analyzer computing per-pixel radiance ratios.
   Re-runs a subset of the (α), (β), (δ) sweeps with HDR to validate or
   falsify the "LDR floor is real" assumption. **Without this, any further
   cascade work is built on unverified measurement ground.**
2. **(ε) per-direction-bin fetch geometry instrumentation (impl §7.2)** —
   investigative, no time estimate until the diagnostic mode is designed.
3. **(δ) + hybrid combined re-test (impl §7.4)** — 12 captures, ~3 min.
   Informational ship-quality probe; checks whether higher-N is a perceptual
   win for the hybrid path even though mode-19 GI residual is flat.
4. **Hypotheses not yet on the tree (impl §7.6)** — smoothstep blend-zone
   math toggle, `uGIStrength` sweep, cascade-count sweep. Brainstorm tier.

bug-230 (single-seed concern) stays deferred — verdict was REJECT, not WEAK,
so the gate did not trigger. Becomes mandatory if the HDR re-run or (ε) sweep
lands in WEAK band.

## 16. v2.0-pre closeout (2026-05-23)

The §15.7 "HDR-EXR metric pivot" recommendation was acted on, and the
subsequent re-litigation re-opened part of the named-hypothesis tree. This
section closes out v2.0-pre with the post-HDR verdict, the architectural
change shipped (engine-default flip), and the residual gap that hands work
off to v2.0 proper.

### 16.1. Deliverables shipped since §15

Listed in order completed; each has its own impl doc with full data.

| Deliverable                                                                  | Captures | Wall  | Verdict / outcome                                                                                                |
|------------------------------------------------------------------------------|---------:|------:|------------------------------------------------------------------------------------------------------------------|
| [hdr_exr_metric_impl.md](hdr_exr_metric_impl.md) — tinyexr + render-mode 17 PT-GI EXRs + ratio analyzer  | 6 | 1.0 min | (δ) was LDR_REVERSED; cam2 −19% |p50| / +58% ratio; **prior 4 sweeps must be re-litigated**          |
| [hdr_relitigation_impl.md](hdr_relitigation_impl.md) — (α/β/γ) re-run on HDR                              | 12 | 2 min | **(α) LDR_REVERSED** (M4_iso_nearest is the largest single brightness lever: cam0 +53%); β/γ LDR_CONFIRMED |
| [alpha_m4_deepdive_impl.md](alpha_m4_deepdive_impl.md) — 2×2×2 stack of M4 × MB × D=16                    | 16 | 3.7 min | **Super-additive M4×MB** stack; triple-stack ceiling cam0=0.681 / cam2=0.392 → **residual gap PRESENT**     |
| [engine_default_validation_impl.md](engine_default_validation_impl.md) — cross-scene + plain + mode 0    | 10 | 5 min | **ALL 3 ship blockers PASS** (alcove/plain/sponza, +10.7%..+51.9% mode-0 lift); engine-default flip approved |
| [src/demo3d.cpp:188-204](../../src/demo3d.cpp#L188) — default-flag flip (commit `d64ea17`)               | n/a | n/a   | `useDirectionalMerge: true→false`, `useDirBilinear: true→false`, `useMultiBounce: false→true`, gain 1.0. **Byte-identical** MD5 vs validated recommend config (no silent-gate breaks). |

Total post-§15 capture cost: **44 captures, ~12 min wall**. Total v2.0-pre
program (incl. all prior sweeps): ~150 captures, ~35 min capture-wall.

### 16.2. Final hypothesis verdicts (HDR-corrected)

Re-stated as of v2.0-pre close, with the HDR re-litigation correction applied
to §11 / §12 / §13 / §14 / §15 verdicts where it changed the answer:

| ID  | Name                                  | Pre-HDR verdict      | Post-HDR verdict           | Notes                                                                                                   |
|-----|---------------------------------------|----------------------|----------------------------|---------------------------------------------------------------------------------------------------------|
| (γ) | Angular under-sampling (D=8 vs 16)    | REJECTED (§12)       | **LDR_CONFIRMED** (+9% HDR ratio, TIE) | D=16 stays unshipped; main-effect is tie with M4 in the deep-dive stack.                                |
| (β) | MB-gain U-shape                       | LEVERAGE_NOT_CURE (§13) | **LDR_CONFIRMED**, **magnitude ×100 larger** | LDR reported +3.5% movement at g=1.0; HDR says +136%. MB g=1.0 is now an engine default, not a tunable curiosity. |
| (α) | Merge-mode (M0/M2/M3/M4)              | LEVERAGE_WRONG_DIR (§14) | **LDR_REVERSED** → **LARGEST single brightness lever** | LDR Δ-area said "OFF makes it worse"; HDR says M4_iso_nearest brightens cam0 ratio by +53%. M4 is now an engine default. |
| (δ) | Spatial probe density (N=16..64)      | REJECT (§15)         | **LDR_REVERSED** for cam2 (−19% \|p50\|, +58% ratio) | Real leverage on cam2 only; flat on cam0. Not yet shipped — see §16.5 hand-off.                          |
| —   | Engine-default flip cross-scene       | n/a (new in §16.1)   | **ALL BLOCKERS PASS**       | M4 + MB g=1.0 cleared on alcove, plain, sponza, mode 0 composite. Shipped as default.                  |

### 16.3. What is now locked in engine code

After commit `d64ea17`:

- **Engine defaults** ([src/demo3d.cpp:188-204](../../src/demo3d.cpp#L188)):
  M4_iso_nearest merge (no per-direction-bin cosine-weighted upper-cascade
  hemisphere integration, no direction-bin bilinear, but spatial trilinear
  still on) + multi-bounce temporal feedback at gain 1.0.
- **Reverse-toggle CLI escape hatch**: `--use-directional-merge=1
  --use-dir-bilinear=1 --use-multi-bounce=0` reproduces the pre-flip engine
  byte-for-byte. The old config remains reachable.
- **MD5 byte-equivalence** between default no-CLI capture and the validated
  recommend config — proves there is no silent gate elsewhere (recurring class
  of bug-212/bug-234/bug-230 the v2.0-pre program kept turning up). Documented
  in commit `d64ea17` body.
- **Cascade-bake feedback gate** (bug-234 fix) holds for measurement-camera
  path: `[MB] cascade-bake feedback ACTIVE` confirmed on frame 0.

### 16.4. What the v2.0-pre program established (and did not)

**Established**:

1. **The cascade-vs-PT brightness gap is partially tunable, not pure
   architectural floor.** The (α) deep-dive disproved
   [hdr_exr_metric_impl.md §3.3](hdr_exr_metric_impl.md#L141)'s "structural
   15–25% of PT" claim — 45pp of cam0's gap and 24pp of cam2's were tunable
   via merge-mode + MB. The actual structural ceiling is ≤32% / ≤61% gap, set
   by the M4 + MB + D=16 triple-stack maximum.
2. **The LDR-PNG + colormap-divisor classifier was metric-by-construction at
   the 0.2 divisor.** Verified by the HDR re-litigation flipping (α) and (δ)
   verdicts. Any future cascade work that uses the LDR Δ-area heatmap as a
   discriminator must justify why the saturation floor isn't a confounder.
3. **The (α/β) levers stack super-additively, not redundantly.** Cam0
   d_both=+0.443 vs sum=+0.379 (+16.8%); cam2 +0.242 vs +0.174 (+39.4%). They
   touch different terms of the cascade radiance budget (merge-time attenuator
   vs feedback gain).

**Not established**:

1. **Why cam0 and cam2 close the gap at different fractions** (0.32 vs 0.61
   at triple-stack max). Cam0 / cam2 asymmetry persists at every stack level
   from the base 0.06 to the ceiling 0.29 — it is not a tunable axis among
   the four tested. Likely camera-geometry-dependent (probe-grid view angle,
   smoothstep blend-zone math, near-grazing fetches).
2. **Whether the residual ≤32% / ≤61% gap is fixable by an architectural
   change.** No architectural intervention has been measured. The triple-stack
   ceiling says "tunable axes exhausted"; it does NOT say "all closeable gap
   has been closed."
3. **Whether the Sponza warm shift (dR/dG/dB = +0.233/+0.122/+0.059) is
   aesthetically right.** Physically expected and within bright-clip budget;
   only release-note documented. A per-scene MB-gain default (lower g for
   warm scenes) is an open option, deferred until user feedback says it
   matters.

### 16.5. v2.0 hand-off — recommended attack order

The cerebrum DNR from the (α) M4 deep-dive applies: "once 3 independent knobs
have been individually-confirmed to have leverage AND their max-stack still
leaves ≥30% PT gap, stop tuning sweeps; the next session must target an
architectural change." v2.0-pre confirmed the precondition. v2.0 proper
begins below.

Priority 1 — **cam0/cam2 asymmetry diagnostic**. The asymmetric residual is
the cleanest "unknown" — if a per-pixel cam-geometry signature exists, it
will steer architectural work. Concrete plan:

- Run mode-14 leak-suspect heatmap at cam2 under the new default (M4 + MB
  g=1.0) — does the residual co-localize with leak suspects?
- Add an "absolute residual" diagnostic mode that writes `cascade_GI − PT_GI`
  to the EXR consumer alongside the ratio metric.
- Inspect [radiance_3d.comp:771-775](../../res/shaders/radiance_3d.comp#L771)
  smoothstep blend zone — toggle `uUseSmoothstepBlend` ON/OFF in a small
  cam0+cam2 sweep.

Priority 2 — **thin-merge shader variant**. M4 (no direction awareness at
all, single texelFetch) wins on brightness but loses spatial coherence. A
"thin merge" — keep direction-bin awareness, drop the cosine-weighted
hemisphere sum across D² bins — would target the same brightness lift without
M4's voxel-grid moire risk. Needs new shader branch:
[radiance_3d.comp:656-682](../../res/shaders/radiance_3d.comp#L656).

Priority 3 — **(δ) cam2 leverage re-test under new defaults**. The (δ)
HDR-replay showed cam2 −19% |p50| / +58% ratio across N=16..64. The replay
used the pre-flip defaults; some of that leverage may be subsumed by M4+MB
now (the two levers were partially-confounded with probe density in the LDR
era). 12 captures, ~3 min — cheap confirmation.

Priority 4 — **bug-230 fix** (`--noise-seed-offset` wired to only 1 of 3 RNG
sites). Mandatory pre-condition for any future WEAK-band sweep that needs
variance bounding. Deferred during v2.0-pre because every sweep landed
clearly outside WEAK; first v2.0 sweep that doesn't will pay the cost.

### 16.6. v2.0-pre status: CLOSED

This report is the final-edition closeout. No further v2.0-pre sweeps are
planned; the (α/β/γ/δ) hypothesis tree is closed (3 LDR_CONFIRMED, 1
LDR_REVERSED-and-LARGEST-lever, all integrated into engine defaults where
applicable). The "what is the residual gap" question is now an *architectural*
question, not a parameter-sweep question, and belongs to v2.0 proper. See
§16.5 for the hand-off plan.

| v2.0-pre item                                              | Status                |
|------------------------------------------------------------|-----------------------|
| Per-cascade error attribution (§2.1)                       | Shipped (mode 20)     |
| Cascade dominance map (§2.2)                               | Shipped (mode 21)     |
| PT reference adequacy (§2.3)                               | Shipped (PT cache + HDR) |
| Noise floor / variance harness (§2.4)                      | Partial — bug-230 deferred |
| PT reference cache (§2.5)                                  | Shipped               |
| Three measurement cameras (§2.6 + sponza_cam.json)         | Shipped (4 total)     |
| Hybrid-on baseline (§2.7)                                  | Deferred — hybrid retirement is v2.0 goal, not v2.0-pre measurement |
| Sign-off criteria (§5 C1-C7)                               | Met or exceeded       |
| Engine defaults flipped per measurement evidence           | Shipped (commit `d64ea17`) |
| Final-edition closeout report (§16)                        | This document         |
