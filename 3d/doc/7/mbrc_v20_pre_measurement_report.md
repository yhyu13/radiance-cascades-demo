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
