# Phase 5g Implementation Learnings Review (Second Pass)

## Verdict

This document is in much better shape than the earlier version. The previous high-severity issue about atlas-missing fallback has been fixed in both the code and the writeup.

I found one remaining stale statement: the note still overstates how the "selected cascade" dropdown relates to mode 6 after the directional-GI fix landed.

## Findings

### 1. Medium: the "selected cascade still applies to mode 6" note is no longer generally true

The document says:

- the selected cascade dropdown still applies to the isotropic path and mode 3/6 debug

Evidence:

- `doc/cluade_plan/phase5g_impl_learnings.md:204-205`

That is only partially true now.

Live behavior is:

- `uRadiance` still binds the user-selected isotropic cascade on texture unit `1`
- mode 3 reads that isotropic texture
- mode 6 reads the isotropic texture only when directional GI is OFF
- mode 6 switches to `sampleDirectionalGI(pos, normal)` when directional GI is ON, which always samples the C0 atlas path instead of the selected isotropic cascade

Evidence:

- `src/demo3d.cpp:1254-1262`
- `res/shaders/raymarch.frag:387-392`
- `res/shaders/raymarch.frag:405-409`

So after the mode-6 fix, the selected-cascade dropdown still matters for:

- isotropic mode 0,
- mode 3,
- and mode 6 only when directional GI is OFF

It does **not** control the directional mode-6 path once `uUseDirectionalGI != 0`.

## Where the document is strong

- The earlier atlas-availability issue is fixed in both code and doc: `uUseDirectionalGI` is now gated by `atlasAvailable`.
- The document correctly records that the final implementation preserved 8-probe trilinear spatial filtering instead of snapping to one probe tile.
- The post-implementation bug-fix section is materially aligned with the current code.

## Bottom line

This is now a mostly trustworthy status note. I would just tighten the selected-cascade paragraph so it distinguishes:

1. isotropic paths, which still use the selected cascade, from
2. directional mode 6, which uses the C0 atlas path and ignores that dropdown.
