# Reply to Review 09 — Commit b3b04ef Phase 5d Trilinear

**Date:** 2026-04-29
**Reviewer document:** `09_b3b04ef_phase5d_commit_review.md`
**Status:** All five findings accepted. F4 is an actionable code fix applied immediately.
F1/F2/F5 are positive confirmations. F3 is a scope clarification already documented in
the plan and impl learnings.

---

## Finding 1 — High: main Phase 5d spatial-merge problem is fixed

**Accepted.** The reviewer independently confirms that the structural fix is correct:
- `sampleUpperDirTrilinear()` with 8-corner `sampleUpperDir()` delegation
- `triP000`/`triF` hoisted above the direction loop
- non-co-located + directional + trilinear-ON routing in the direction loop

No action needed. This is the primary goal of the commit.

---

## Finding 2 — High: border bug fixed correctly

**Accepted.** The clamp-before-floor/fract pattern at `radiance_3d.comp:275-285`:

```glsl
vec3 upperGridClamped = clamp(upperGrid, vec3(0.0), vec3(uUpperVolumeSize - ivec3(1)));
triP000 = ivec3(floor(upperGridClamped));
triF    = fract(upperGridClamped);
```

This mirrors Phase 5f's `octScaled = clamp(dirToOct(dir)*D - 0.5, 0, D-1)` exactly.
The reviewer confirms the fix is structurally correct. No action needed.

---

## Finding 3 — Medium: not full ShaderToy weighted merge parity

**Accepted.** This was explicitly scoped in both the plan and impl learnings docs:

> "This implements the **spatial interpolation half** of ShaderToy's `WeightedSample()`.
> Per-corner visibility weighting is deferred — not claimed."

The project goal for this commit was "fix the blocky nearest-parent Phase 5d merge",
not "achieve full ShaderToy weighted parity". The reviewer agrees the commit achieves
its stated goal. Per-corner visibility weighting remains a known open item.

No action needed beyond keeping the distinction in docs.

---

## Finding 4 — Medium: stale co-located checkbox HelpMarker

**Accepted.** The old text:

```
"OFF (ShaderToy-style, probe-resolution halving only; no spatial interpolation merge):
...
Phase 5d visibility check is implemented but currently inert:
distToUpper ~= 0.108m < tMin_upper = 0.125m, so visHit < distToUpper*0.9 never fires."
```

Both statements are now wrong:
- "no spatial interpolation merge" — false since `useSpatialTrilinear` defaults true
- "visibility check inert" — the check was removed in this commit

**Fix applied** (`src/demo3d.cpp`):

```
"OFF (ShaderToy-style, probe-resolution halving):
C0=32^3  C1=16^3  C2=8^3  C3=4^3. Same world volume; upper probes ~0.108m away.
Directional merge reads upper atlas with 8-neighbor spatial trilinear (Phase 5d
trilinear, default ON -- see checkbox below). Spatial trilinear OFF falls back to
nearest-parent (probePos/2), which is blocky but cheaper.
Toggling destroys and rebuilds all cascade textures."
```

The new text accurately describes post-commit behaviour and points the user to the
dedicated trilinear checkbox below for details.

---

## Finding 5 — Low: build verification confirmed

**Accepted.** Reviewer confirmed 0 errors, 0 warnings via MSBuild on HEAD. This is
stronger than diff-only verification used in some earlier reviews. No action needed.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| F1: main Phase 5d fix correct | High | Confirmed — no change |
| F2: border bug fix correct | High | Confirmed — no change |
| F3: not full ShaderToy weighted parity | Medium | Scope already documented; no change |
| F4: stale co-located HelpMarker | Medium | **Fixed** in `src/demo3d.cpp` HelpMarker |
| F5: build verified 0 errors | Low | Confirmed — no change |

The only code change is the HelpMarker text. No logic, no uniforms, no shader changes.
