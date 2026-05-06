# Reply to Review 12 — Phase 5g Implementation Learnings

**Date:** 2026-04-29
**Reviewer document:** `12_phase5g_impl_learnings_review.md`
**Status:** Both findings accepted. F1 requires a code fix and doc correction. F2 is a
scope gap — acknowledged and documented more explicitly; no code change warranted.

---

## Finding 1 — High: "atlas missing => forced isotropic" invariant was false

**Accepted.** The correctness table claimed:

> if C0 atlas not yet allocated, atlas binding skipped; `uUseDirectionalGI` pushed as
> 0 from `useDirectionalGI=false`.

The first sentence (binding guarded by `cascadeCount > 0 && ... probeAtlasTexture != 0`)
was true. The second was not: `uUseDirectionalGI` was unconditionally pushed from the
UI field:

```cpp
glUniform1i(glGetUniformLocation(prog, "uUseDirectionalGI"), useDirectionalGI ? 1 : 0);
```

So if a user had toggled directional GI ON before the first bake completed, the shader
would enter the directional path with `uDirectionalAtlas` sampler pointing at whatever
texture unit 3 happened to contain — undefined behaviour.

**Fix applied** (`src/demo3d.cpp:1248-1262`):

```cpp
bool atlasAvailable = false;
if (cascadeCount > 0 && cascades[0].active && cascades[0].probeAtlasTexture != 0) {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, cascades[0].probeAtlasTexture);
    // ... push uDirectionalAtlas / uAtlasVolumeSize / uAtlasGridOrigin / uAtlasGridSize / uAtlasDirRes
    atlasAvailable = true;
}
glUniform1i(glGetUniformLocation(prog, "uUseDirectionalGI"),
            (useDirectionalGI && atlasAvailable) ? 1 : 0);
```

The shader now always receives `0` for `uUseDirectionalGI` when the atlas texture is
not allocated, regardless of UI state. The isotropic fallback is now actually enforced
at the C++ level, not just promised in the doc.

**Correctness table in `phase5g_impl_learnings.md` updated:**

| Scenario | Behaviour (corrected) |
|---|---|
| C0 atlas not yet allocated | Binding skipped AND `uUseDirectionalGI` forced to 0 by `atlasAvailable` guard. |
| `uUseDirectionalGI=0` (or forced 0) | `texture(uRadiance, uvw)` — identical to pre-5g. |

---

## Finding 2 — Medium: no directional-GI-only debug view exists

**Accepted.** The document stated mode 6 (GI-only) stays on the isotropic average as
"a comparison reference." That is accurate but understates the gap: once
`uUseDirectionalGI=1`, the only way to see the directional GI contribution in isolation
is through mode 0, where it is mixed with direct light, shadow, tone mapping, and gamma
correction. This makes it difficult to judge whether the directional weighting actually
improved the indirect signal.

**No code change:** adding a new debug mode (e.g., mode 7 — "directional GI only,
linear, no tone map") is the right fix but was already deferred in plan review 11 (F4)
as non-blocking for Phase 5g. It remains a future diagnostic.

**Doc updated** in `phase5g_impl_learnings.md` — the "What This Does Not Do" section
now includes an explicit note:

> No directional-GI-only debug view exists. Mode 6 reads `uRadiance` (isotropic) in
> both ON and OFF states — it cannot be used to validate the directional improvement
> in isolation. Validation of `uUseDirectionalGI=1` requires mode 0, where direct light,
> shadow, and tone mapping are all active. A future debug mode 7 (directional GI only,
> linear) would address this gap.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| "Atlas missing => forced isotropic" invariant was false | High | **Fixed**: `atlasAvailable` flag gates both the binding and the shader push |
| No directional-GI-only debug view | Medium | **Acknowledged**: doc updated; mode 7 deferred as future work |
