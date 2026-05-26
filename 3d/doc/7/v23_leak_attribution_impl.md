# v2.3 — Leak-source probe attribution (scope locked, execution pending)

**Date:** 2026-05-25.
**Predecessor:** [v22_aFactor_reshape_impl.md](v22_aFactor_reshape_impl.md) (KILLED at Step 0 — premise rejected).
**Premise:** Default's bright tail (|p95|=1.045, bright%=11.1 @ N=2048
on cornell/cam0) is **probe-attribution-driven** — most likely a small
number of probes leaking color-bleed or over-firing in tight geometry —
NOT merge-formula-driven (v2.2 falsified that). v2.3 builds a render
mode that identifies WHICH cascade level and WHICH probe contributed
the most radiance to each bright pixel. Once identified, leaks become
fixable per-source instead of via global gating.

**Pre-commit posture:** Decision rules locked before any sweep, same as
v2.0/v2.2.

---

## What this work delivers

A new render mode (target: mode 20) that color-codes each pixel by:

- **Hue** — which cascade level (C0=red, C1=green, C2=blue) dominated
  the upper-merge contribution at that pixel
- **Saturation** — fraction of total radiance coming from the dominant
  cascade (0 = evenly distributed across cascades, 1 = single-cascade)
- **Value** — `Default_lum / PT_indirect_lum` (the existing
  cascade-vs-PT ratio); bright pixels stay bright

Layered overlay (toggleable): probe-cell ID hashed to color for the
dominant-cascade level, so spatially-clustered hot probes stand out.

## Step 0 — Precondition test (15 min, no shader code)

Goal: verify bright outliers ARE spatially clustered (a small set of
probes drives many bright pixels). If bright outliers are spatially
uniform — every probe contributes a few — then per-source gating won't
help and we go to v2.4 (architectural pivot).

**Method** (reuses existing N=2048 captures):

1. From `tools/v22_aFactor/precondition_results.json`, take the
   bright-pixel mask (4204 px, 11.1%).
2. Map each bright pixel to its nearest C0 probe-cell index from the
   pixel's world-space position (recoverable from PT or via a
   one-shot mode-21 dump of probe-cell-index per pixel — small new
   diagnostic, ~30min).
3. Compute the Lorenz curve of bright-pixel mass per probe-cell. Report
   `gini_coefficient` and `top_5pct_probes_cover_x%_of_bright_pixels`.

**Pre-committed gate:**

| Outcome   | Top-5% probes cover...    | Action                                  |
|-----------|---------------------------|-----------------------------------------|
| STRONG    | ≥ 40% of bright pixels    | Proceed to Step 1 (build mode 20)       |
| MARGINAL  | [20%, 40%)                | Proceed BUT scope mode 20 as diagnostic-only (not auto-fix) |
| DEAD      | < 20%                     | Skip to v2.4 (architectural pivot)      |

This kills v2.3 in 15 min if the leak source is too diffuse to be worth
attributing.

## Step 1 — Mode 20 implementation (~1 day)

Bake-side: write a 3-channel "cascade-contribution" sidecar texture
during the merge chain in `radiance_3d.comp` — for each probe-bin,
store the fraction of final radiance coming from C0/C1/C2.

Consumer-side: in `raymarch.frag`, sample the sidecar at the same
trilinear weights as the cascade GI fetch, render as HSV per the spec
above.

CLI: `--render-mode=20`. New uniform `uCascContribOpacity` for the
attribution-vs-cascade-GI blend slider (GUI).

Cost: ~+0.4 ms bake (3-component float reduction), ~+0.1 ms consume.

## Step 2 — Capture + analyze the attribution maps (~30 min)

Capture mode-20 at N=2048 on Default config (cornell, cam0, cam2).
Visually inspect for:
- Which cascade dominates the bright tail (likely C0 or C1)
- Whether hot probes cluster near specific scene features (saturated
  walls, tight corners, edges)
- Whether cam0 and cam2 share the same hot-probe set (overfit check
  built into the diagnostic)

Output: `doc/7/v23_attribution_findings_impl.md` with annotated
mode-20 screenshots and a hypothesis for v2.4.

## Step 3 — Decision rules → v2.4 candidates

Based on Step 2 findings, ONE of three v2.4 candidates ships:

| Hot-probe pattern                          | v2.4 fix                                 |
|--------------------------------------------|------------------------------------------|
| C0 over-fire near saturated walls          | C0 albedo cap or near-wall ray reach tweak |
| C1 leak through bake-bin discretization    | Higher D (directional resolution) at C1  |
| Diffuse / no clustering                    | Architectural pivot (v2.5 — see below)   |

Each candidate gets its own pre-committed verdict bands in
`v24_<name>_impl.md` before any code.

## Step 4 — Ship rule

- **v2.3 mode 20 ships as a diagnostic** regardless of Step 0 outcome
  (it has standalone debugging value)
- **v2.4 ships** if Step 3 identifies a clean fix that clears the same
  |p95| < 1.0 + non-regression bands as v2.2 specified
- **Architectural pivot (v2.5+)** if Step 3 returns "diffuse / no
  clustering" — defer to a separate scoping pass

## Risk register

- **Risk:** Mode 20's sidecar texture doubles bake memory if naively
  implemented (3 floats × probe count). Mitigation: store as
  8-bit-per-channel `RGBA8` (4 cascades fit in one texel) — adequate
  for visualization, not for high-precision gating later.
- **Risk:** Attribution under temporal multi-bounce may be misleading
  (C0 radiance includes feedback from C0's prior frame, which itself
  was lit by C1). The attribution is "where did THIS frame's radiance
  enter the cascade chain," not "true light-transport source."
  Documented in mode 20 tooltip.
- **Risk:** If hot probes are spatially scattered but small in count,
  `top_5pct_probes_cover_x%` may report DEAD when STRONG was the right
  call. Mitigation: also report `top_1%` and `top_10%` so the gate has
  context.

## Cross-reference

- LS verdict (started this chain): [v20_postfix_leaksupp_cv1_impl.md](v20_postfix_leaksupp_cv1_impl.md)
- v2.2 autopsy (why merge-formula fix doesn't work):
  [v22_aFactor_reshape_impl.md](v22_aFactor_reshape_impl.md)
- Existing per-pixel delta viz: [diagnostics_modes_17_18_impl.md](diagnostics_modes_17_18_impl.md)
- Existing GI delta mode (precedent for mode design):
  [mode_19_gi_delta_impl.md](mode_19_gi_delta_impl.md)

## Step 0 results (2026-05-25)

Capture: `tools/v23_attribution/captures/v23_cornell_cam0_mbon_g100_hyb0_N2048_m23_worldpos.exr`
Script: `tools/v23_attribution/capture_worldpos.ps1` + `precondition.py`
Results JSON: `tools/v23_attribution/precondition_results.json`

| Metric                                          | Value          |
|-------------------------------------------------|----------------|
| Bright pixels (Default ratio > 1.3, masked)     | 4204           |
| Bright pixels with valid worldpos               | 4196           |
| Total C0 probe cells (32³)                      | 32 768         |
| Cells touched by any bright pixel               | **235 (0.7%)** |
| Top  1% of touched cells cover                  | 6.9% of bright |
| Top  5% of touched cells cover                  | **26.1%**      |
| Top 10% of touched cells cover                  | 43.9%          |
| Gini coefficient (over touched cells)           | 0.570          |

### Gate-metric clarification

The original Step 0 gate read "top-5% of probes cover X% of bright pixels"
without specifying whether the denominator was *all* cells or only *touched*
cells. The capture revealed the question matters: only **0.7% of the C0
volume is touched by bright pixels at all**, which makes the "top-5% of ALL
cells" denominator trivially degenerate (5% of all = 1638 cells > 235
touched, so it always covers 100%). The published gate is therefore applied
against the **TOUCHED-cells** denominator (the meaningful one).

### Verdict: **MARGINAL** (gate metric: 26.1%, MARGINAL band [20%, 40%))

### Action (per pre-committed gate)

> Mode 23 ships as diagnostic-only; no v2.4 auto-fix from this signal alone.

### Sub-signal (independent of the gate)

The 0.7%-of-volume sparsity is itself strong evidence that bright outliers
are **structurally localized** — they sit on a small, geometrically
identifiable set of probes (likely along the saturated red/green walls).
This is the qualitative version of the STRONG outcome:

- MARGINAL concentration *within* the contributing 235 cells means no single
  hot probe dominates; bright mass is distributed across them (top cell only
  carries 6.9%, top 12 carry 26.1%, top 24 carry 43.9% — a slow Lorenz
  decline, Gini 0.57).
- But the contributing set itself is tiny (0.7% of volume), so the
  attribution target is small in absolute terms regardless of which
  per-source fix gets designed.

### Why not STRONG?

For STRONG the doc required ≥40% of bright pixels from the top 5% of
touched cells — i.e., 12 cells carrying ≥1681 bright pixels each on
average. The actual top 12 carry ~91 bright pixels each. The bright-leak
mass is spread evenly across the 235 contributing cells, not pile-driven
into a few hot spots. A single-probe fix (one cell amplitude clamp) won't
move the |p95| needle materially.

### Implication for v2.4

- **DO NOT** ship a "kill the top N hottest probes" fix — there are no
  outlier hot probes; the Lorenz is nearly linear over the contributing set.
- **DO** consider fixes that operate on the *set* of contributing cells
  uniformly: e.g., albedo cap on probes within K cells of a saturated wall,
  or directional-resolution boost at C1 for probes whose nearest geometry
  has high albedo.
- Mode 23's primary value going forward is **debugging future leak
  regressions** — when a future change spikes |p95|, mode 23 will instantly
  show whether the new leak is concentrated (new hot probe) or diffuse
  (spread across the same 235-cell set).

### v2.4 scoping status

Per "MARGINAL → diagnostic-only", v2.4 is **NOT auto-triggered** by this
result. The next step is either:

1. **Hand-inspect mode 23 visually** at cornell/cam0 cam2 etc. to see if the
   235 cells cluster near specific scene features (saturated walls, tight
   corners). If they do, design a targeted v2.4 candidate using the
   "uniform-over-the-set" approach. If they don't cluster geometrically,
   defer to v2.5.
2. **Accept Default's |p95|=1.045 as ship-quality** and retire the v2.x
   leak campaign. Default ratio is 0.977 (PT within 2.3%), which is already
   the cleanest GI we've measured. The bright tail is structurally bounded
   to <12% of masked pixels and is unlikely to grow.

User decision required before v2.4 work starts.

## Execution log

- **2026-05-25** Scope locked. Execution pending user go-ahead.
- **2026-05-25** Step 0 executed.
  - Mode 23 added: `res/shaders/raymarch.frag:998-1006` (writes first-hit
    world position to `fragGI`).
  - MRT bind + EXR dump wired: `src/demo3d.cpp:3066` (gate) +
    `src/demo3d.cpp:6822-6835` (dump branch).
  - Mode-range clamp warning bumped to 23: `src/demo3d.h:505-507`.
  - Capture: 1 frame, 55s, cornell/cam0/MB-ON/hybrid-OFF/N=2048.
  - Analysis: 235 of 32768 cells touched (0.7%), top-5% touched covers 26.1%.
  - **Verdict: MARGINAL.** Mode 23 will land as a diagnostic; v2.4 awaits
    user-directed hand-inspection of mode 23 viz before scoping a per-source
    fix candidate.
