# v2.0 P2-D — P2 dominant-bin readout under MB-ON

**Status:** Second of the B → D → A long-term sequence ([v20_p2b_dsweep_impl.md](v20_p2b_dsweep_impl.md)
was step B). Asks: does multi-bounce temporal feedback REDISTRIBUTE per-bin
atlas content enough to close cam2's dx-collapse, or is the asymmetry
preserved (or amplified) under MB?

**Verdict: `MB_NO_CLASSIFICATION_EFFECT`** — per-row weighted JS at
D=8 MB-ON = **0.1534**, EXACTLY identical to MB-OFF D=8 baseline 0.1534 (Δ = 0.0000).
The per-pixel dominant-bin histogram is L1-identical between MB-OFF and
MB-ON despite the raw EXR bytes differing (sub-bin floating-point
modulation from MB feedback, but no pixel's argmax-bin shifts).

**Architectural implication: MB feedback uniformly scales per-bin
atlas energies; it does NOT reshuffle WHICH bin dominates per pixel.** MB
is not a cure for the bake-side per-bin atlas content asymmetry. Option A
(bake-side fix prototype) is required and will run under MB-ON.

**Date:** 2026-05-24

## 1. Pre-committed bands ([p2_dombin_mbon.ps1](../../tools/v20_arch_diagnostic/p2_dombin_mbon.ps1) header)

| band | predicate (per-row JS, MB-ON, D=8) | architectural read |
|---|---|---|
| `MB_CURES`      | ≤ 0.05 | MB redistributes per-bin atlas content enough to drop per-row asymmetry below B step's DISSOLVE threshold. MB-ON is the architectural fix. |
| `MB_REDUCES`    | (0.05, 0.10] | MB closes >25% of the asymmetry; bake-side fix gets less leverage. |
| `MB_PRESERVES`  | (0.10, 0.16] | MB has little effect on per-bin distribution; cam2 still collapses. |
| `MB_AMPLIFIES`  | > 0.16 | MB increases asymmetry; temporal feedback consolidating cam2's narrow bins by re-injecting their content. |

Apples-to-apples baseline is **D=8 MB-OFF per-row JS = 0.1534** (the engine
default and the value reported in the B step §4 D-sweep table). The original
P2 doc's "D=4 baseline 0.1310" was an artifact of the pre-fix loose `infer_D`
tolerance reading D=8 data as D=4 — see B step §3 for the bug analysis.

## 2. Setup

### Capture

Per [p2_dombin_mbon.ps1](../../tools/v20_arch_diagnostic/p2_dombin_mbon.ps1):

- `cornell-orig-alcove`, cam0 + cam2 from
  [tools/v20_pre_measurement/cameras.json](../../tools/v20_pre_measurement/cameras.json)
- `--use-multi-bounce=1 --multi-bounce-gain=1.0`
- `--blend-mode=0` (default smoothstep), `--noise-seed-offset=0`,
  `--use-hybrid=0`, `--cascade-dir-res` unset (engine default D=8)
- `--render-mode=22` (mode 22 dominant-bin viz, nearest-parent atlas readout)
- `--screenshot-exr=1 --exit-frames=256`
- **bug-234 mitigation**: `--use-probe-jitter=1` explicitly set.
  Default measurement-camera mode pins jitter to zero → no rebake trigger
  → MB feedback never fires after the seeding frame. Forcing jitter ON
  triggers per-frame cascade rebakes so MB temporal feedback accumulates
  into the atlas across the 256-frame capture window.

### Sanity check (MB-ON is actually firing)

To rule out a bug-234-class silent no-op, a mode-17 (GI-only) MB-OFF vs
MB-ON pair was captured at cam0 with the same `--use-probe-jitter=1` flag.
md5sums differ (`61928067…` vs `7b8d17ce…`) — MB IS firing. This rules out
"MB never ran" as the explanation for the mode-22 L1-identical histogram.

### Analyzer

Re-used the B step's
[analyze_p2_dombin.py](../../tools/v20_arch_diagnostic/analyze_p2_dombin.py)
with the FP-tight `infer_D` fix and `per_row_metrics()` extension; no
analyzer changes for this step.

## 3. Results

| metric | MB-OFF (D=8) | MB-ON (D=8) | Δ |
|---|---:|---:|---:|
| histogram overlap            | 0.6293 | 0.6293 | **+0.0000** |
| 2D JS divergence             | 0.1568 | 0.1568 | **+0.0000** |
| per-row weighted overlap     | 0.6329 | 0.6329 | **+0.0000** |
| per-row weighted JS          | 0.1534 | 0.1534 | **+0.0000** |
| cam0 mean dominance          | 0.119  | 0.119  | +0.000 |
| cam2 mean dominance          | 0.116  | 0.116  | +0.000 |
| cam0 valid GI pixels         | 138332 | 138332 | 0 |
| cam2 valid GI pixels         | 116390 | 116390 | 0 |
| cam0 top-1 bin               | (3, 3) 0.285 | (3, 3) 0.285 | identical |
| cam2 top-1 bin               | (0, 3) 0.510 | (0, 3) 0.510 | identical |
| **cam0 histogram L1 diff**   | — | — | **0.0 / 138332** |
| **cam2 histogram L1 diff**   | — | — | **0.0 / 116390** |

The dominant-bin histogram (count of pixels classified into each of D×D=64
bins) is bit-identical between MB-OFF and MB-ON. The per-bin shares match
to all reported decimal places.

Sub-bin EXR differences exist (cam0 EXR md5 `f1850391…` MB-OFF vs
`377098b2…` MB-ON) — confirming MB feedback IS modulating per-bin atlas
energy values — but the modulation preserves the argmax-over-bins ordering
at every surface pixel.

## 4. Verdict + interpretation

**`MB_NO_CLASSIFICATION_EFFECT`**: per-row JS Δ = 0.0000. Lands at the
exact `MB_PRESERVES` band boundary but goes further than "preserves"
— the per-pixel dominant-bin classification is *exactly* invariant.

### Why MB doesn't shift dominance

The MB shader path
([radiance_3d.comp:555-557](../../res/shaders/radiance_3d.comp#L555))
adds `albedo × stochastic_atlas_sample × multiBounceGain` to the cascade
bake-time ray's accumulated color. The temporal-feedback contribution is
sampled from the previous frame's C0 atlas via
`sampleC0AtlasStochastic(pos, n, frameSeed)` — a SINGLE stochastic atlas
read per bake ray, not a per-bin lookup.

So MB adds a `(roughly-uniform-across-bins)` energy term to each bin's
final stored value. Two implications:

1. **No directional preference**: MB feedback doesn't differentially brighten
   bins by direction; it adds the same temporally-smoothed indirect term
   to every bin's contribution.
2. **Argmax preserved under uniform additive bias**: if bin A had value `a`
   and bin B had value `b` with `a > b` pre-MB, then `(a + δ) > (b + δ)`
   post-MB for any δ ≥ 0. The argmax bin doesn't change.

The asymmetry between cam0's fanned distribution `[28%, 25%, 18%, 8%, …]`
across dy=3 and cam2's collapsed `[51%, 16%, 6%, 4%, …]` is therefore
bake-side and single-bounce in origin. MB's temporal accumulation can
brighten the absolute radiance level (the +136% cam0 ratio lift measured
in [hdr_relitigation_impl.md §4.2](hdr_relitigation_impl.md)) but cannot
reshape the per-bin dominance distribution.

### Combined B+D conclusion

| step | knob | result | rules out |
|---|---|---|---|
| B    | D ∈ {8, 16}            | per-row JS 0.153→0.176 SHARPEN | "raise D as fix" |
| D    | useMultiBounce ∈ {0, 1} | per-row JS 0.153→0.153 NO EFFECT | "MB as cure" |

Both cheap-fix candidates eliminated. **The cascade per-direction-bin
atlas content asymmetry between cam0 and cam2 is a bake-side
single-bounce property — neither discretization nor temporal feedback
moves the distribution.** Architectural fix candidates narrow to three
bake-side targets that all act on bin CONTENT at gather time:

- **(a) bin-coverage hardening**: per-bin minimum sample count with
  multi-sample fallback when the bin's first ray hits an SDF occluder
  within `[tMin, tMax]`. Direct attack on cam2's dx=0 collapse (the
  collapse is "alcove probe samples direction X, ray hits wall, no
  contribution; alternative ray to slightly-perturbed direction X' might
  miss the wall and gather radiance"). ~2-4h engine work.
- **(b) per-bin HIGH-only firefly clamp at bake** ([[feedback_asymmetric_filters]]):
  not directly aimed at the collapse but defensible as general bake-side
  robustness — would also reduce extreme single-bin spikes. ~1-2h.
- **(c) direction-aware probe placement**: shift probes near alcove
  geometry to sample more of the missing azimuthal range. Largest
  engineering cost; most architectural. ~4-8h.

Per the long-term plan, Option A is the next phase after this D-step
checkpoint. User sign-off requested before proceeding.

## 5. Self-critique

**Strengths:**

- bug-234 mitigation (`--use-probe-jitter=1`) applied proactively per
  cerebrum DNR §204; sanity check (mode 17 PNG md5 comparison) confirmed
  MB IS firing before drawing the architectural conclusion. Without the
  sanity check, the L1-identical histogram would have been ambiguous
  between "MB has no effect on classification" and "MB silently no-op'd."
- Pre-committed bands with a clean `MB_NO_CLASSIFICATION_EFFECT` outcome
  (Δ=0.0000) — a sharper finding than the band thresholds anticipated.
  The bands were designed expecting MB to move per-row JS by some small
  amount; the actual exact-zero is itself the headline.
- Comparison baseline is now D=8 MB-OFF (the engine default), aligning
  the verdict to apples-to-apples with the dominant-bin readout running
  at engine-default D. The B step's "D=4 baseline" framing was an
  artifact of the pre-fix `infer_D` bug.

**Weaknesses:**

- Only tested at D=8 (engine default) and MB-gain=1.0. A two-axis sweep
  over MB-gain ∈ {0.5, 1.0, 2.0} × D ∈ {8, 16} could surface whether
  higher gain (or differently-quantized atlas) breaks the argmax-invariance.
  At gain=2.0 the MB term is large enough to potentially exceed the
  margin between competing bins; could shift argmax. Defensible to defer
  because the asymmetric-filter rule [[feedback_asymmetric_filters]]
  caps the realistic gain range.
- The "MB additive term is uniform across bins" interpretation in §4 is
  informed by reading the MB shader path, but not directly measured.
  A diagnostic that prints per-bin MB-delta values (atlas_with_mb -
  atlas_without_mb, per bin) would verify the uniformity claim — but the
  observation that no pixel's argmax shifts is already a strong
  consequence of uniformity.
- Histogram L1 diff = 0.0 is a categorical "no change in classification
  at all" — stronger than any of the pre-committed bands described. The
  pre-committed bands were calibrated for "small movement, classify the
  direction" rather than "exact-zero, accept the null." A future similar
  test should include a `*_EXACT` band at Δ=0.0 to capture this case
  cleanly. (Soft rule continuation of [[from §218]] "include a quantitative
  tie-breaker, not a dichotomy.")

## 6. Cross-reference

- Parent (P2 baseline): [v20_p2_dombin_impl.md](v20_p2_dombin_impl.md)
- Parent (B step): [v20_p2b_dsweep_impl.md](v20_p2b_dsweep_impl.md)
- Capture: [tools/v20_arch_diagnostic/p2_dombin_mbon.ps1](../../tools/v20_arch_diagnostic/p2_dombin_mbon.ps1)
- Analyzer (unchanged from B): [tools/v20_arch_diagnostic/analyze_p2_dombin.py](../../tools/v20_arch_diagnostic/analyze_p2_dombin.py)
- Results: `tools/v20_arch_diagnostic/captures_p2_dombin_mbon/p2_dombin_mbon_results.json`
- Sanity check: `tools/v20_arch_diagnostic/captures_p2_dombin_mbon/mb_sanity/`
- MB shader path: [res/shaders/radiance_3d.comp:555-557](../../res/shaders/radiance_3d.comp#L555)
- bug-234 precedent (jitter pinning in measurement mode): cerebrum DNR §204
- MB-fires-in-measurement-mode precedent: cerebrum DNR §210 (hdr_relitigation
  measured MB +136% brightness lift on cornell-orig)
- **Next planned step: surface combined B+D findings to user before Option A**
