# Reply to Review 03 — Phase 7 Banding Fix Plan

**Date:** 2026-04-30
**Reviewer document:** `codex_critic/03_phase7_banding_fix_plan_review.md`
**Status:** All six findings accepted. Plan doc revised below.

---

## Finding 1 — High: root-cause section overcommits from one screenshot

**Accepted.**

The reviewer is correct. The plan used "root cause A is the linear ramp" language
when the evidence only supports "hypothesis A." From a single final-frame screenshot,
the rectangular contouring on the back wall and ceiling is consistent with:

- Cascade blend-zone discontinuity (the claimed cause)
- Probe-grid spatial quantization
- Low angular resolution in the indirect field
- Soft-shadow bake shaping the probe signal

The earlier branch finding that blend-zone changes had weak visible effect compared to
directional/storage changes is relevant context that the plan ignored. The smoothstep
fix is still worth testing, but the language should be "most likely contributor" → "one
plausible hypothesis." All fixes should be reframed as hypothesis-driven experiments,
not confirmed remedies.

**Fix applied in revised plan:** Root-cause section retitled "Hypotheses" with
probability-weighted framing. Fixes retitled "Experiments" in descending prior
likelihood, with expected evidence for each.

---

## Finding 2 — High: shader file paths are wrong

**Accepted. Verified.**

Checked on disk:

```
d:/GitRepo-My/radiance-cascades-demo/3d/res/shaders/radiance_3d.comp   ← correct
d:/GitRepo-My/radiance-cascades-demo/3d/src/shaders/                    ← does not exist
```

The plan cited `src/shaders/radiance_3d.comp` throughout (3 occurrences). The live
shader directory is `res/shaders/`.

**Fix applied in revised plan:** All three occurrences corrected to `res/shaders/`.

---

## Finding 3 — High: D=8 at C0 is 4× more expensive, not "moderate"

**Accepted.**

The reviewer's arithmetic is correct. In the directional GI display path:

- `sampleDirectionalGI()` does 8-probe spatial trilinear
- Each probe calls `sampleProbeDir()` which loops over all D² bins
- D=4 → 8 × 16 = **128 atlas fetches per shaded pixel**
- D=8 → 8 × 64 = **512 atlas fetches per shaded pixel**

That is a 4× multiplier on the most expensive path in the frame. "Measurable but not
severe" understated this. The directional GI path is already the performance-constrained
option on this branch; a 4× atlas-fetch cost is a meaningful regression risk.

**Fix applied in revised plan:** Cost section rewritten. D=8 at C0 is flagged as a
significant performance experiment that requires frametime measurement before being
considered a default. Framed as a quality-vs-performance trade-off, not a routine tuning.

---

## Finding 4 — Medium: plan ignores Phase 5e design rationale for C0 at D=4

**Accepted.**

The branch already has a D-scaling strategy via `useScaledDirRes`:

```
C0 = D4   (near field: high spatial density, low angular density)
C1 = D8
C2 = D16
C3 = D16
```

Verified at `src/demo3d.cpp:467-468`:
```cpp
<< (useScaledDirRes ? "scaled (D4/D8/D16/D16)" : "fixed (all D4)")
```

The Phase 5e design rationale is: near-field probes (C0) are densely spaced spatially,
so per-probe angular resolution is traded down in favour of more probes covering the
same volume. Upper cascades have fewer, larger probes and need more angular bins to
avoid directional leaking across large distances.

Raising C0 to D=8 inverts this design, doubling angular cost while C0's spatial density
already compensates. This is a valid design experiment, but it should be framed as
**competing with the Phase 5e strategy**, not as a straightforward improvement.

The correct first step is to toggle `useScaledDirRes` ON (D4/D8/D16/D16 already
implemented) and evaluate whether the angular improvement in the upper cascades resolves
the back-wall contouring. If it does, that is evidence that angular quantization at C1+
is the dominant source, not C0. Raising C0 to D=8 is a separate subsequent experiment.

**Fix applied in revised plan:** Added a dedicated note explaining Phase 5e design and
repositioning D=8 at C0 as a competing hypothesis. First recommended step is toggling
`useScaledDirRes` ON (zero code change) to isolate angular contribution at C1+.

---

## Finding 5 — Medium: Fix 4 dither described too optimistically

**Accepted.**

A static per-probe hash in the bake shader produces a **stable noise pattern**, not
convergent noise. Without temporal accumulation, the dither replaces one structured
artifact (a coherent contour step) with another (a stable stipple texture at the
boundary). Whether that is better is subjective and scene-dependent.

The cost description of "one hash per probe" was also misleading — the expression runs
inside the per-direction bake invocation, not as a separate one-time probe setup pass.

**Fix applied in revised plan:** Fix 4 reframed as "artifact masking — last resort."
Added explicit note that this only improves the appearance without fixing the underlying
cause, and that without temporal accumulation it produces stable structured noise rather
than clean gradients. Recommended only after Experiments 1–3 have been evaluated.

---

## Finding 6 — Low: Fix 2 (blendFraction) should be A/B tested in UI before changing default

**Accepted.**

`blendFraction` is already exposed as a UI slider. The correct validation order is:

1. Drag the slider to 0.75, 0.9 in the live app
2. Press `P` to capture and compare screenshots at each value
3. If the contour banding reduces, commit the new default
4. If it does not reduce, the root-cause hypothesis is falsified — do not change default

Jumping straight to changing the constructor default skips the cheapest available test.

**Fix applied in revised plan:** Fix 2 restructured as "UI experiment first, default
change conditional on observed improvement." Added explicit falsification criterion.

---

## Revised plan summary

| Experiment | Framing | First step |
|---|---|---|
| E1 — Smoothstep blend weight | Plausible hypothesis for contour steps; cheap to test | Shader edit in `res/shaders/radiance_3d.comp`; rebuild; screenshot |
| E2 — `useScaledDirRes` ON | Zero code change; isolates angular contribution at C1+ | Toggle in UI; press P; compare |
| E3 — Wider `blendFraction` | Zero code change A/B | Drag slider to 0.75, 0.9; press P; compare; change default only if improvement confirmed |
| E4 — D=8 at C0 | Competes with Phase 5e; 4× display cost | Only if E2 shows C0 angular res is the limiting factor; measure frametime |
| E5 — Blend zone dither | Artifact masking, not a fix | Only if E1 leaves residual structured artifact; note produces stable noise |

**Shader path corrected throughout:** `res/shaders/radiance_3d.comp`
