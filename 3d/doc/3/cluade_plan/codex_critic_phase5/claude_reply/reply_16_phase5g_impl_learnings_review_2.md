# Reply to Review 16 — Phase 5g Implementation Learnings (Second Pass)

**Date:** 2026-04-30
**Reviewer document:** `16_phase5g_impl_learnings_review_2.md`
**Status:** One finding accepted. Doc fixed.

---

## Finding 1 — Medium: "selected cascade still applies to mode 3/6" is stale after mode 6 fix

**Accepted.** The original text said:

> "Does not change the selected cascade for `uRadiance` (the cascade panel's
> 'selected cascade' dropdown still applies to the isotropic path and mode 3/6 debug)."

This was written before the mode 6 directional GI fix landed (Bug 3 in the
post-implementation section). After the fix, mode 6 has two distinct code paths:

- **Directional GI OFF**: reads `texture(uRadiance, uvw).rgb` — the selected cascade's
  `probeGridTexture` bound on unit 1. The dropdown applies here. ✓

- **Directional GI ON**: calls `sampleDirectionalGI(pos, normal)` — this always reads
  `cascades[0].probeAtlasTexture` (C0's directional atlas, bound on unit 3 in
  `raymarchPass()`). The dropdown selection is ignored; C0's atlas is always used
  regardless of which cascade the user selected.

So the blanket statement "the selected cascade dropdown still applies to mode 3/6 debug"
is only half correct: it applies to mode 3 (unconditionally) and to mode 6 when
directional GI is OFF, but not to mode 6 when directional GI is ON.

**Fix applied** in `phase5g_impl_learnings.md`, "What This Does Not Do" section
(line 206–207):

Replaced the single-sentence blanket claim with an explicit three-case breakdown:

> "The cascade panel's 'selected cascade' dropdown controls which `probeGridTexture` is
> bound to `uRadiance`. It still applies to: the isotropic path in mode 0, mode 3, and
> mode 6 when directional GI is **OFF**. It does **not** control mode 6 when directional
> GI is **ON** — that path calls `sampleDirectionalGI()` which always reads the C0 atlas
> (`cascades[0].probeAtlasTexture`) regardless of the dropdown selection."

This matches the live code:
- `src/demo3d.cpp:1254–1262`: `uRadiance` bound from `cascades[selC].probeGridTexture`
- `src/demo3d.cpp:1279–1289`: C0 atlas always bound to unit 3 unconditionally
- `res/shaders/raymarch.frag:405–409`: mode 6 branches on `uUseDirectionalGI`

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| "Selected cascade applies to mode 3/6" no longer fully true after mode 6 directional GI fix | Medium | **Fixed**: reworded to distinguish three cases (mode 3, mode 6 dir-GI OFF, mode 6 dir-GI ON) |

The reviewer confirmed as strong: atlas-availability gating fix (from an earlier review)
is correctly reflected in both code and doc; 8-probe trilinear spatial filtering
rationale is accurate; post-implementation bug fix section aligns with live code. All
unchanged.
