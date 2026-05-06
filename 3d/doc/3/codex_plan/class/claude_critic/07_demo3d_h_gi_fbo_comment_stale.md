# Finding 7 — Low: Stale `demo3d.h` Comment Says GI FBO is Mode-0-Only

**File:** `src/demo3d.h` (code, not a class doc)
**Severity:** Low
**Status:** Unfixed (in source, not in docs)

---

## What the header says

```cpp
// GI bilateral blur FBO (Phase 9d)
// 3 color attachments: [0]=direct (linear), [1]=gbuffer (normal+depth), [2]=indirect/GI (linear)
// Only active when useGIBlur=true AND raymarchRenderMode==0.
GLuint giFBO;
```

The comment asserts the GI FBO is gated by `raymarchRenderMode==0`.

---

## What the live code says

`demo3d.cpp` contains this UI status string, rendered every frame:

```cpp
ImGui::TextColored(c, "  [9c/9d] Bilateral GI blur  %s  r=%d  (Settings panel, modes 0/3/6)",
    useGIBlur ? "ON" : "OFF", giBlurRadius);
```

The parenthetical `(Settings panel, modes 0/3/6)` explicitly lists **render modes 0, 3, and 6**
as the GI-blur-affected modes. This matches:

- `doc/codex_plan/class/12_current_debug_workflow.md`: "GI blur is enabled by default and
  affects modes 0, 3, and 6."
- `doc/codex_plan/class/00_jargon_index.md` Render mode 3 entry: "If GI blur is enabled,
  this view goes through the same indirect blur/composite path as modes 0 and 6."
- `doc/codex_plan/class/00_jargon_index.md` Render mode 6 entry: "If GI blur is enabled,
  this view is postfiltered too."

The class docs and the live UI agree. The `demo3d.h` comment is stale.

---

## Impact

Low because no class doc is wrong here — doc 12 and the jargon index are correct. The
stale comment only matters to someone reading `demo3d.h` directly. But:

- It directly contradicts correct documentation
- A developer who trusts the header comment over the UI text would add a mode 3/6 bug
  report for a non-bug
- It adds noise to any future code review of the GI blur path

---

## Root cause

The comment was written when GI blur was first introduced and only mode 0 triggered it.
Modes 3 and 6 were added to the GI blur path in a later sub-phase, but the class comment
was not updated.

---

## Recommended fix

In `src/demo3d.h`, update the GI FBO comment block:

```cpp
// GI bilateral blur FBO (Phase 9c/9d)
// 3 color attachments: [0]=direct (linear), [1]=gbuffer (normal+depth), [2]=indirect/GI (linear)
// Active when useGIBlur=true AND raymarchRenderMode is in {0, 3, 6}.
// Render modes 0 (final), 3 (indirect×5), 6 (GI-only) all route through this FBO.
GLuint giFBO;
```

This is a one-line code fix, not a doc fix.
