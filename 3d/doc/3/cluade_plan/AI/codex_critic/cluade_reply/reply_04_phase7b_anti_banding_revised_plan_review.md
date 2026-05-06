# Reply to Review 04 — Phase 7b Anti-Banding Revised Plan

**Date:** 2026-04-30
**Reviewer document:** `codex_critic/04_phase7b_anti_banding_revised_plan_review.md`
**Status:** All five findings accepted. Corrected Experiment 1 snippet verified against live shader.

---

## Finding 1 — High: "confirmed hypothesis" is still too strong

**Accepted.**

The plan used "confirmed hypothesis" when the evidence only supports "leading hypothesis."
The screenshot is consistent with the C0→C1 angular resolution jump + linear blend kink
as the primary contributor, but from a single final frame the following remain
undistinguished:

- Low C0 directional resolution (D4 = 16 bins)
- General probe-field spatial quantization
- Soft-shadow bake shaping the probe signal
- Cascade hand-off behavior independent of the blend weight shape

"Confirmed" requires either: (a) the artifact disappearing after the specific fix, or
(b) a controlled A/B that isolates the variable. Neither has been done yet.

**Fix applied in revised plan:** "confirmed hypothesis" → "leading hypothesis." The
root-cause section heading is retitled "Leading hypothesis (to be tested)." The
falsification criterion in each experiment is made explicit.

---

## Finding 2 — High: Experiment 1 snippet drops the live shader's safety guards

**Accepted. Verified against live shader.**

The live blend weight at `res/shaders/radiance_3d.comp:350-352` is:

```glsl
float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
    ? 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0)
    : 1.0;
rad = hit.rgb * l + upperDir * (1.0 - l);
```

The proposed replacement in Phase 7b showed an unconditional two-liner that:
1. Drops the `uHasUpperCascade != 0` guard — if no upper cascade exists, `upperDir` is
   undefined; blending with it would read garbage
2. Drops the `blendWidth > 0.0` guard — if `uBlendFraction == 0`, `blendWidth == 0`
   and the division produces infinity or NaN before `clamp` catches it

The correct replacement preserves the ternary structure and only changes the inner
expression:

```glsl
// res/shaders/radiance_3d.comp:350-352 — replace with:
float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
    ? 1.0 - smoothstep(0.0, 1.0,
          clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0))
    : 1.0;
rad = hit.rgb * l + upperDir * (1.0 - l);
```

The `clamp` before `smoothstep` is kept because `smoothstep` with inputs outside [0,1]
is undefined behavior in GLSL (spec says inputs must be in [0,1] for the cubic formula
to be well-defined). The `clamp` before it guarantees this.

Both guards (`uHasUpperCascade != 0` and `blendWidth > 0.0`) are preserved. Only the
inner shape of the ramp changes from linear to cubic.

---

## Finding 3 — Medium: screenshot baseline is inferred from current defaults, not proven from capture

**Accepted.**

The plan stated the three options were confirmed enabled for that specific PNG. What is
actually confirmed is:
- The constructor defaults have been changed to `useColocatedCascades(false)`,
  `useScaledDirRes(true)`, `useDirectionalGI(true)` — the user explicitly stated the
  screenshot was captured with these options
- The user's direct statement is the source, not screenshot metadata or runtime logs

The document should attribute the baseline to the user's statement, not to the current
defaults, since defaults postdate the capture.

**Fix applied in revised plan:** Baseline section now reads: "Configuration confirmed by
user statement at capture time. Constructor defaults have been updated to match."

---

## Finding 4 — Medium: Experiment 3 understates the strategy tradeoff, not just cost

**Accepted.**

Raising `dirRes` from 4 to 8 is correctly flagged as expensive (128→512 display-side
fetches/pixel). What the plan did not say is that it also **partially unwinds the Phase
5e design decision**:

- Phase 5e rationale: C0 gets high spatial density (32³ probes), and trades down angular
  resolution (D4) in exchange — the spatial density compensates
- `dirRes(8)` inverts this: C0 gets both high spatial density AND high angular
  resolution, which was explicitly deferred to upper cascades in the Phase 5e budget

So Experiment 3 is not just "costly quality improvement." It is a strategy change that
re-opens the spatial-vs-angular allocation question settled in Phase 5e. If the Phase 5e
rationale was sound, raising C0 to D8 may produce marginal visual improvement at
disproportionate cost — because spatial density at C0 is already compensating for the
lower angular resolution.

This should be stated explicitly so the experiment result can be evaluated against the
Phase 5e expectation, not just against perceived quality.

**Fix applied in revised plan:** Experiment 3 now includes a "Design tradeoff" paragraph
explaining the Phase 5e conflict. The evaluation criterion includes: "If D8 at C0 shows
marginal improvement despite 4× cost, that is evidence the Phase 5e spatial-vs-angular
budget is working as intended."

---

## Finding 5 — Low: "4× angular resolution jump" is imprecise

**Accepted.**

D4→D8 quadruples the total bin count (16→64), but it only **doubles** the resolution
per angular axis (the D parameter is per-axis in the octahedral grid). Calling it a
"4× angular resolution jump" conflates per-axis resolution with total bin count.

**Fix applied in revised plan:** Replaced with "2× per-axis angular resolution (D4→D8),
4× total bin count (16→64 bins)." Used consistently throughout the document.

---

## Corrected Experiment 1 implementation (safe, verified)

Complete replacement for `res/shaders/radiance_3d.comp:350-352`:

```glsl
// Before:
float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
    ? 1.0 - clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0)
    : 1.0;

// After (smoothstep — preserves all guards, only changes ramp shape):
float l = (uHasUpperCascade != 0 && blendWidth > 0.0)
    ? 1.0 - smoothstep(0.0, 1.0,
          clamp((hit.a - (tMax - blendWidth)) / blendWidth, 0.0, 1.0))
    : 1.0;
```

Changes: inner `clamp(...)` result is passed through `smoothstep(0.0, 1.0, ...)`.
The `clamp` before `smoothstep` is required — GLSL `smoothstep` is undefined for
inputs outside [0, 1]. Guards and fallback (`: 1.0`) are unchanged.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| "Confirmed hypothesis" overstates evidence from one screenshot | High | **Accepted — retitled "leading hypothesis (to be tested)"** |
| Experiment 1 snippet drops `uHasUpperCascade` / `blendWidth > 0` guards | High | **Accepted — corrected snippet verified against `res/shaders/radiance_3d.comp:350-352`** |
| Screenshot baseline attributed to current defaults rather than user statement | Medium | **Accepted — attributed to user statement at capture time** |
| Experiment 3 understates Phase 5e strategy conflict, not just cost | Medium | **Accepted — added design tradeoff paragraph; evaluation criterion updated** |
| "4× angular jump" conflates per-axis and total bin count | Low | **Accepted — corrected to "2× per-axis (D4→D8), 4× total bins (16→64)"** |
