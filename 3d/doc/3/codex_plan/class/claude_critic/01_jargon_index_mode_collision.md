# Finding 1 — High: Mode Numbering Collision in Jargon Index

**Document:** `00_jargon_index.md`
**Severity:** High
**Status:** Unfixed

---

## What the document says

Under "Debug terms":

> `Mode 3` — Atlas raw view
> `Mode 4` — Hit-type heatmap
> `Mode 5` — Nearest-bin viewer
> `Mode 6` — Bilinear bin viewer
> `Raymarch mode 6` — The final-render GI-only view. It shows indirect light without
> direct lighting. In the current code it respects the directional-GI toggle.

---

## What the live code actually has

There are **two independent mode numbering spaces** in this project:

### 1. `radiance_debug.frag` — `uVisualizeMode`

Controls the atlas debug overlay panel. Values confirmed at `radiance_debug.frag:29-37`:

| Value | Name |
|---|---|
| 0 | Slice (isotropic grid) |
| 1 | MaxProj (isotropic grid) |
| 2 | Average (isotropic grid) |
| 3 | Atlas raw (D×D tile per probe, full directional layout) |
| 4 | HitType heatmap (surf/sky/miss fractions from atlas alpha) |
| 5 | Bin viewer (single direction bin, nearest-bin) |
| 6 | Bilinear bin viewer (same direction, bilinear blend at bin midpoint) |

### 2. `raymarch.frag` — `uRenderMode`

Controls the final image render mode. Values confirmed at `raymarch.frag:67` and call sites:

| Value | Name |
|---|---|
| 0 | Final render (direct + cascade indirect) |
| 1 | Surface normals as RGB |
| 2 | Depth map (distance ray travelled) |
| 3 | Indirect radiance × 5 (isotropic `uRadiance` grid, magnified) |
| 4 | Direct light only (no indirect, shadow applies) |
| 5 | Step-count heatmap |
| 6 | GI-only view (directional or isotropic depending on `uUseDirectionalGI`) |

---

## The conflict

The jargon index defines "Mode 3" = "Atlas raw view" — but in `raymarch.frag`, mode 3 is
"indirect × 5". A reader who sees this jargon index entry and then looks at the final
renderer's `uRenderMode == 3` block will find the description backwards.

Similarly "Mode 4" = "Hit-type heatmap" in the index, but `uRenderMode == 4` is
"direct light only."

The index separately defines "Raymarch mode 6" correctly, but modes 1-5 of `uRenderMode`
are never mentioned — only the overlay `uVisualizeMode` is described (without being named
as such).

---

## Root cause

The jargon index was written using the `radiance_debug.frag` overlay mode numbers for
the "Mode 3/4/5/6" entries, without labelling them as overlay modes. When the jargon
index was extended to mention "Raymarch mode 6" separately, the two mode spaces started
coexisting in the index without disambiguation.

---

## Recommended fix

**Rename the debug entries to make the shader namespace explicit:**

```markdown
## Debug overlay modes (radiance_debug.frag `uVisualizeMode`)

`Overlay mode 0` — Slice view (isotropic grid)
`Overlay mode 3` — Atlas raw (full D×D directional tile per probe)
`Overlay mode 4` — HitType heatmap (surf/sky/miss fractions)
`Overlay mode 5` — Bin viewer (nearest-bin, single direction across all probes)
`Overlay mode 6` — Bilinear bin viewer (compare with mode 5 to see smoothing)

## Final render modes (raymarch.frag `uRenderMode`)

`Render mode 0` — Final image (direct + cascade indirect + tone map)
`Render mode 1` — Surface normals (RGB)
`Render mode 2` — Depth map
`Render mode 3` — Indirect × 5 (isotropic probe grid, no direct light, magnified)
`Render mode 4` — Direct light only (shadow applies; useful baseline before adding indirect)
`Render mode 5` — Step-count heatmap (raymarch performance view)
`Render mode 6` — GI-only (indirect without direct, respects directional-GI toggle)
```

This separates the two mode spaces at the cost of a slightly longer index section, but
removes the ambiguity completely.
