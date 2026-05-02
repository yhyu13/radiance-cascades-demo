# Reply to Review 14 — Phase 9 Implementation Detail Review

**Date:** 2026-05-02

All three findings accepted. Replies below.

---

## Finding 1 (Medium): GUI controls vs. debug visualization — accepted

The review is correct. The doc section 4e describes `renderUI()` changes but does not
distinguish between standard panel controls and a debug visualization mode. To be
precise:

- **Phase 9 GUI: yes** — three controls added to the existing settings panel
  (`Temporal accumulation` checkbox, `Temporal alpha` slider, `Probe jitter` checkbox
  with auto-disable)
- **New debug render mode: no** — the existing mode 0–7 range is unchanged; there is
  no mode 8, no history-atlas viewer, no per-probe temporal weight overlay

The practical consequence is that the only way to observe whether temporal accumulation
is working is to watch the rendered output soften over ~30 rebuild cycles. There is no
direct visual confirmation that history buffers contain anything.

Doc will be amended to make this distinction explicit.

---

## Finding 2 (Medium): "What does NOT reset history" is incomplete — accepted

The review is correct that the section overstates inspectability. The current state is:

- **No dedicated "Reset temporal history" button** — the workaround is to change and
  revert a structural parameter (e.g., cascade count slider) which calls `initCascades()`
  and zeros history. This is awkward and not documented in the UI.
- **No history visualization** — there is no way to see what is currently accumulated
  in `probeAtlasHistory` or `probeGridHistory` without attaching a GPU debugger.

The note implied the workflow was manageable ("to manually reset history, change and
revert any structural parameter"). This is accurate as a technical fact but misleadingly
comfortable — a user who wants to A/B test accumulation ON vs. OFF has no clean reset
path.

Future improvement (not currently planned): add a "Reset history" button that calls a
new `clearTemporalHistory()` which zeroes both history textures with
`glClearTexImage()` without tearing down and reallocating the full cascade stack.

---

## Finding 3 (Low): "renderUI()" naming imprecise — accepted

The doc says `renderUI()` is in the files-changed table as if it were a standalone
module. In reality the temporal controls live inside the single contiguous
`renderUI()` settings panel, interleaved with existing Phase 4–8 controls. There is
no separate Phase 9 window or sub-panel. The naming is accurate at the function level
but could mislead a reader into expecting a new dedicated section.

No code change needed — this is a doc precision issue only.

---

## Summary of corrections to apply to phase9_impl_detail.md

1. Section 4e: add a note that the controls are standard settings-panel entries, not a
   new debug visualization mode; explicitly state no new render mode was added.
2. Section 7: revise "What does NOT reset history" to acknowledge the lack of a
   dedicated reset button and the absence of history visualization; add the
   `glClearTexImage()` approach as a future improvement note.
3. Files-changed table: qualify `renderUI()` as "existing settings panel, new controls
   only — no new debug mode or dedicated window."
