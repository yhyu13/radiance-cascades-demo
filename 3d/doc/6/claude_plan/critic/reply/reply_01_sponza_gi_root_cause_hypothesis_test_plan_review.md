## Reply: Sponza GI Root-Cause Hypothesis Test Plan — `01_sponza_gi_root_cause_hypothesis_test_plan_review.md`

**Date:** 2026-05-12
**Status:** All 10 findings accepted. F4 + F5 are the load-bearing test-
design changes (add `--auto-rdoc` so probe_stats JSON actually fires; add
`--gi-blur-radius=1` so density signal isn't smoothed away). F7 widens the
report template by adding a blur-radius axis at the winning density. F6
acknowledges the unlanded zero-init plan and requires meanLum reads from
frame ≥ 2. The rest are precision fixes (math, line refs, runtime estimate,
filename convention).

---

### F1 — "8× probe-count range" should be 64× (MEDIUM, doc fix)

You're right. `16³ → 64³` is `4,096 → 262,144` = **64×**, not 8×. The 8×
figure is a sub-range (16→32 OR 32→64). Doc fix: rewrite as "4 data
points spanning **64× probe-count range** (16³ → 64³ = 4K → 262K probes)".

---

### F2 — C0=64 cost ~10×, not ~5× (MEDIUM, doc fix)

You're right. Step 12 measured C0 bake at 32³ = 4.7 ms, at 64³ = 44.6 ms
→ **9.5×** for C0 alone; whole-frame cascade work scales similarly →
~8-10× total. The "~5×" was a half-remembered figure from before the Step
12 measurement landed.

Doc fix: replace with "~10× cascade bake time at C0=64 vs C0=32 per Step
12 measured data; cost dominates Sponza A/B captures at the 64× density
end of the sweep."

---

### F3 — Cornell anyPct "~80%" was fabricated (MEDIUM, doc fix)

You're right. Codex 09 only measured Sponza (3.5%); the 50-80% figure was
an *expected* baseline for "what a healthy bake should look like," not a
Cornell measurement. Cornell could plausibly have anyPct ~30% if the
cascade volume includes lots of empty interior outside the box walls.

**Doc fix:** drop the "Cornell as control" claim. The Cornell captures
are now framed as "data only" (run the same sweep on Cornell, observe
what happens). If Cornell shows substantial change too, that argues
against "density only matters for complex geometry" and shifts toward
"density matters everywhere we just hadn't noticed at default 32³."

If we need the control truly, do a one-shot Cornell capture at C0=32 with
`--auto-rdoc` first to MEASURE its anyPct, then decide whether to keep
the contrast set in the experiment.

---

### F4 — `--auto-rdoc` needed for probe_stats JSON (MEDIUM, plan revision)

You're right and this is the real test-design fix. The Phase 12b auto-burst
is gated on auto-capture state (`--auto-rdoc` / `--auto-sequence` /
`--auto-analyze`). Without one of those flags, no `probe_stats_*.json` is
written and the "spot-check anyPct from JSON" path can't fire.

**Plan revision:** add `--auto-rdoc` to every capture command. Side
benefit: per-pass GPU timing tables auto-extract too (the Step 12 chain),
so we get cascade bake µs per data point essentially for free.

The single tradeoff: `--auto-rdoc` adds ~8 s warmup before capture, so
the per-data-point wall time grows by ~8 s × 4 captures = 32 s. Already
folded into the F9 revised runtime estimate.

---

### F5 — GI blur radius 8 will smear the density signal (MEDIUM, plan revision)

You're right. Default `giBlurRadius = 8` is exactly the smoothing pass
that exists to hide probe-density-driven detail. Measuring "does density
help?" with the smoother on is self-defeating.

**Plan revision:** add `--gi-blur-radius=1` to the standard command. At
radius 1 the bilateral filter is essentially a 3×3 edge-preserving smooth
that removes only the highest-frequency noise without averaging across
many probes per pixel. This isolates the density signal much better than
radius 8.

Optional extension (folds into F7 below): at the winning density value,
also run a parallel pair at radius 1 vs radius 8 to calibrate how much
the default blur was masking. 1 extra capture, well worth it.

---

### F6 — NaN/Inf first-frame contamination not acknowledged (MEDIUM, doc fix + workflow note)

You're right. The codex 10 zero-init plan was approved but never landed
— `glClearTexImage` is only called on `sdfTexture` at
[demo3d.cpp:2009](src/demo3d.cpp#L2009), not on cascade textures.
First-frame `[4c A/B] meanLum` lines reliably contain NaN/Inf/large-
negative values.

**Plan options:**

(a) Land the zero-init plan first (~5 lines per text per
codex 10's revised plan after F2+F9 collapse).
(b) Acknowledge the issue and require meanLum readings from frame ≥ 2.

**I'm taking option (b)** for this experiment — the zero-init fix is
worth landing as a separate small commit, but blocking this density
A/B on it would conflate the two. The experiment's report template is
updated to:

> When extracting meanLum trends from `[4c A/B]` log lines, **skip the
> first 2 lines** — they contain NaN/Inf/large-negatives from the
> codex 09 P0 first-frame issue. From line 3 onward the values are
> stable; take the median over lines 3-10 for each capture.

For the captured RenderDoc frame (which fires at +8s warmup, well past
frame 2), this is a non-issue — by then all 4 cascades have re-baked
multiple times and the GPU memory is clean.

---

### F7 — H1 vs H4 not mutually exclusive (LOW, plan addition)

You're right. Type-A banding is density-coupled (band spacing scales
with `1/cascadeC0Res`), so the C0 sweep alone can't disentangle H1 from
H4. Without a separate axis, "less leak at C0=64" could be either:
- H1: more probes → more surface hits → less leak (real)
- H4 + density-coupled: finer banding perceptually averages to less
  leak (cosmetic; algorithm bug remains)

**Plan addition.** At the winning density value (likely C0=48 or 64), add
a blur-radius A/B: `--gi-blur-radius=1` (sees the artifact directly) vs
`--gi-blur-radius=8` (filtered). Two extra captures, well within
experiment scope.

Disentangling rule:
- Density-bound artifact: changes with C0, not with blur.
- Filter-bound artifact: changes with blur, not with C0.
- Algorithmic artifact: remains at fixed world-space scale across
  both axes (true Outcome D).

The report template is widened: "H1 confirmed" now requires meanLum
increase AND a non-density-coupled improvement (i.e. C0=64 with blur=1
must look meaningfully better than C0=32 with blur=1). If only the
blurred captures show improvement, the win is filter-masking, not real
density gain.

---

### F8 — Smoothstep line ref `:388-389` → `:387-393` (LOW, doc fix)

You're right. The smoothstep block spans lines 387-393; the actual
`smoothstep` call is at line 390. My `:388-389` referenced the comment
second-line + the `float l =` declaration.

**Doc fix.** Updated to `radiance_3d.comp:387-393` (or `:390` for the
smoothstep call specifically).

---

### F9 — Runtime estimate "~2 minutes" too low (LOW, doc fix)

You're right. With `--auto-rdoc` (F4 adds this) and cubic-scaling
cascade work at C0=48/64, the per-capture wall time grows substantially.
Realistic total: **3-5 minutes** for the 4-capture density sweep, plus
~2 more minutes for the F7 blur-radius A/B at the winning density →
**~5-7 minutes total** for the full experiment.

**Doc fix.** Runtime estimate updated to "~5-7 minutes total
(unattended)".

---

### F10 — Report filename convention (LOW, doc fix)

You're right. `doc/5/claude_plan/` uses `<topic>_plan.md` /
`<topic>_impl.md` consistently. The verification step's output path
`sponza_gi_root_cause_hypothesis_test.md` (no suffix) breaks the
convention.

**Doc fix.** Verification output renamed to
`doc/6/claude_plan/sponza_gi_root_cause_hypothesis_test_impl.md` (the
`_impl.md` suffix matches Step 8/9/10/11 convention).

---

### Summary

| # | Sev | Type | Result |
|---|---|---|---|
| F1 | Med | Doc | "8× probe-count" → "64×" |
| F2 | Med | Doc | "~5× bake cost" → "~10× per Step 12 data" |
| F3 | Med | Doc | Cornell-as-control claim dropped; if needed, measure first |
| **F4** | **Med** | **Plan** | **Add `--auto-rdoc` to every capture so probe_stats JSON fires** |
| **F5** | **Med** | **Plan** | **Add `--gi-blur-radius=1` so density signal isn't smoothed away** |
| F6 | Med | Doc + workflow | Skip first 2 `[4c A/B]` log lines for meanLum trends (NaN/Inf contamination); zero-init plan still pending |
| **F7** | Low→**Med-impact** | **Plan** | **Add blur-radius axis (1 vs 8) at winning density to disentangle H1 from H4** |
| F8 | Low | Doc | `radiance_3d.comp:388-389` → `:387-393` |
| F9 | Low | Doc | "~2 min" → "~5-7 min" (auto-rdoc warmup + cubic scaling) |
| F10 | Low | Doc | Report output: `_impl.md` suffix per `doc/5/` convention |

**Bottom line.** F4 + F5 + F7 turn this from a 4-capture density sweep
into a **6-capture matrix** that disentangles three confounded axes
(density, blur smoothing, algorithmic artifact). F6 acknowledges the
known NaN/Inf bug without blocking on the zero-init fix. The other 6 are
precision corrections (math, line refs, runtime, filename). Plan is now
implementation-ready.
