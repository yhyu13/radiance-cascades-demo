# Finding 4 — Low: Doc 06 Debug Views Not Identified by Shader Namespace

**Document:** `06_phase5_directional_atlas.md`
**Severity:** Low
**Status:** Unfixed
**Depends on fix:** Finding 1 (jargon index mode collision)

---

## What the document says

In the "Phase 5 debug views" section:

> Once direction bins exist, new debug views become possible:
>
> - atlas raw
> - hit-type heatmap
> - nearest-bin viewer
> - bilinear viewer
>
> These modes exist so you can ask:
> - does this probe really store different colors by direction?
> - does the merge look up the correct upper-cascade direction?

---

## What is missing

These four debug views are all `radiance_debug.frag` `uVisualizeMode` overlay modes
(modes 3-6). The document does not label them as such, and does not distinguish them
from the `raymarch.frag` `uRenderMode` values.

A reader who reads this section and then goes to `raymarch.frag` looking for "atlas raw"
will not find it there — it lives in `radiance_debug.frag` under `uVisualizeMode == 3`.

Similarly, this section does not mention the `uRenderMode` values that are specifically
useful for Phase 5 validation:
- `uRenderMode = 3` (indirect × 5): the main probe-grid GI isolation view
- `uRenderMode = 6` (GI-only): the directional-GI toggle isolation view (covered in doc 08)

---

## Recommended fix

After fixing finding 1's namespace terminology, update the "Phase 5 debug views" section
to attribute each mode to its shader:

```markdown
## Phase 5 debug views

**Atlas overlay modes** (`radiance_debug.frag`, `uVisualizeMode`):

- Overlay mode 3 (Atlas raw): D×D tile per probe — does this probe really store
  different colors by direction?
- Overlay mode 4 (HitType heatmap): surf/sky/miss fractions from atlas alpha
- Overlay mode 5 (Bin viewer): single direction bin across all probes (nearest-bin)
- Overlay mode 6 (Bilinear viewer): same direction, bilinear blend — does Phase 5f smooth
  the bin boundary?

**Final render modes** (`raymarch.frag`, `uRenderMode`):

- Render mode 3 (Indirect × 5): magnified isotropic probe grid; isolates indirect from direct
- Render mode 6 (GI-only): respects `uUseDirectionalGI` — the primary A/B tool for Phase 5g
```

This anchors readers to the correct shader when cross-referencing the docs with the code.

---

## Severity rationale

Low rather than medium: doc 06 is a conceptual overview, not a debugging reference.
The confusion is more likely to surface when a reader actively tries to use the debug
modes than when reading for understanding. Finding 1 (jargon index) is the root; this
finding is a downstream symptom.
