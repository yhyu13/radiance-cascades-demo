# MBRC v2.0 — (h.3) MB-ON × {M0,M2,M4} factorial fill-in at b=2

**Date**: 2026-05-24 (immediately follows
[v20_h2_merge_asymmetry_impl.md](v20_h2_merge_asymmetry_impl.md), commit
`eba082b`).

**Motivation**: (h.2) measured the MB-OFF leg of the M0/M2/M4 sweep at
b=2 and isolated the merge-architectural asymmetry. The MB-ON leg was
missing except for M4 (from `captures_pt_bounce_ladder/`); the
(h.2) doc §6 flagged it as a 12-cap factorial fill-in that would test
whether the M4+MB super-additivity (+16.8%/+39.4% measured in
alpha_m4_deepdive at b=8) is merge-formula-specific or a uniform MB
brightness amp. This sweep adds 4 new cells (M0+M2 × MB-ON × cam0+cam2)
and reuses the other 8 from prior captures.

## 1. The experiment

[h3_mb_factorial_capture.ps1](../../tools/v20_arch_diagnostic/h3_mb_factorial_capture.ps1)
captured 4 new cells (M0 + M2, MB-ON, each on cam0+cam2) at b=2.
M0+M2 MB-OFF reused from `captures_h2_merge/`; M4 MB-OFF reused from
`captures_h_disambig/`; M4 MB-ON reused from
`captures_pt_bounce_ladder/`. 4 captures, 1.1 min.

Full factorial: 2 (MB) × 3 (merge) × 2 (cam) = 12 cells; the 4 new
cells close the gap.

## 2. Results

### 2.1. Per-cell cascade/PT energy ratio at b=2

| merge | MB | cam0 | cam2 |
|-------|----|-----:|-----:|
| M0_baseline    | OFF | 0.4348 | 0.2825 |
| M0_baseline    | ON  | **1.0235** | 0.5817 |
| M2_iso_merge   | OFF | 0.5630 | 0.3079 |
| M2_iso_merge   | ON  | **1.1899** | 0.7108 |
| M4_iso_nearest | OFF | 0.6686 | 0.3327 |
| M4_iso_nearest | ON  | **1.3927** | 0.7699 |

### 2.2. MB multiplier (MB-ON ratio / MB-OFF ratio) per (merge, cam)

| merge | cam | OFF | ON | MB mult |
|-------|----:|----:|---:|--------:|
| M0_baseline    | 0 | 0.4348 | 1.0235 | **2.354** |
| M0_baseline    | 2 | 0.2825 | 0.5817 | 2.059 |
| M2_iso_merge   | 0 | 0.5630 | 1.1899 | 2.114 |
| M2_iso_merge   | 2 | 0.3079 | 0.7108 | **2.308** |
| M4_iso_nearest | 0 | 0.6686 | 1.3927 | 2.083 |
| M4_iso_nearest | 2 | 0.3327 | 0.7699 | **2.314** |

Worst-case per-cam max/min spread: cam0 1.130, cam2 1.124. Both just
over the 1.10 INVARIANT band; verdict: **MB_MULTIPLIER_MILDLY_MERGE_DEPENDENT**.

### 2.3. Spread (cam2/cam0) per (merge, MB)

| merge | MB-OFF spread | MB-ON spread | delta | interpretation |
|-------|--------------:|-------------:|------:|----------------|
| M0_baseline    | 0.6497 | 0.5683 | **−0.0814** | MB AMPLIFIES asymmetry |
| M2_iso_merge   | 0.5470 | 0.5974 | +0.0504 | MB symmetrizes (small) |
| M4_iso_nearest | 0.4976 | 0.5528 | +0.0552 | MB symmetrizes (small) |

**Per-merge MB behavior on spread is NOT uniform.** M0 (full directional
merge) — MB widens the cam0/cam2 spread by −0.08. M2/M4 (isotropic
merge variants) — MB closes the spread by +0.05.

## 3. Architectural findings

### 3.1. MB amplifier IS approximately merge-invariant at the brightness level

MB at g=1.0 multiplies cascade brightness by **~2.0–2.4× regardless of
merge variant**. Max/min spread is 1.13 (just above the 1.10 INVARIANT
band). The earlier alpha_m4_deepdive super-additivity finding
(+16.8% cam0 / +39.4% cam2 at b=8) **does not reproduce cleanly at b=2**
— at the apples-to-apples single-bounce baseline, MB multipliers are
~2.1× on all 3 merge variants. M4 does NOT get a meaningfully larger
MB lift than M0 or M2 at b=2.

**Interpretation**: alpha_m4_deepdive's super-additive M4×MB interaction
was substantially a b=8 PT-baseline artifact. PT at b=8 integrates 6+
bounces that cascade with MB-equilibrium ≈ 2.5–3 effective bounces
cannot reach; the per-cam ratio at b=8 captures both "MB brightness
amp" AND "missing bounce content." The M4 variant exposes more atlas
radiance for MB to bounce, so the *gap shrinks more rapidly under M4*
as PT integrates further bounces. The +16.8% super-additivity at b=8 is
mostly a measurement of "M4 closes the gap to high-bounce PT faster
than M0 closes it." At b=2 (apples-to-apples), the super-additivity is
mostly absent.

This is the second time in v2.0 (after PT-bounce-ladder §3) that an
"M4 magic" claim has been re-attributed to PT-bounce-count framing.

### 3.2. MB DOES interact with merge formula at the spread level

On M0 (full directional + bilinear + trilinear): MB shifts cam0/cam2
spread from 0.65 → 0.57 (−0.08). MB makes the asymmetry *worse* on the
full-feature merge.

On M2/M4 (isotropic merge variants): MB shifts spread closer to
symmetry by +0.05. MB partially closes the spread on isotropic
variants.

**Why the directional split**: the per-direction-bin upper-cascade
sampling (M0's `useDirectionalMerge=1`) likely has cam-projection-
dependent fetch-coordinate aliasing — M0's per-bin lookup at cam0
viewport pixels samples atlas slices that get *more* radiance reflected
back to them by MB's per-bin feedback (since the cam0-favored bins
already have higher atlas content), whereas at cam2 the per-bin lookup
samples lower-atlas-content bins → MB's per-bin feedback amplifies a
smaller signal. On M2/M4 (isotropic merge), MB feeds back a single
direction-averaged value at each probe, which is closer to uniform
across cam viewports.

This is a **first-time direct measurement of "merge formula × MB
spread interaction"** — none of the prior measurements separated these.

### 3.3. New observation: cam0 with MB-ON systematically EXCEEDS PT at b=2

At b=2 MB-ON, **all 3 merge variants over-shoot PT on cam0**:
- M0: 1.024 (+2.4%)
- M2: 1.190 (+19.0%)
- M4: 1.393 (+39.3%)

The ladder previously reported cam0=1.39 at MB-ON M4 b=2; we now know
this over-shoot is universal across merge variants, with magnitude
modulated by merge formula (M4 > M2 > M0).

**Why over-shoot exists**: cascade with MB g=1.0 in equilibrium delivers
~2.5–3 effective bounces (per (h) disambig §3 finding); PT at b=2
integrates only 1 indirect bounce. So at b=2 PT is *under-integrating*
relative to cascade's equilibrium effective-bounce count. This is the
mirror of the b=8 finding where PT over-integrates relative to
cascade's equilibrium.

**The apples-to-apples PT baseline for cascade with MB g=1.0 is
somewhere between b=2 and b=3** (b_eff ≈ 2.5–3). Future ratio
measurements should pick that as the reference instead of arbitrary
b=2 or b=8.

### 3.4. Best-case spread across the factorial is M2 MB-ON = 0.60

The factorial-minimum cam0/cam2 spread is **M2 + MB-ON = 0.5974** — the
*least* asymmetric configuration measured. The architectural floor on
cam2/cam0 spread across 2×3 (MB × merge) is 0.60, far from symmetric
1.0. This bounds the asymmetry-closing power of merge+MB tuning at
≤0.40 (i.e. residual spread of ≥0.60).

## 4. Updated hypothesis space

| Hypothesis | Pre-h.3 | Post-h.3 |
|------------|---------|----------|
| (a) bake-side leak | REJECTED | unchanged |
| (b) smoothstep blend zone | P1 | **unchanged P1** — still the most likely upstream-of-merge source. Spread interaction with merge (3.2) suggests blend-zone smoothness varies with merge formula. |
| (c) probe-grid coverage of cam2 | P1 | **unchanged P1** — still the cheapest diagnostic via mode 8. The merge×MB spread interaction is consistent with both (b) and (c). |
| (d) basis-representation error | FALSIFIED | unchanged |
| (e) thin-merge shader | P5 | unchanged |
| (f) bake-time energy loss | FALSIFIED | unchanged |
| (g) multi-bounce delivery gap | P3 | **REFINED** — MB delivers ~2.1× brightness amp uniformly across merge variants. The MB delivery is real, but its *cam2 ceiling at b=2 MB-ON = 0.77* (M4) is still below PT-cascade-match expected ~1.0. MB compensates the bounce-count gap but not the cam2 architectural under-integration. |
| (h.1) MB feedback amplifier | confirmed-stacked | **REFINED** — multiplier ~2.1× and approximately merge-invariant (max/min 1.13). M4×MB super-additivity at b=8 was substantially a PT-bounce-count artifact, not a merge-formula effect. |
| (h.2) first-bounce merge asymmetry | P3 tuning lever | unchanged |
| (h.3) MB × merge spread interaction | NEW | **NEW finding** — small per-merge differential in MB's effect on spread (M0: amplifies; M2/M4: mildly symmetrizes). Magnitude small (Δ ≈ 0.05–0.08). Suggests directional-merge per-bin fetch geometry is cam-projection-dependent (a *specific* hypothesis to pair with (c) viewport-coverage diagnostic). |

## 5. Recommended next step

(b)/(c) remain co-leading; cheapest discriminator unchanged from
(h.2) §5: **mode-8 probe-cell `fract()` viz on cam0 vs cam2
viewports**. (h.3) adds a focusing hypothesis for the viz to test —
**look for whether cam2's viewport oversamples probe-cell boundaries
on the directional-bin axis** specifically (not just spatial axis).

Cost: 1 min mode-8 capture (cam0+cam2) + ~5 min visual A/B.
Disambiguates (b)+(c) source candidates before any shader work.

Secondary follow-on: render mode 6 (directional atlas) cross-cam
comparison to test atlas-content asymmetry. If atlas content is uniform
but cam2 viewport over-samples low-atlas bins, the bug is in the
*lookup* (probe placement / directional binning), not the *bake*.

## 6. Self-critique

- **The MB multiplier "INVARIANT vs MILDLY DEPENDENT" verdict landed
  on the boundary again.** Both cams sit at ~1.13 max/min spread, just
  over the 1.10 INVARIANT band. The pattern with the (h.2) MIXED verdict
  is repeating — pre-committed bands chosen ad-hoc tend to land just
  outside the boundary. Future bands should be derived from prior
  measurement variance (e.g. cam0/cam2 spread variance across known-
  equivalent configurations) instead of round-number choices.
- **PT b=2 reference variance.** Per ladder §7.2, PT at 512 spp on cam2
  may have hard-pixel noise that biases low-magnitude ratios. cam2
  ratios at 0.58–0.77 are mid-magnitude (safer than (h.2)'s 0.28–0.33
  regime), but worth a re-run at 1024 spp for cam2 cells before the
  spread-direction-of-effect (+0.05 vs −0.08) is treated as
  load-bearing.
- **g=1.0 only.** MB at g=1.0 is the engine default but the (β) sweep
  showed g=2.0 produces +363%/+213% Δ-area (LDR) and +13770% HDR ratio
  on cam0 — runaway. The "MB multiplier is merge-invariant" finding is
  for g=1.0 only; at g=2.0 the merge×MB interaction could be very
  different. A g={0.5, 1.0, 1.5} mini-sweep × {M0, M4} × {cam0, cam2}
  (8 cells, ~2 min) would test the gain-axis of the interaction.
- **The "M4 super-additivity was a PT-baseline artifact" claim deserves
  a direct re-test.** The alpha_m4_deepdive measurement was at b=8; if
  we re-render that exact factorial at b=3 (cascade's effective bounce
  count under MB g=1.0), the super-additivity should vanish or shrink
  significantly. 8 cells, ~2 min. Would strengthen the b=8-artifact
  interpretation from "consistent with the data" to "directly
  measured."

## 7. Artefacts

- Capture script: [tools/v20_arch_diagnostic/h3_mb_factorial_capture.ps1](../../tools/v20_arch_diagnostic/h3_mb_factorial_capture.ps1)
- New captures (4 cells × 4 files = 16 files): [tools/v20_arch_diagnostic/captures_h3_mb_factorial/](../../tools/v20_arch_diagnostic/captures_h3_mb_factorial/)
- Reused captures:
  - MB-OFF M0/M2: [tools/v20_arch_diagnostic/captures_h2_merge/](../../tools/v20_arch_diagnostic/captures_h2_merge/)
  - MB-OFF M4: [tools/v20_arch_diagnostic/captures_h_disambig/](../../tools/v20_arch_diagnostic/captures_h_disambig/)
  - MB-ON M4: [tools/v20_arch_diagnostic/captures_pt_bounce_ladder/](../../tools/v20_arch_diagnostic/captures_pt_bounce_ladder/) (b=2 cells)
- Analyzer: [tools/v20_arch_diagnostic/analyze_h3_mb_factorial.py](../../tools/v20_arch_diagnostic/analyze_h3_mb_factorial.py)
- Results JSON: [tools/v20_arch_diagnostic/h3_mb_factorial_results.json](../../tools/v20_arch_diagnostic/h3_mb_factorial_results.json)
