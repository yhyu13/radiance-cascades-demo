# Phase 4a Implementation Review

Reviewed commit: `72e4f01 [Claude] Phase 4a: environment fill toggle + debug stats`  
Progress doc: `doc/cluade_plan/phase4a_progress.md`  
Review date: 2026-04-23T18:08:08+08:00

## Summary

Phase 4a is mostly implemented in the revised, critic-approved shape:
- environment fill is a toggle, default OFF
- sky color is only effective when the toggle is ON
- shader merge continues to sample upper cascade RGB only
- C++ build passes with 0 warnings and 0 errors

The remaining issues are not large enough to reject the commit, but they should be fixed before using the new stats as evidence for Phase 4a behavior.

## Findings

### Medium: `sky* = any% - surf%` is not exact sky coverage, including for C3

Refs:
- `src/demo3d.cpp:1877`
- `src/demo3d.cpp:1878`
- `src/demo3d.cpp:1879`
- `src/demo3d.cpp:1890`
- `doc/cluade_plan/phase4a_progress.md:92`
- `doc/cluade_plan/phase4a_progress.md:93`
- `doc/cluade_plan/phase4a_progress.md:126`
- `res/shaders/radiance_3d.comp:164`
- `res/shaders/radiance_3d.comp:167`
- `res/shaders/radiance_3d.comp:179`

The shader stores only `surfaceFrac` in probe alpha. The UI then computes:

```cpp
float pct  = 100.0f * probeNonZero[ci]    / float(probeTotal);
float surf = 100.0f * probeSurfaceHit[ci] / float(probeTotal);
float sky  = pct - surf;
```

This is not exact sky coverage. `any%` is the size of `surface OR sky`, and `surf%` is the size of `surface`. Therefore `any% - surf%` equals `sky - (surface AND sky)`. Any probe that has both a surface-hit ray and a sky-exit ray is counted as surface and removed from `sky*`, even though it also has sky contribution.

Impact:
- the C3 claim "exact for C3" is mathematically false unless surface-hit probes and sky-hit probes are disjoint
- `sky*` underreports sky exposure for mixed probes
- the stat is still useful as "non-surface-only contribution coverage", but it should not be labeled or documented as sky coverage

Recommended fix:
- rename the displayed stat to something like `nonSurfOnly*` if keeping the current data
- or track `skyHits` explicitly in shader and store/report a real sky fraction
- if both `surfaceFrac` and `skyFrac` are needed, do not overload the single alpha channel with only one of them

### Medium: Runtime shader validation is still missing

Refs:
- `doc/cluade_plan/phase4a_progress.md:149`
- `doc/cluade_plan/phase4a_progress.md:150`
- `doc/cluade_plan/phase4a_progress.md:151`
- `res/shaders/radiance_3d.comp:31`
- `res/shaders/radiance_3d.comp:101`
- `res/shaders/radiance_3d.comp:180`

The C++ build passes, but this does not prove the GLSL change compiles or runs. The progress doc correctly says the main acceptance tests are "Not yet verified at runtime."

Impact:
- the new uniforms and branch logic have not been proven under the app's shader loader
- the reported `any/surf/sky*` UI behavior has not been validated against actual probe readback
- Phase 4a should be described as build-verified, not runtime-verified

Recommended fix:
- run from project root and verify `radiance_3d.comp` loads without shader compilation errors
- capture C3 `any%` and `surf%` with env fill OFF and ON
- verify mode 6 visual delta with env fill ON

### Low: `phase4a_progress.md` conflates raymarch return alpha with stored probe alpha

Refs:
- `doc/cluade_plan/phase4a_progress.md:158`
- `doc/cluade_plan/phase4a_progress.md:159`
- `doc/cluade_plan/phase4a_progress.md:160`
- `res/shaders/radiance_3d.comp:89`
- `res/shaders/radiance_3d.comp:179`
- `res/shaders/radiance_3d.comp:180`

The progress doc says:

```text
The `.a = surfaceFrac` return convention in `raymarchSDF` is already in place.
```

That is inaccurate. `raymarchSDF()` still returns `.a` as a result sentinel (`1.0`, `-1.0`, or `0.0`). Only the final stored probe uses alpha for `surfaceFrac`.

Impact:
- this could mislead the Phase 4c implementation, which will replace surface-hit return alpha with hit distance
- the code is fine, but the handoff note is imprecise

Recommended fix:
- rewrite the note as: "stored probe alpha now contains `surfaceFrac`; `raymarchSDF().a` remains the hit/sky/miss sentinel until 4c changes surface hits to return distance"

## Non-findings

- The default OFF policy matches the revised Phase 4 plan.
- The env-fill dirty sentinel is reasonable: changing sky color while fill is OFF does not invalidate cascades.
- Repurposing stored probe alpha does not appear to affect final raymarch rendering, because current final sampling uses RGB only.
- `MSBuild` passed: `RadianceCascades3D.exe` built with 0 warnings and 0 errors.

## Recommendation

Keep the Phase 4a commit, but do not treat `sky*` as a validated sky metric. Fix the label/math before using the UI readback as acceptance evidence, then perform the runtime checks listed in `phase4a_progress.md`.

