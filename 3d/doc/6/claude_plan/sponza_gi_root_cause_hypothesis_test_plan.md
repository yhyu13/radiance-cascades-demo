# Plan: Sponza GI Root-Cause Hypothesis Test (revised after critic 01)

## Changelog (post critic `01_sponza_gi_root_cause_hypothesis_test_plan_review.md`)

All 10 findings accepted:

- **F1 (medium) doc fix.** Probe-count range "8×" → "**64×**"
  (16³ → 64³ = 4K → 262K probes).
- **F2 (medium) doc fix.** Cascade bake cost at C0=64 "~5×" →
  "**~10× per Step 12 measured data**" (32³ = 4.7 ms → 64³ = 44.6 ms).
- **F3 (medium) doc fix.** Cornell anyPct "~80%" claim dropped — codex
  09 only measured Sponza; the figure was a hypothetical baseline, not
  a Cornell measurement. Optional Cornell captures reframed as "data
  only," not as a falsification anchor.
- **F4 (medium) plan revision.** Added `--auto-rdoc` to every capture
  so the Phase 12b auto-burst fires and `tools/probe_stats_*.json`
  files actually get written. Without this, the "spot-check anyPct
  from JSON" verification path can't fire.
- **F5 (medium) plan revision.** Added `--gi-blur-radius=1` to every
  capture. Default radius=8 is the bilateral smoother that exists to
  hide probe-density-driven detail — measuring "does density help?"
  with the smoother on is self-defeating.
- **F6 (medium) doc fix.** Acknowledged the codex 09 P0 NaN/Inf
  first-frame contamination (cascade textures still not
  zero-initialized; codex 10 plan never landed). meanLum trend
  reading must skip the first 2 `[4c A/B]` log lines.
- **F7 (low → medium-impact) plan addition.** At the winning density
  (likely C0=48 or 64), added a blur-radius A/B (1 vs 8) so we can
  disentangle H1 (density-bound) from H4 (algorithmic) — Type-A
  banding is density-coupled and would otherwise confound the result.
- **F8 (low) doc fix.** Smoothstep blend ref `:388-389` →
  `radiance_3d.comp:387-393` (or `:390` for the call specifically).
- **F9 (low) doc fix.** Runtime estimate "~2 min" → "**~5-7 min**"
  (auto-rdoc warmup × 4 + cubic-scaling cascade work + F7 blur axis).
- **F10 (low) doc fix.** Report output filename:
  `sponza_gi_root_cause_hypothesis_test.md` →
  `sponza_gi_root_cause_hypothesis_test_impl.md` (matches the `_plan.md`
  / `_impl.md` convention from `doc/5/claude_plan/`).

Net effect: 4 captures → **6 captures** (4 density + 2 blur-radius A/B
at winning density). Two real flags added to every command
(`--auto-rdoc`, `--gi-blur-radius=1`). Test design now disentangles
three previously-confounded axes (density, blur smoothing, algorithmic
artifact).

## Changelog (post kilo `sponza_gi_quality_diagnosis.md` reply)

Three new hypotheses added based on kilo's diagnosis (see
[reply_kilo_sponza_gi_quality_diagnosis.md](critic/reply/reply_kilo_sponza_gi_quality_diagnosis.md)
for full critique — 2 of 3 kilo claims verified, P1 framing rejected):

- **H5 added**: isotropic `volumeSize=(4,4,4)` mismatched to Sponza's
  3.8:1.59:2.34 aspect ratio. Sponza's normalized bounds fill X
  edge-to-edge but only [-0.79, 0.79] in Y → ~50% of probes wasted in
  empty Y space. Anisotropic volume sizing should raise anyPct without
  changing probe count.
- **H6 added**: `sampleProbeDir` ([raymarch.frag:297-310](../../res/shaders/raymarch.frag#L297))
  has no probe-to-surface visibility check; only cosine weight. Light
  leaks through thin walls (Sponza arches/columns/curtains).
- **H7 added**: boundary clamp at [raymarch.frag:328-335](../../res/shaders/raymarch.frag#L328)
  duplicates edge probes for out-of-grid positions, leaking interior
  radiance to surfaces just outside the volume.
- **Phase 3 added** to Test Design — these 3 require small code changes
  (not just CLI flags) so split out as a follow-up session if Phase 1+2
  don't produce a clear H1 win.
- **Per-cascade anyPct logging emphasized** in "What to Measure": kilo
  surfaced that `--cascade-c0-res` only directly scales C0; in
  non-co-located mode upper cascades scale as N/2/4/8. So C0=64 means
  C1=32, C2=16, C3=8 — upper cascades go from "tiny" to "merely small".
  Need to track whether upper-cascade anyPct improves alongside C0.
- **Kilo's P1 ("no multi-cascade merge in raymarch")** explicitly
  REJECTED in the hypothesis list — the merge happens at bake time
  (radiance_3d.comp:367-397 reads upper cascade and inherits via
  smoothstep blend); claim is technically true for the render path but
  conceptually wrong about where the multi-cascade contribution lives.
  Adding kilo's proposed render-time merge would double-count
  non-overlapping intervals.

## Context

User question (paraphrased): "Why does Sponza GI look bad — light leaking,
not enough bounces? Is the cascade fulfilling the entire scene? Is it
density? Or is the algorithm wrong?"

Step 11 codex 09 verification report measured C0 anyPct ≈ 3.5% on
Sponza-master (i.e., 96.5% of near-field probes find no surface in their
ray budget at default 32³ probe-res). Step 12 scaling experiment confirmed
cascade work scales cubically with probe-res.

Combined with the new lighting controls (`--light-direction`,
`--light-intensity`, `--ambient-bake-strength`,
`--ambient-composite-strength`), we can now run a clean A/B that isolates
**probe density** as the dominant suspect, without the ambient floor's
amplification masking the real GI signal.

## Hypotheses (ranked by prior probability)

| # | Hypothesis | Predicted A/B outcome |
|---|---|---|
| **H1** | Probe density is the dominant cause | At C0=64 vs C0=32: dramatic reduction in light leaking + brighter shadowed crannies + visible colored bounce on column shadow-sides |
| H2 | SDF voxelization (128³ ≈ 3 cm voxels) is the dominant cause | At C0=64 vs C0=32: minimal change (probes leak through the same SDF holes either way) |
| H3 | Single-bounce limitation is the dominant cause | At C0=64: brighter bake but shadow regions stay near-black (would need multi-bounce to fill) |
| H4 | Algorithm bug (probe interpolation / cascade merge math) | Density sweep doesn't fix the visual class of artifacts (banding stays at the same world-space scale) |
| **H5** | Isotropic `volumeSize=(4,4,4)` mismatched to Sponza's 3.8:1.59:2.34 aspect → ~50% probes wasted in empty Y space (kilo P0 sub-fix 1a) | Anisotropic `volumeSize≈(4,1.67,2.47)` should raise anyPct from 4.3% significantly without changing probe count |
| **H6** | No probe-to-surface visibility check causes light leaking through thin walls (kilo P2; verified at [raymarch.frag:297-310](../../res/shaders/raymarch.frag#L297)) | Adding `shadowRay()` in `sampleProbeDir` should visibly reduce through-wall leaking; possibly slight overall darkening |
| **H7** | Boundary clamp at volume edges (`raymarch.frag:328-335`) leaks edge-probe radiance to surfaces just outside the grid (kilo P0 sub-fix 1c) | Returning zero (instead of nearest-edge probe) for out-of-grid positions should slightly darken surfaces near the `[-1.9, 1.9]` boundary |

**On kilo's P1 ("no multi-cascade merge in raymarch.frag")** — see
[reply_kilo_sponza_gi_quality_diagnosis.md](critic/reply/reply_kilo_sponza_gi_quality_diagnosis.md).
Rejected as a hypothesis: the multi-cascade hierarchy IS being merged at
**bake time** (`radiance_3d.comp:367-397` reads upper cascade and inherits),
just empty in Sponza because upper cascades are also at ~0% surface hits
(same root cause as H1). Adding render-time merge would double-count
non-overlapping intervals and break the architecture.

## Test Design

A/B comparison at the established Sponza viewpoint, holding ALL other
variables constant. Per critic 01 F4+F5+F7, the command now includes
`--auto-rdoc` (so probe_stats JSON fires) and `--gi-blur-radius=1` (so
the bilateral smoother doesn't hide the density signal):

```
RadianceCascades3D.exe \
    --window-size=1280,720 \
    --load-obj=sponza-master --gpu-voxelize --gpu-sdf \
    --camera-pos=1.0710,-0.0723,-0.3393 \
    --camera-target=0.1212,-0.0812,-0.6520 \
    --light-direction=-0.3,-1.0,-0.4 \
    --light-intensity=2.0 \
    --ambient-bake-strength=0.0 \
    --ambient-composite-strength=0.0 \
    --gi-blur-radius=1 \
    --cascade-c0-res=N \
    --auto-rdoc \
    --exit-frames=900 \
    --screenshot=tools/sponza_density_AB_cN.png
```

**Phase 1 — Density sweep**: `--cascade-c0-res` ∈ **{16, 32, 48, 64}**
(4 data points spanning **64× probe-count range** — 16³=4K → 64³=262K
probes per critic 01 F1). Lower bound 16 added to anti-confirm
(below-default density should look worse if H1).

**Phase 2 — Disentangle H1 from H4 at winning density** (critic 01 F7):
at the density value that wins Phase 1, run a blur-radius A/B:
`--gi-blur-radius=1` (already captured in Phase 1) vs
`--gi-blur-radius=8` (added). 1 extra capture. Disentangling rule:
density-bound artifact changes with C0 not blur; filter-bound changes
with blur not C0; algorithmic remains at fixed scale across both axes.

**Phase 3 — H5/H6/H7 test (kilo's findings)**: each requires a small
code change rather than just a CLI flag, so these are proper experiments
not pure A/B captures. Defer to a separate exec session if Phase 1+2
don't produce a clear H1 win:

| Hyp | Required change | Capture pair |
|---|---|---|
| **H5** anisotropic volume | In `loadOBJMesh`, set `volumeSize` per-scene from OBJ aspect ratio (Sponza: ~`(4, 1.67, 2.47)`) instead of hardcoded `(4,4,4)` at [demo3d.cpp:170](../../src/demo3d.cpp#L170). Affects probe density distribution. | Sponza at C0=32 with isotropic vs anisotropic volume. Compare anyPct. |
| **H6** probe-surface visibility | Add `shadowRay()` call in [raymarch.frag:297-310](../../res/shaders/raymarch.frag#L297) `sampleProbeDir`, gating each direction-bin's contribution on probe-to-surface visibility. | Sponza at C0=32 with/without visibility gate. Compare leak through arches. |
| **H7** boundary clamp | At [raymarch.frag:328-335](../../res/shaders/raymarch.frag#L328), replace clamp-to-edge with return-zero for out-of-grid positions. | Sponza at C0=32 with/without boundary fix. Look at near-edge surfaces (Sponza ceiling at Y≈0.79, near volume edge Y=2). |

**Constants**:
- Camera + light direction (from cam.md viewpoint, slight downward sun)
- Light intensity 2× to make any bounce signal visible
- Both ambient floors at 0 — strips the cosmetic uniform brown so we see
  ONLY what the cascade GI is actually computing
- Default raymarch steps (256)
- GI blur radius **1** (was 8 default — see critic 01 F5 above)
- `--auto-rdoc` warmup 8 s captures all 4 cascades and writes
  `tools/probe_stats_*.json` for spot-checking anyPct
- Sponza auto-enables directional light

**Total captures**: 4 density + 2 blur-radius A/B at winner = **6
captures** (was 4 pre-critic). Runtime **~5-7 minutes** unattended
(critic 01 F9 — was "~2 min", undershoot due to auto-rdoc warmup +
cubic cascade scaling at C0=64).

Optional: capture Cornell-Original at the same 4 probe-res values for a
**data-only** contrast set (critic 01 F3 — dropped the "control"
framing since Cornell's anyPct was never measured, only the Sponza
3.5%). If Cornell shows substantial change too, that argues against
"density only matters for complex geometry" and shifts the conclusion
toward "density matters everywhere."

## What to Measure

For each capture:

1. **Visual inspection** — is light leaking visibly reduced? Are shadowed
   crannies visibly brighter (real GI bounce filling them)?
2. **Probe stats from log** — every frame logs `[4c A/B] meanLum: C0=...
   C1=... C2=... C3=...`. **Critic 01 F6: skip the first 2 lines** —
   they contain NaN/Inf/large-negatives from the codex 09 P0 first-frame
   issue (cascade textures still not zero-initialized; codex 10 plan
   never landed). Take median of meanLum over lines 3-10 for each
   capture. Compare trends across density: if H1, meanLum should grow
   super-linearly with probe-res (more probes find surfaces → more bake
   energy → more downstream GI).
3. **From probe_stats JSON** — `--auto-rdoc` (added per critic 01 F4)
   triggers Phase 12b auto-burst at +8s warmup, which writes
   `tools/probe_stats_<TIMESTAMP>.json` containing per-cascade `anyPct`
   / `surfPct` / `skyPct`. **Per-cascade tracking matters** (kilo
   surfaces this): `--cascade-c0-res` only scales C0 directly; in
   non-co-located mode C1=N/2, C2=N/4, C3=N/8 — so raising C0 from
   32→64 means C1=16→32, C2=8→16, C3=4→8. Upper cascades go from
   tiny to merely small. **If C0 anyPct improves but C1/C2/C3 stay
   near zero**, the bake-time inheritance has nothing useful to read
   from upper cascades, partially confirming H1's "density everywhere"
   reading vs just "C0 density".
4. **Per-pass GPU timing** — `--auto-rdoc` also triggers the Step 12
   pipeline-extract chain, producing
   `tools/analysis/rdoc_frame_frame<N>_pipeline.md` per capture.
   Measures the cost of each density value (C0=64 should land near
   the Step 12 ~10× projection per critic 01 F2).

## Success Criteria — what each outcome implies

### Outcome A: H1 confirmed (density is dominant)

**Visual**: C0=64 capture shows visible color bleed from floor onto
adjacent columns (currently absent), shadowed atrium corners brighter
than at C0=32, fewer "checkerboard" leaking artifacts.

**Critic 01 F7: H1 must beat H4.** "H1 confirmed" requires BOTH:
(a) meanLum increase with density, AND
(b) the C0=64 capture at `--gi-blur-radius=1` looks meaningfully
better than C0=32 at `--gi-blur-radius=1`. If only the blurred
captures show improvement, the win is filter-masking (H4) rather
than real density gain (H1) — Type-A probe-spatial banding is
density-coupled and would otherwise confound the result.

**Implication**: next step is "raise default C0 res to 48 or 64 for
complex scenes" + "add per-scene probe-res selection (Cornell stays at
32, Sponza defaults to 48)". Cost: **~10× cascade bake time at C0=64**
(critic 01 F2 — Step 12 measured C0 32³=4.7 ms → 64³=44.6 ms; whole-
frame cascade work scales similarly).

### Outcome B: Diminishing returns past C0=32 (H1 partial)

**Visual**: C0=32 → C0=48 shows modest improvement; C0=48 → C0=64 shows
little. Density helps but isn't the bottleneck.

**Implication**: density is necessary but not sufficient. Investigate
H2 (SDF resolution) next — would need to add runtime volumeResolution
infrastructure (deferred during Step 12 scaling experiment).

### Outcome C: No change with density (H1 rejected)

**Visual**: All 4 captures look essentially identical.

**Implication**: density is NOT the bottleneck. Pivot to H2 (SDF res)
or H3 (multi-bounce). H3 is architecturally hard; H2 is "raise
volumeResolution to 256 + reallocate textures" which we know how to do.

### Outcome D: Banding artifacts unchanged in pattern (H4)

**Visual**: Even if magnitude grows with density, the same world-space
banding/leak pattern repeats at all densities — meaning the artifact is
algorithmic, not sampling-density.

**Implication**: read cascade merge / interpolation code carefully.
Likely culprits: `sampleUpperDirTrilinear` (Phase 5d), the smoothstep
blend at [radiance_3d.comp:387-393](../../res/shaders/radiance_3d.comp#L387)
(critic 01 F8 — was `:388-389`; the actual `smoothstep` call is at
line 390), or directional-bin merge edge cases.

## Files & Outputs

- Screenshots: `tools/sponza_density_AB_c{16,32,48,64}.png` (Phase 1)
- Plus: `tools/sponza_density_AB_c{winner}_blur8.png` (Phase 2 blur A/B)
- Logs: `tools/app_run_sponza_density_c{16,32,48,64}.log` + the blur-A/B log
- RenderDoc captures: `tools/captures/rdoc_frame_frame<N>.rdc` per data
  point (auto-rdoc trigger)
- Probe stats JSON: `tools/probe_stats_<TIMESTAMP>.json` per data point
  (Phase 12b auto-burst — fires because of `--auto-rdoc`)
- Per-pass GPU timing: `tools/analysis/rdoc_frame_frame<N>_pipeline.md`
  per capture (Step 12 chain)
- Optional Cornell **data-only** contrast (critic 01 F3 — not a control):
  `tools/cornell_density_AB_c{16,32,48,64}.png`

No code changes. This is a measurement-only test using existing CLI
infrastructure (window-size from Step 12; cascade-c0-res from Step 12+;
light-direction / ambient-strength from the lighting controls follow-up;
auto-rdoc + Phase 12b auto-burst pipeline from Step 6b).

## Verification

1. All 6 (or 10 with Cornell contrast) captures land cleanly with
   `--exit-frames` reached
2. Probe-stats meanLum log lines present in each log; **first 2 lines
   skipped** for trend reading per critic 01 F6
3. `tools/probe_stats_<TIMESTAMP>.json` produced per data point (one
   per `--auto-rdoc` warmup-burst)
4. Per-pass GPU timing tables auto-extracted to
   `tools/analysis/rdoc_frame_frame<N>_pipeline.md`
5. Visual comparison clearly differentiates outcomes A/B/C/D
6. Report dumped to
   **`doc/6/claude_plan/sponza_gi_root_cause_hypothesis_test_impl.md`**
   (critic 01 F10 — `_impl.md` suffix matches `doc/5/claude_plan/`
   convention) with annotated A/B images + meanLum trend table +
   anyPct trend table + which hypothesis was confirmed/rejected +
   concrete next-step recommendation

## Out of Scope

- Implementing fixes for whichever hypothesis wins (separate plan)
- Volume resolution scaling (H2 test deferred until H1 outcome known)
- Multi-bounce GI (H3 test requires significant architectural work)
- Per-cascade D resolution sweep (a different scaling axis; could be
  follow-up if H1 shows density helps but with anisotropic bias)
- GPU clock locking for variance reduction (single-shot is fine here
  because we expect 5-10× density effect, well above the ±2-5×
  variance floor identified in Step 12)

---

## Honest expectation note

H1 (density) is most likely to dominate based on the Step 11 codex 09
anyPct=3.5% measurement, but H2 (SDF res) and H3 (multi-bounce) are
both plausible co-contributors. The test will rank them, not eliminate
all but one. Expect "H1 helps significantly; H2 still matters for thin
features; H3 still matters for deep shadows" rather than a single
silver bullet.
