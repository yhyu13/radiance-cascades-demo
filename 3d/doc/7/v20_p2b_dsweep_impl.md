# v2.0 P2-B — Per-row JS metric + D-sweep on P2 (mode 22)

**Status:** Sharpening pass on the P2 finding ([v20_p2_dombin_impl.md](v20_p2_dombin_impl.md)).
Adds per-row-conditioned JS divergence + overlap to the analyzer (follow-up
W1 from §7), then runs P2 at D ∈ {4, 8, 16} on cornell-orig-alcove to
discriminate "asymmetry is a D=4 discretization artifact" vs "asymmetry is
genuine bake-side atlas content."

**Verdict: `P2_DSWEEP_SHARPEN`** — per-row JS at D=16 = **0.1757**, above the
0.15 SHARPEN threshold. Raising direction resolution does NOT cure the
asymmetry; it exposes more of it. **Architectural implication: bake-side
fix must target bin CONTENT, not bin COUNT.** D-as-a-knob is dead for this
problem.

**Date:** 2026-05-24

## 1. Pre-committed bands ([p2_dombin_dsweep.ps1](../../tools/v20_arch_diagnostic/p2_dombin_dsweep.ps1) header)

| band | predicate (per-row JS at D=16) | architectural read |
|---|---|---|
| `P2_DSWEEP_DISSOLVE` | < 0.05 (~2.6× reduction from D=4) | asymmetry is D=4 quantization; ship "raise D" as the fix |
| `P2_DSWEEP_PERSIST`  | [0.05, 0.15] (within 60% of D=4) | genuine atlas content, but no leverage from D | 
| `P2_DSWEEP_SHARPEN`  | > 0.15 (greater than D=4 baseline) | asymmetry intensifies; D was masking it |

## 2. Analyzer extension (per-row JS + overlap)

`per_row_metrics(h0, h2, D)` in
[analyze_p2_dombin.py](../../tools/v20_arch_diagnostic/analyze_p2_dombin.py):

- For each `dy ∈ [0..D-1]`, isolate `row_dx_dist0 = h0[dy, :] / sum(h0[dy,:])` and
  `row_dx_dist2 = h2[dy, :] / sum(h2[dy,:])`.
- Compute `row_overlap = Σ_dx min(p0[dx], p2[dx])` and `row_js = JS(h0[dy,:], h2[dy,:])`.
- Mass-weighted average uses `weights[dy] = (share0[dy] + share2[dy]) / 2`
  (so rows that one cam ignores but the other concentrates on still
  contribute proportionally to their joint mass).

**Why this matters even at D=4 (analyzer self-correction)**: my prior framing
in [v20_p2_dombin_impl.md §5](v20_p2_dombin_impl.md) claimed the per-row
JS would "read MUCH sharper" than the headline 0.66 overlap. **It does
not.** At D=4 per-row weighted JS = **0.1310** vs headline 2D JS = **0.1327**
— essentially identical. Within the dominant dy=1 row itself, row_js = 0.1445
and row_overlap = 0.6460 (also ~equal to the headline). The dx-collapse is
real at the top-1 level (cam2 54.9% vs cam0 31.4%), but the full per-dx
distribution within dy=1 is `cam0=[27, 31, 9, 19]` vs `cam2=[55, 20, 8, 0]`
— these share substantial mass at dx=0, 1, 2; the asymmetry concentrates at
dx=3 (19% vs 0%) and at the "single-bin dominance" shape at dx=0.

The top-1 share is a sharper headline than mean overlap, but the FULL
distribution comparison was overstated as "MUCH sharper" — the right framing
is "comparable; the asymmetry is in the shape (collapse vs fan), not in the
overlap measure." See cerebrum DNR 2026-05-24 §2 on this self-correction.

## 3. infer_D bug found and fixed

Initial D-sweep capture run produced D=8 and D=16 EXRs that the analyzer
WRONGLY classified as D=4. Root cause: `infer_D`'s tolerance was
`err < 0.5/D` — for D=4 this is 0.125, which is wide enough that ANY R
value in [0,1] gets within 0.125 of some D=4 center {0.125, 0.375, 0.625,
0.875}. So D=4 false-positived on truly-D=16 data.

Fix: check that every observed R value is within FP tolerance (5e-4) of
one of the D allowed centers. The smallest D for which this holds is the
correct answer. Bug-class: pre-committed verdict ran on misclassified data;
caught by noticing mean dominance halved at "D=16" (consistent with shader
loop running at D=16) but recovered dx/dy ∈ [0..3] (consistent with
encoding being D=4). **Cerebrum DNR pattern**: when a 2-channel encoding
embeds the quantization base, the inferrer's tolerance must be FP-tight,
not bin-width-tight; bin-width tolerance is silently subsumed by the
coarsest legal D.

## 4. D-sweep results

Capture: cornell-orig-alcove, cam0+cam2, M0, MB-OFF, b=2, ST=0 default,
256 frames. Engine flag `--cascade-dir-res=N` (already shipped at
[src/main3d.cpp:660](../src/main3d.cpp#L660); no engine work). 0.5 min total.

| D  | hd overlap | hd JS  | row-w overlap | row-w JS | cam2 top-1 share | cam2 dy-row collapse |
|---:|-----------:|-------:|--------------:|---------:|-----------------:|---------------------:|
| 4  | 0.6617     | 0.1327 | 0.6676        | 0.1310   | 0.549 @ (0, 1)   | dx=[55, 20, 8, 0]    |
| 8  | 0.6293     | 0.1568 | 0.6329        | 0.1534   | 0.510 @ (0, 3)   | dx=[51, 16, 6, ...]  |
| 16 | 0.6192     | 0.1794 | 0.6221        | 0.1757   | 0.483 @ (0, 7)   | dx=[48, 15, 5, ...]  |

All three D values land at the same **shape** — cam2 collapses near dx=0
on the dy=D/2-1 row (the octahedral upper-hemisphere band): dy=1 at D=4,
dy=3 at D=8, dy=7 at D=16. Consistent with the encoding's geometry.

**The numeric metrics all walk in the SHARPEN direction monotonically:**

- Headline overlap: 0.662 → 0.629 → 0.619 (lower = more asymmetric, −6.3%)
- Headline JS: 0.133 → 0.157 → 0.179 (higher = more asymmetric, **+35%**)
- Per-row weighted overlap: 0.668 → 0.633 → 0.622 (−7%)
- Per-row weighted JS: 0.131 → 0.153 → **0.176** (**+34%, lands above SHARPEN threshold**)

cam2 top-1 share walks DOWN with D (0.549 → 0.510 → 0.483) because more
bins spread the top-bin share thinner — but the **collapse PATTERN** is
preserved: cam2 still concentrates ~half its dominant-bin mass near dx=0
on the dominant row at every D. cam0 still fans across multiple dx values.

## 5. Verdict + interpretation

**`P2_DSWEEP_SHARPEN`**: per-row JS at D=16 (0.1757) is 11.8pt above the
0.15 SHARPEN threshold and 34% larger than the D=4 baseline.

**Architectural reading:**

1. **Discretization is innocent.** D=4 is not artifically creating the cam2
   collapse via coarse binning. At D=16 the collapse is sharper. Therefore
   the candidate "raise D as default fix" (a 1-line change) is **off the
   table**.

2. **Bake-side bin CONTENT is the source.** cam2's surface samples genuinely
   receive radiance from a narrower set of azimuthal directions than cam0's,
   and this property is robust to direction-resolution. The narrowness is
   in the radiance distribution per probe, not in the binning of that
   distribution.

3. **Why does D=16 sharpen rather than equalize?** Hypothesis: cascade ray
   count per probe is D² (16, 64, 256). Each bin's energy estimate is
   integrated by ~`D²/D = D` rays on the dominant row band. At D=16 with
   D=16 rays per row-band, cam2's narrow-azimuthal probes hit fewer
   occluder-free directions per bin sample, so each bin's content reflects
   its actual radiance more faithfully — including the asymmetry. At D=4
   the 4 rays per row-band averaged across more solid angle each, soft-
   averaging the asymmetry into a slightly more uniform per-bin
   distribution.

4. **The fix candidate space narrows to three.** All three target bin
   content, not bin count:
   - **(a) bin-coverage hardening**: per-bin minimum sample count (multi-
     sample fallback when a bin's first ray hits an occluder within
     [tMin, tMax]). Direct attack on the dx=0 collapse.
   - **(b) per-bin firefly clamp at bake** (HIGH-only per the
     `feedback_asymmetric_filters` rule): would reduce extreme single-bin
     spikes; less directly aimed at cam2's collapse but defensible as
     general bake-side robustness.
   - **(c) direction-aware probe placement**: shift probes near alcove
     geometry to sample more of the missing azimuthal range. Largest
     engineering cost; most architectural.

## 6. Self-critique

**Strengths:**

- Pre-committed bands in [p2_dombin_dsweep.ps1](../../tools/v20_arch_diagnostic/p2_dombin_dsweep.ps1)
  forced the right interpretation. Without them, "metrics walk monotonically
  in the asymmetric direction" would have read as "interesting trend" not
  "verdict-confirmed sharpening." The pre-committed numbers prevent the
  cherry-pick of D=4-vs-D=16 framing.
- The infer_D bug surfaced via the "mean dominance halved but bins still
  in 0..3" cross-check (catches half-fired pipeline changes — bug-class
  similar to bug-230 / bug-234 RNG seed wiring). Added the bin-count check
  to the analyzer for future direction-encoded readouts.
- D-sweep is genuinely cheap (0.5 min capture + 1 min analyze) and the
  verdict has architectural blast radius (kills an entire fix candidate).
  Validation-infrastructure-pays-for-itself rule precedent ([from
  cerebrum 2026-05-19](../../.wolf/cerebrum.md)).

**Weaknesses:**

- Self-corrected an over-statement from the prior P2 doc. "Per-row JS
  would read MUCH sharper" was wrong — at D=4 the per-row and headline
  metrics are essentially equal. The actual lever the per-row analysis
  provides is at D=8/16 where rows partition the encoding finer; at D=4
  with only 4 rows total it adds little. Caveat for future analyzers:
  per-row metrics need D ≥ 8 to materially differ from the headline.
- Only tested even D values that are powers of 2 (4, 8, 16); didn't test
  intermediate (6, 10, 12). The shape conclusion (cam2 collapse pattern
  preserved) is robust to this, but a future fully-rigorous sweep should
  fill in D=8 and D=12 too.
- The "why does D=16 sharpen?" hypothesis (per-bin ray count at higher D)
  is informed speculation, not a measurement. Direct measurement would
  require instrumenting the cascade bake to record per-bin sample counts,
  which is moderate engineering. Defensible to defer until a fix candidate
  needs the data.

## 7. Cross-reference

- Parent: [v20_p2_dombin_impl.md](v20_p2_dombin_impl.md) (initial mode 22
  measurement + P2_OVERLAP_MEDIUM verdict)
- Capture: [tools/v20_arch_diagnostic/p2_dombin_dsweep.ps1](../../tools/v20_arch_diagnostic/p2_dombin_dsweep.ps1)
- Analyzer: [tools/v20_arch_diagnostic/analyze_p2_dombin.py](../../tools/v20_arch_diagnostic/analyze_p2_dombin.py)
  (per-row JS + infer_D FP-tight fix added this session)
- Results: `tools/v20_arch_diagnostic/captures_p2_dombin_dsweep/p2_dombin_D{8,16}_results.json`
- Engine: `--cascade-dir-res=N` already shipped ([src/main3d.cpp:660](../src/main3d.cpp#L660))
- Next planned step: P2 with MB-ON (the second-cheapest cure rule-out
  per the long-term B→D→A recommendation)
