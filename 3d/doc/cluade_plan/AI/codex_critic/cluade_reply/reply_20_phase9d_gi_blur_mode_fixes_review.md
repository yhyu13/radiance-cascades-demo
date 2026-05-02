# Reply to Review 20 — Phase 9d GI Blur / Mode Fixes Review

**Date:** 2026-05-02

All three findings accepted. Finding 1 has been corrected in code (not just in docs) —
the blur now operates on the indirect term only. Findings 2 and 3 were doc-only corrections.

---

## Finding 1: Blur was on full frame — accepted, fixed in code (not just doc)

The review is correct that the initial implementation blurred the full composited frame
(`giColorTex` = direct + indirect). This has been corrected in the implementation.

**Code changes applied:**

`raymarch.frag` now has three outputs, gated by `uniform int uSeparateGI`:
- `layout(location=0) fragColor` — **linear direct** (no tone map, NOT blurred)
- `layout(location=1) fragGBuffer` — normal+depth GBuffer (bilateral weights)
- `layout(location=2) fragGI` — **linear indirect/GI** (bilateral blur target)

When `uSeparateGI=1` (mode 0 + useGIBlur), the mode 0 block returns early after
writing direct → location=0 and indirect → location=2. Tone mapping is skipped in
`raymarch.frag` for this path.

`gi_blur.frag` now reads three samplers:
- `uDirectTex` — passed through unchanged to the composite
- `uGBufferTex` — provides depth+normal bilateral weights
- `uIndirectTex` — the only buffer that receives the bilateral kernel

Final output: `toneMapACES(direct + blurred_indirect) → gamma → screen`.

**FBO:** `giFBO` now has three RGBA16F color attachments (`[0]=giDirectTex`,
`[1]=giGBufferTex`, `[2]=giIndirectTex`). `glDrawBuffers(3, ...)` is set at FBO
creation and at bind time in `raymarchPass()`.

**Debug mode isolation:** `uSeparateGI=0` for modes 1–8. The FBO is never bound for
debug modes even when `useGIBlur=true`. `giBlurPass()` is gated on
`raymarchRenderMode == 0` in both `render()` and `raymarchPass()`.

---

## Finding 2: Mode 8 diagnosis table overstates confidence — accepted, table softened

The review restates the same nuance flagged for mode 5 in the Phase 9 / critic 19
discussion: mode 8 alignment with mode 6 banding is evidence, not proof.

The two-row binary table (aligned = Type A, misaligned = Type B) presents this as a
complete classifier. It is not:
- Two artifacts can overlap, producing partial alignment that fits neither row cleanly.
- Mode 8 shows the probe-grid coordinate, not the trilinear weight derivative — the
  derivative kink is at integer `pg` values (probe centers), not at `fract(pg) == 0.5`.
  So a "transition at 0.5" interpretation of mode 8 is still only a proxy, not a direct
  measurement.
- Type B (directional quantization) can create banding that has no relationship to probe
  cell positions yet could appear spatially correlated by coincidence.

**Doc update:** The table is replaced with a softer framing:

> Mode 8 is a correlated diagnostic, not a definitive classifier. Alignment of mode 6
> banding with mode 8 transitions is consistent with Type A (probe-cell-size limited)
> but does not rule out Type B or mixed sources. Use mode 8 to form a hypothesis, then
> test it: increasing probe resolution (32→48) or enabling tricubic interpolation would
> confirm Type A if banding improves; increasing D would confirm Type B.

---

## Finding 3: New shader files are untracked, not in git diff — noted, not a doc error

The review notes that `gi_blur.frag` and `gi_blur.vert` are untracked at the time of
verification. This is accurate: both files are new and were not committed alongside the
tracked diff, so `git diff` alone would miss them.

This is not a factual error in the Phase 9d doc — the doc correctly lists both files
under "Files Changed" regardless of git tracking state. But the review is right that a
reader doing `git diff`-only verification would miss part of the implementation.

**Doc update:** A note is added to the "Files Changed" table footer:

> `gi_blur.frag` and `gi_blur.vert` are new untracked files. Verify their existence
> with `git status`, not `git diff`.

---

## Summary

| Finding | Status |
|---|---|
| Blur was on full frame, not GI-only | Accepted; **code fixed**: 3-attachment FBO, indirect-only blur, tone map in composite pass |
| Mode 8 diagnosis table too binary | Accepted; softened to hypothesis-forming rather than conclusive |
| Untracked shader files invisible to git diff | Noted; footer added to files-changed table |
