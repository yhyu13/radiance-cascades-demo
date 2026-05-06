# Finding 2 — Medium: Jargon Index Missing `uRenderMode` Values 1-5

**Document:** `00_jargon_index.md`
**Severity:** Medium
**Status:** Unfixed

---

## What the document says

The jargon index defines "Raymarch mode 6" in the Debug terms section:

> `Raymarch mode 6` — The final-render GI-only view. It shows indirect light without
> direct lighting. In the current code it respects the directional-GI toggle.

It does **not** define any other `uRenderMode` value (0-5).

---

## What is missing

Once finding 1 is fixed (mode namespace disambiguation), the render modes 1-5 should also
be defined. These are actively used during debugging and are referenced in the
implementation learnings without a central definition:

| `uRenderMode` | What it shows | Why useful |
|---|---|---|
| 1 | Surface normals | Verify SDF normal estimation |
| 2 | Depth (ray travel distance) | Verify raymarch convergence |
| 3 | Indirect × 5 | Inspect cascade indirect without direct; always reads isotropic grid |
| 4 | Direct light only | Verify shadow ray in isolation; no indirect |
| 5 | Step-count heatmap | Performance: see where the SDF march is expensive |

Mode 3 is referenced in multiple Phase 5 docs (e.g. `phase5g_impl_learnings.md`:
"Mode 3 continues to read the isotropic probe grid"). Mode 4 is referenced in
`phase5h_impl_learnings.md` and `phase5i_impl_learnings.md` as the isolation debug mode
for the shadow ray. Both are important diagnostic entry points.

---

## Recommended fix

After fixing finding 1's mode namespace, add a complete "Final render modes" table
(see finding 1's recommended fix section for the full proposed text).

---

## Impact

Without this, the jargon index implies "Raymarch mode 6" is the only render mode of
interest, causing readers to overlook mode 3 (indirect isolation) and mode 4
(direct-only isolation), which are the two most useful Phase 5 diagnostic modes.
