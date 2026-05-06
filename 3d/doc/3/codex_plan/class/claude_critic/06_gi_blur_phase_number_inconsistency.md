# Finding 6 — Low: GI Blur Phase Number — "9d" vs "9c/9d"

**Document:** `11_phase6_to_14_latest_systems.md`
**Severity:** Low
**Status:** Unfixed

---

## What the document says

> Phase 9d added GI blur. The final raymarch shader can output direct light, GBuffer, and
> indirect light separately. `gi_blur.frag` blurs only the indirect term, using depth, normal,
> and luminance weights, then composites direct + blurred indirect.

---

## What the code and companion docs say

Three independent sources disagree on the phase number for GI blur:

| Source | Phase label |
|---|---|
| `doc/cluade_plan/class/` (cluade_plan) | Phase 9c throughout |
| `src/demo3d.cpp` load-shader comment | `// Phase 9c: Bilateral GI blur` |
| `src/demo3d.cpp` FBO declaration comment | `// GI bilateral blur FBO (Phase 9d)` |
| `src/demo3d.cpp` live UI status string | `[9c/9d] Bilateral GI blur` |
| `doc/codex_plan/class/11_phase6_to_14_latest_systems.md` | Phase 9d |

The code's own UI text `[9c/9d]` is the most explicit acknowledgment that this feature
spans two phase numbers. The cluade_plan docs call it 9c consistently. Codex doc 11
picks 9d only.

---

## Why this is a problem

A reader who:
1. uses doc 11's "9d" label to search for related code
2. finds the load-shader comment says "9c"
3. looks at the FBO comment that says "9d"
4. looks at the live UI that says "9c/9d"

...cannot determine whether "9c" and "9d" are the same feature, adjacent sub-phases, or
different features. The confusion is compounded by the cluade_plan's consistent "9c" label
making doc 11's "9d" look like an outright error.

---

## Root cause

GI blur was developed across two sub-phases:
- Phase 9c introduced the FBO + `gi_blur.frag` bilateral pass
- Phase 9d likely added the luminance edge-stop (`giBlurLumSigma`, called out in comments
  as `// Phase 13b: stops within-plane tonal blur` — so possibly the phase numbering
  shifted again)

The code reflects this by using both labels. Doc 11 may be treating 9d as the final
canonical label while cluade_plan docs use 9c as the introduction phase.

---

## Recommended fix

Replace "Phase 9d added GI blur" with "Phase 9c/9d added GI blur." This matches the live UI
string exactly and acknowledges the multi-sub-phase development without requiring detailed
sub-phase archaeology.

Alternatively, define the sub-phase split once in doc 11:

> Phase 9c introduced the GI blur FBO and `gi_blur.frag` bilateral pass. Phase 9d refined it
> (adding the luminance edge-stop `giBlurLumSigma`). The code's own UI labels it `[9c/9d]`.

Either approach eliminates the "9d only" phrasing that conflicts with cluade_plan's "9c."

---

## Severity rationale

Low because the feature description is accurate in all other respects — depth/normal/luminance
edge stops, separate direct+indirect outputs, composite path. The phase label is navigational
metadata, not content. A reader encountering the inconsistency is confused, not misinformed.
