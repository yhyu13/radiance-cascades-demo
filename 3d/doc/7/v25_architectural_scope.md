# v2.5 — Architectural axis scoping

**Date:** 2026-05-26.
**Predecessor:** [v24b_indirect_clamp_impl.md](v24b_indirect_clamp_impl.md) — DEAD verdict on output-side luminance clamp. Symptom-clamp axis closed (v2.4 bake-bin DEAD + v2.4.b output clamp DEAD).
**User direction:** "go b and then c" — proceed to v2.5 architectural regardless of v2.4.b outcome.

## What we now know

After v2.2 (merge formula reshape, DEAD), v2.3 (leak attribution, MARGINAL), v2.4 (C0 dirRes bump, DEAD), v2.4.b (output luminance clamp, DEAD), the leak has the following characterized shape:

- **Bias, not variance.** Bright pixels are *consistently* bright frame-over-frame at N=2048 (high sample count; MC noise long gone). More rays/probes won't help.
- **Broad, not localized.** ~11% of mask pixels carry the tail. Geometrically concentrated on the green-wall side (cell_x=23 holds 41%) but spread *uniformly within* that region (v2.3 Lorenz: top-5%=26.1%, Gini 0.57). No hot probes.
- **Not addressable post-hoc.** Output clamps drag bulk pixels down faster than fireflies. The bright pixels' indirect/direct ratio overlaps with non-bright pixels — there's no salience signal at consume time.
- **Not addressable by bake-bin resolution.** D=8→16 moved |p95| by 0.1%; the bias survives finer angular binning.
- **Not addressable by merge-formula reshape.** Upper-cascade luminance is uncorrelated with bright-tail luminance — no aFactor reshape can isolate them.

These four DEADs corner the diagnosis: the bias is structural in *how the cascade radiance integral is constructed at bake time*, not in how it is merged, discretized, or post-processed.

## Candidate architectural axes

Three axes survive the elimination. Listed with cost, information content, and pre-committed posture.

### Axis A — Per-cascade contribution isolation (diagnostic-first)

**Premise:** the leak is currently observed on the *blended* cascade output (C0 trilinear-sampled, with C1+ folded in at bake time). We don't know which cascade level *introduces* the bias. Capture mode 17 EXRs per cascade level (cascade-only-C0, cascade-only-C0+C1, ..., cascade-only-C0..Cn) at cornell/cam0/N=2048 and measure `bright%` per level.

**Mechanism gained:**
- If `bright%` is flat at C0 and grows monotonically with each cascade added → leak source is in *cascade merge* (upper cascade contributes overestimate).
- If `bright%` is already high at C0-only → leak source is in *C0 bake itself* (direct radiance integration, not merging).
- If `bright%` is non-monotone → multi-source; needs further attribution.

**Cost:** ~1.5 h (uniform `uMaxCascadeLevel`, gate in merge.comp, capture script loop, analyzer extension). No DEAD-pivot risk — diagnostic always returns information.

**Pre-committed bands:**
- **CLEAR ATTRIBUTION:** any single cascade level accounts for ≥70% of bright% growth → that level becomes the v2.5 fix target.
- **MULTI-SOURCE:** growth spread across ≥2 levels with no dominant → v2.5 needs a different axis (B or C below); revisit.

### Axis B — Bake-side cone integral audit (read-only first, fix if found)

**Premise:** the v2.0 fix was on the *consumer* (`sampleProbeDir`: `irrad = (4/D²) × Σ L cos⁺`). The *producer* — bake shaders that fill `uDirectionalAtlas` — was not audited under the v2.0 contract. Candidate bugs that produce bright-bias:
- Double cosine weighting (bake includes cos⁺, consumer also applies cos⁺ → bias high in the integral)
- Solid-angle factor present at bake (should be 4π/D² baked, not at consume)
- Missing visibility weighting at the radiance source (ray hits emitter directly with no occlusion check)
- Multi-bounce gain compounding above unity over temporal accumulation

**Cost:** ~30 min audit (read bake.comp / merge.comp / probe sampling shaders); 0–2 h fix if a bug is found.

**Pre-committed posture:**
- **BUG FOUND:** report mechanism + landed fix in a separate impl doc. Re-run full N=2048 sweep, gate against v2.0-postfix baseline using v2.4.b's bands (|p95| ≥10% drop = MARGINAL, ≥30% = STRONG).
- **NO BUG:** axis returns negative result; document the audit conclusions (so future sessions don't repeat); proceed to Axis C.

### Axis C — Probe ray-basis jitter (alignment-bias hypothesis)

**Premise:** `binToDir(ivec2 dx, dy, D)` deterministically maps bin index → world direction. Every probe sees the green wall via the *same* bin indices. If the green wall's bright reflection lands inside a particular bin for many probes simultaneously, the deterministic basis creates correlated overestimation across probes (the green wall is hit by "the same ray" everywhere).

**Test:** per-probe rotation of the direction basis (frame-stable per-probe quaternion; cosmetic angle, no resampling cost) at bake time, with consumer rotated to match.

**Cost:** ~2 h (basis rotation in bake + consumer; per-probe RNG seed plumbing already exists for jitter; verify cos⁺ still computed against actual rotated direction).

**Pre-committed bands** (gate at cornell/cam0/N=2048):
- **STRONG:** bright% drops ≥3 pp, ratio shift ≤0.05, |p95| drops ≥20%.
- **MARGINAL:** bright% drops 1–3 pp, ratio shift ≤0.10, |p95| drops 10–20%.
- **DEAD:** otherwise — alignment hypothesis falsified; the bias is not basis-aligned. Pivot to v2.6.

## Recommended order

**A → B → (C only if A or B produce no actionable signal)**.

- **A first** because it's a pure diagnostic. It conditions the choice between B and C without committing to a hypothesis. The capture cost is small (one extra mode + a CLI flag); the analyzer extends in <50 lines.
- **B second** only if A points at C0 itself (i.e. C0-only already shows the bias). The audit is read-only and cheap; if A points at *merge* levels rather than at C0, B is less valuable and C is more relevant.
- **C last and only on demand.** Alignment-bias is a real risk in deterministic-basis probe schemes, but it's a specific hypothesis with a non-trivial implementation. Don't commit to it without A's evidence.

## Stopping criterion for v2.x

If A, B, and C all return DEAD/no-bug/no-attribution: the v2.x bright-tail leak is judged *fundamental to the chosen cascade representation* at the cornell/cam0 measurement configuration. Default ships as v2.x terminus; the hybrid retirement goal is met within ±2.3% ratio and ±5% |p95| (already true today). Further quality work moves to v3.0 (different cascade representation entirely, e.g. surfel/restir, or accept the current bound).

## Execution log

(append-only, populated as axes are run)

### 2026-05-26 — Axis A run (per-cascade contribution isolation)

- Wired `maxCascadeLevel` field + `setMaxCascadeLevel` + CLI `--max-cascade-level=N`. Bake loop caps to `min(N, cascadeCount-1)`; top-baked level forced `uHasUpperCascade=0` so stale higher atlases don't leak in.
- Build clean (Release).
- Sweep: `tools/v25_axisA/capture_per_level.ps1` → L ∈ {0,1,2,3} at cornell/cam0/MB-ON g=1.0/hybrid-OFF/N=2048/mode-17. Total wall ≈ 3.5 min.
- Analyzer: `tools/v25_axisA/analyze_per_level.py` → `v25A_results.json`.

**Per-level metrics:**

| Level   | ratio | \|p50\| | \|p95\| | dim%  | bright% |
|---------|-------|---------|---------|-------|---------|
| C0 only | 0.255 | 0.827   | 0.933   | 92.4  |  2.36   |
| +C1     | 0.745 | 0.371   | 0.828   | 20.0  |  5.70   |
| +C2     | **0.994** | 0.262 | 0.898 |  5.6 | **13.49** |
| +C3     | 0.977 | 0.255   | 0.883   |  5.1  | 11.09   |

**Per-transition Δ bright%:**

| Transition | Δ bright% | share of total growth (+8.73 pp) |
|------------|-----------|---|
| C0→+C1     | +3.34 pp  | +38.2% |
| **+C1→+C2** | **+7.80 pp** | **+89.3%** |
| +C2→+C3    | −2.40 pp  | −27.5% |

**Verdict: CLEAR_ATTRIBUTION at the C1→C2 merge.**

- C2 alone contributes +89.3% of the entire bright% growth between C0-only and the full chain.
- The full chain (+C3) actually *reduces* bright% by 2.40 pp from the C0+C1+C2 peak — C3's smoothstep into C2's overshoot acts as a soft attenuator, but does NOT cancel the source.
- |p95| pattern matches: it peaks at L=2 (0.898) and gets pulled slightly back by C3 (0.883) — same direction as the bright%.
- ratio at L=2 (0.994) is the only level *above* PT — C2 is the over-contributor; C3's −0.017 ratio correction is undershoot relative to the C2 peak.

**Mechanism interpretation:** C2's interval bracket is 4× C1's (and 16× C0's). When that long-range radiance enters the merge, it over-contributes — most likely because the merge formula at the C1↔C2 boundary doesn't correctly normalize for C2's larger cone solid angle, OR C2's ray budget produces high-magnitude bin samples that the smoothstep merge weights too generously. Adding C3 partially attenuates because the C2↔C3 merge applies the *same* over-weighted formula in reverse on C3's contribution, masking the C2 peak.

**Why C3 < C2 (not strictly monotone):** the cascade integral is not a simple sum — each level's contribution is gated by smoothstep visibility and tMin/tMax brackets relative to the level *above* it. When C3 is present, the C2↔C3 merge re-distributes C2's bracket coverage, shifting some C2 radiance into C3-flagged territory. The net effect is that C3's presence dampens C2's peak (−2.40 pp) but doesn't eliminate it (+5.39 pp residual from C1).

**Axis A action triggered:** the v2.5 fix target is the C1→C2 cascade-merge step. Open hypotheses, ranked by probability:

1. **Cone solid-angle normalization at the merge:** the `4/D²` factor is per-cascade D; if C2's D differs from C1's (D-scaling on), the merge may not rescale correctly. Verify in the bake shader.
2. **tMin/tMax bracket overlap:** C2's tMin should be exactly C1's tMax. If there's overlap (even one cell width), radiance is double-counted at the boundary band.
3. **MB temporal feedback amplification:** with g=1.0 and C2 contributing peak +7.80 pp per cycle, the multi-bounce accumulator may compound C2's overshoot frame-over-frame. Test with `--multi-bounce-gain=0` (single-bounce) and see if the C2 peak shrinks proportionally.
4. **Phase 5e D-scaling interaction:** if dirRes scales per cascade, the per-cone solid angle changes too, which the bake formula must mirror.

**Recommended next step (within v2.5):** instead of pivoting to Axis B/C, run a focused **Axis A.1 diagnostic**: re-capture the per-level sweep at `--multi-bounce-gain=0` (single-bounce). This isolates whether the C2 overshoot is steady-state geometry of the merge (still +7.80 pp at MB-OFF) or temporal compounding (peak collapses at MB-OFF). Outcome conditions the C2 mechanism hypothesis: geometric → look at solid-angle normalization in bake shader; temporal → look at multi-bounce feedback for upper cascades.
