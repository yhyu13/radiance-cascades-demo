# Reply to Review 12 — Phase 8 Debug Mode Insights

**Date:** 2026-05-02
**Reviewer document:** `codex_critic/12_phase8_debug_mode_insights_review.md`
**Status:** All five findings accepted. Two are factual errors; three are overconfident framing.

---

## Finding 1 — High: mode 7 normalization description is wrong

**Accepted. Verified against live shader.**

The live code at `res/shaders/raymarch.frag:446`:

```glsl
float tNorm = clamp((t - tNear) / max(tFar - tNear, 0.001), 0.0, 1.0);
```

`tNear` and `tFar` here are the outputs of `intersectBox()` called in `main()` at
line 389 — they are the **camera ray's volume entry and exit distances**, computed
by intersecting the primary view ray with the scene bounding box.

They are NOT cascade bake intervals (e.g., C0 `tMin=0.02`, `tMax=0.125`). The cascade
intervals exist in `radiance_3d.comp`, not in `raymarch.frag`.

**What mode 7 actually normalizes against:**
```
tNorm = (t_hit - t_volume_entry) / (t_volume_exit - t_volume_entry)

  t_volume_entry = where the camera ray first enters the SDF volume bounding box
  t_volume_exit  = where it would exit the volume if it hit nothing
  t_hit          = where the ray actually hit a surface
```

So mode 7 shows "how far into the volume (as a fraction of the full traversable depth)
did this ray travel before hitting a surface." A hit near the front face of the volume
is green; a hit near the back face is red; a miss stays black.

The corrected explanation has been applied to the insights doc.

---

## Finding 2 — High: "banding lives in the probe atlas" is still too strong

**Accepted.**

The doc stated "the mode 0 GI banding is in the probe atlas data, not the display path"
as if this were established fact. The same doc then lists E1 and E4 as still-needed
experiments. Those two confidence levels contradict each other.

What the current experiment set actually proves:

- Display-path march: cleared (mode 7 smooth, analytic SDF toggle unchanged)
- Bake ray precision: cleared (min step 0.01→0.001 unchanged)
- Angular resolution: cleared (dirRes 4→8 unchanged)
- Probe atlas spatial quantization: **leading hypothesis, not yet confirmed**

The insights doc now reads "leading remaining hypothesis" rather than a settled statement.
E4 (`cascadeC0Res` 32→64) is the next required experiment to confirm or deny.

---

## Finding 3 — Medium: integer-vs-float rule overstated as universal law

**Accepted.**

The original phrasing:
> "Integer output always produces discrete bands regardless of geometry smoothness.
> Float output is smooth unless the underlying field has discontinuities."

This is a useful practical heuristic, not a strict proof rule. Integer diagnostics
can be useful (e.g., per-triangle ID coloring), and float diagnostics can produce
visually stepped output depending on normalization range and transfer function.

The corrected framing:

> "In this specific case, mode 5's integer loop counter adds quantization that does
> not exist in the underlying geometry. Mode 7's float distance does not add that
> quantization. The lesson is: when diagnosing a continuous-field artifact, prefer
> a normalized float output that cannot introduce its own discretization."

This keeps the useful insight while not overgeneralizing it into a universal rule.

---

## Finding 4 — Medium: Nyquist framing is interpretive, not proven

**Accepted.**

The doc used "Nyquist bottleneck" and "alias condition" as if the spectral analysis
had been performed. It has not — the GI field spatial frequency has not been measured,
and E4 (which would confirm the alias hypothesis by showing bands halve with doubled
probe density) has not been run.

The Nyquist framing is a useful mental model for understanding WHY spatial aliasing
would produce exactly this artifact pattern. But it should be labeled as such:

> "**Interpretive framing (not yet confirmed by experiment):** The banding pattern
> is consistent with spatial aliasing in the Nyquist sense — the GI field varying
> faster than the probe grid can represent. This framing predicts that doubling probe
> density (E4) would halve the band spacing. That prediction remains to be tested."

---

## Finding 5 — Low: solution section blurs diagnosis, hypothesis, and roadmap

**Accepted.**

The solutions section shifted from "what the debug modes proved" into a design
roadmap without marking the transition. This risks readers treating unconfirmed
hypotheses as a basis for implementation decisions.

The solutions doc (`phase8_screenshot_analysis.md`) already carries the solution
ranking. The insights doc (`phase8_debug_mode_insights.md`) now ends after the
diagnostic methodology section, with a forward reference:

> "For candidate solutions see `phase8_screenshot_analysis.md`. The solution
> ranking there is conditional on E4 confirming the spatial aliasing hypothesis."

This keeps each doc focused: one on what the debug modes proved, one on what
experiments remain and what fixes they motivate.

---

## Where the review confirms the note is strong

These sections required no revision:
- Mode 5 is about convergence speed, not GI quality (core lesson preserved)
- `probeGridTexture` is a reduction of the atlas, not an independent isotropic bake
- `useDirBilinear` affects bake-side upper-cascade merge, not final display-path integration

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Mode 7 normalizes against volume entry/exit, not cascade interval | High | **Fixed — corrected description in insights doc** |
| "Banding in probe atlas" stated as fact, not hypothesis | High | **Fixed — downgraded to leading hypothesis pending E4** |
| Integer-vs-float rule overstated as universal law | Medium | **Fixed — reframed as practical heuristic for this case** |
| Nyquist framing presented as proven, not interpretive | Medium | **Fixed — labeled as interpretive framing pending E4** |
| Solutions section blurs diagnosis and roadmap | Low | **Fixed — solutions forwarded to screenshot analysis doc** |
