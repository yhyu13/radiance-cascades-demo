# Phase 4b Plan Review

Reviewed file: `doc/cluade_plan/phase4b_plan.md`  
Review date: 2026-04-24T07:35:54+08:00

## Summary

This plan is materially better than the original Phase 4 draft:
- it includes the missing runtime wiring for `baseRaysPerProbe`
- it correctly avoids `initCascades()` rebuilds on slider changes
- it treats ray-count scaling as a C++/UI change, not a shader feature change

The remaining issues are mostly about plan precision and verification.

## Findings

### High: the `base=32` acceptance case conflicts with the current packed probe-debug encoding

Refs:
- `doc/cluade_plan/phase4b_plan.md:42`
- `doc/cluade_plan/phase4b_plan.md:80`
- `doc/cluade_plan/phase4b_plan.md:101`
- `res/shaders/radiance_3d.comp:180`
- `res/shaders/radiance_3d.comp:181`
- `res/shaders/radiance_3d.comp:182`
- `src/demo3d.cpp:376`
- `src/demo3d.cpp:380`
- `src/demo3d.cpp:381`

The plan allows `baseRaysPerProbe` up to `32`, which implies:
- `C0=32`
- `C1=64`
- `C2=128`
- `C3=256`

Current probe debug packing stores hit counts in alpha as:

```glsl
float packedHits = float(surfaceHits) + float(skyHits) * 255.0;
```

and decodes with:

```cpp
int skyH  = static_cast<int>(packed / 255.0f + 0.5f);
int surfH = static_cast<int>(std::fmod(packed, 255.0f) + 0.5f);
```

This encoding is not safe once a hit count can reach `255` or `256`. That means the plan's highest slider setting can make the very probe stats used in its acceptance criteria become ambiguous or wrong.

Impact:
- the `base=32` validation path is not trustworthy with the current debug encoding
- `surf%` and `sky%` can misreport at the top end of the proposed slider range
- the plan says "no shader changes", but that is only true if the acceptance strategy avoids this range or accepts broken stats

Recommended fix:
- cap the slider so `C3 < 255` hits are possible in all cases, for example `base <= 16`
- or change the debug encoding before using `base=32` as an acceptance target
- explicitly mention this constraint in the plan so verification is honest

### Medium: the file/function target is stale again; `renderMainPanel()` does not exist here

Refs:
- `doc/cluade_plan/phase4b_plan.md:26`
- `doc/cluade_plan/phase4b_plan.md:81`
- `src/demo3d.h:410`
- `src/demo3d.cpp:1763`
- `src/demo3d.cpp:1790`

The plan says to update `renderMainPanel()`, but the current codebase uses `renderSettingsPanel()` for that UI block. The underlying display bug is real, but the plan points at a non-existent function.

Impact:
- small implementation friction
- another sign that the plan was not fully cross-checked against the current branch
- easy place for future docs/code drift if the wrong function name is copied forward

Recommended fix:
- rename that section to `renderSettingsPanel()`
- keep the concrete line-level description of the current hardcoded `cascades[0].raysPerProbe` text

### Medium: “no shader changes” is only true for radiance generation, not necessarily for debug/validation support

Refs:
- `doc/cluade_plan/phase4b_plan.md:26`
- `doc/cluade_plan/phase4b_plan.md:42`
- `doc/cluade_plan/phase4b_plan.md:98`
- `res/shaders/radiance_3d.comp:180`

For the lighting result itself, the plan is right: `uRaysPerProbe` is already wired and scaling the integer count is a CPU-side change. But the plan also uses probe stats as part of validation, and those stats currently depend on shader-side packed alpha encoding.

Impact:
- the plan oversimplifies the scope if it expects trustworthy `surf%` / `sky%` validation at all proposed slider values
- a future implementation could technically satisfy the plan while leaving the top-end debug evidence invalid

Recommended fix:
- narrow the wording to: "no shader changes required for radiance integration"
- then separately note that debug-stat encoding may constrain the slider ceiling unless updated

## Recommendation

Keep the basic 4b direction and runtime wiring exactly as proposed. Update the plan in two places before implementation:

1. Replace `renderMainPanel()` with `renderSettingsPanel()`.
2. Either lower the slider ceiling or acknowledge that `base=32` exceeds the safe range of the current packed hit-count debug path.

With those fixes, the plan is in good shape and aligned with the earlier critic position.

