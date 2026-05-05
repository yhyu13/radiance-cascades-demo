# Finding 5 — High: "Fused Atlas EMA" Described as Implemented but Does Not Exist

**Documents:** `11_phase6_to_14_latest_systems.md`, `00_jargon_index.md`
**Severity:** High
**Status:** Unfixed

---

## What the documents say

### `11_phase6_to_14_latest_systems.md` — Phase 10 section

> Phase 10 also added fused atlas EMA. When active, the bake shader blends atlas history while
> writing the fresh atlas, then C++ swaps texture handles. That removes the separate atlas
> temporal-blend dispatch in the common path.

### `00_jargon_index.md` — Temporal and quality terms section

> `Fused atlas EMA`
> Phase 10 path where `radiance_3d.comp` blends the fresh atlas into atlas history during the
> bake, avoiding a separate atlas `temporal_blend.comp` dispatch.

Both documents describe fused atlas EMA as a real Phase 10 implementation — a concrete
code path that changes the dispatch structure.

---

## What the live code actually has

Comprehensive grep of `src/demo3d.h`, `src/demo3d.cpp`, and `res/shaders/` for any
fused-EMA-related identifier returns **zero matches**:

```
grep: fusedAtlas    → no matches
grep: fused_atlas   → no matches
grep: fuseAtlas     → no matches
grep: useFused      → no matches
```

`Demo3D::Demo3D()` unconditionally loads `temporal_blend.comp` as a separate shader. There
is no boolean field in `demo3d.h` named `useFusedAtlasEMA` or any equivalent. There is no
code path that conditionally skips `temporal_blend.comp` in favor of a fused bake.

The `probeAtlasHistory` texture **exists** (allocated in `RadianceCascade3D`), which shows
the data structure for fused EMA was prepared. But the compute shader path that would blend
directly into that history during the bake pass was never added, or was removed before the
current HEAD.

---

## Why this matters

Doc 11 says fused EMA "removes the separate atlas temporal-blend dispatch in the common
path" — implying the default codepath already skips `temporal_blend.comp` for the atlas.
This is false. Every frame that updates a cascade still dispatches `temporal_blend.comp`
for the atlas.

A reader who:
1. reads doc 11 and believes fused EMA is the current path
2. then looks for a `useFusedAtlasEMA` toggle in `demo3d.h` to disable it for debugging
3. finds nothing and concludes the docs are unreliable

...will lose trust in the entire doc set, which is otherwise accurate.

The jargon index entry reinforces the problem by defining "fused atlas EMA" without any
hedge ("proposed", "planned", "not yet active") — treating it as a current vocabulary term
on equal footing with real implemented features like "History clamp" and "GI blur."

---

## Root cause

Doc 11 was likely written when fused EMA was a planned Phase 10 design. The design details
(`radiance_3d.comp` blends during bake; C++ swaps handles) are architecturally coherent, so
they read as if they were already implemented. The jargon index entry was added in the same
pass without a "planned" qualifier.

---

## Recommended fixes

**Option A — Mark as unimplemented (preferred)**

In doc 11, change:

> Phase 10 also added fused atlas EMA.

to:

> Phase 10 also proposed fused atlas EMA as a design direction. In this approach, `radiance_3d.comp`
> would blend atlas history during the bake pass, then C++ swaps texture handles — removing the
> separate atlas `temporal_blend.comp` dispatch. This design was not implemented in the current
> HEAD; `temporal_blend.comp` still runs as a separate pass.

In the jargon index, add a clarifying sentence:

> `Fused atlas EMA`
> Proposed (not yet implemented) Phase 10 path where `radiance_3d.comp` blends the fresh atlas
> into atlas history during the bake, avoiding a separate atlas `temporal_blend.comp` dispatch.
> The `probeAtlasHistory` texture exists for this purpose, but the bake-time blend path has not
> been added to `radiance_3d.comp` as of HEAD.

**Option B — Remove from jargon index**

If fused EMA is unlikely to be revisited, removing its jargon entry entirely prevents
future confusion. Doc 11 can briefly note the idea was explored but not implemented.

---

## Verification method

```sh
grep -r "fusedAtlas\|fused_atlas\|fuseAtlas\|useFused" src/ res/shaders/
# Expected: zero matches in current codebase
```

Also check `demo3d.h` for any field controlling whether `temporal_blend.comp` is skipped
for the atlas path — none exists as of HEAD.
