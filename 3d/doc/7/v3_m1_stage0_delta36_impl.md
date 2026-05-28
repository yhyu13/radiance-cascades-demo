# M1 Stage 0 Implementation - Delta #3/#6 A/B Switches

**Date:** 2026-05-27.
**Plan:** `doc/7/v3_m1_stage0_delta36_plan.md`.
**Status:** Implementation-prep slice complete. A/B switches compile and smoke-run; full N=2048 matrix not run yet.

## Implemented

| File | Change |
|------|--------|
| `res/shaders/radiance_3d.comp` | Added `uM1Delta3GatedTrilinear` and `uM1Delta6GeometricCone`. Delta #3 flag consumes `sampleUpperDirWeighted().rgb` directly and bypasses scalar `aFactor`; default path unchanged. |
| `src/demo3d.h` | Added setters and state for `m1Delta3GatedTrilinear` and `m1Delta6GeometricCone`; enabling either implies `useWeightedSample=true`. |
| `src/demo3d.cpp` | Wired new uniforms; Delta #6 flag changes `uUpperBinConeSin` to the ShaderToy-like candidate `sin(0.75*pi/2)`. |
| `src/main3d.cpp` | Added CLI flags `--m1-delta3-gated-trilinear=` and `--m1-delta6-geometric-cone=`. |
| `tools/v3_m1_delta36/capture_matrix.ps1` | Added 2x2 matrix capture harness for Cornell/Sponza. |
| `doc/7/v3_m1_stage0_delta36_plan.md` | Added plan + self-critique for the #3/#6 A/B slice. |

## Semantics

### Delta #3 flag

`--m1-delta3-gated-trilinear=1`

Current M0 path:

```glsl
upperDir = vec4(upperDirTrilinear.rgb, ws.a);
rad = hit.rgb * l + upperDir.rgb * (1.0 - l) * upperDir.a * uGIStrength;
```

M1 #3 path:

```glsl
upperDir = ws;
rad = hit.rgb * l + upperDir.rgb * (1.0 - l) * uGIStrength;
```

This tests the redefined ShaderToy-style rule: rejected corners contribute zero to numerator and denominator, and surviving corners are renormalized.

### Delta #6 flag

`--m1-delta6-geometric-cone=1`

This changes the WeightedSample visibility cone from the current upper-bin angular width to a ShaderToy-like permissive candidate. It is deliberately treated as an A/B candidate, not final proof of the volumetric cone derivation.

## Verification

| Check | Result |
|-------|--------|
| Build | Passed: `cmake --build build --config Release --target RadianceCascades3D`. Existing MSVC warnings remain. |
| Matrix dry run | Passed: `capture_matrix.ps1 -Scene cornell -N 16 -DryRun` emitted four distinct tags: baseline, delta3, delta6, both. |
| Smoke capture | Passed with both flags ON at N=8; emitted PNG + three EXR sidecars. Temporary smoke files were removed after size verification. |
| CLI parsing | Initial smoke caught bad substring offsets: both flags parsed `=1` as zero. Fixed with `std::strlen(prefix)` and reran smoke successfully. |

## Self-Critique and Improvements

- **SC1: The first CLI parse was wrong.** The hardcoded substring offsets were off by one, so `=1` parsed as zero. Improvement: use `std::strlen(prefix)` instead of manual constants.
- **SC2: Delta #6 remains a candidate, not a proof.** The ShaderToy-like cone may be too permissive for volumetric probes. Improvement: it is isolated behind a flag and documented as requiring metric improvement plus geometric justification before landing.
- **SC3: Delta #3 may regress by dimming.** Historical Phase 3 work saw dimming when using visible-corner renormalized RGB. Improvement: the default remains unchanged and the matrix harness isolates #3 alone from #6.
- **SC4: The matrix harness initially produced blank condition names.** PowerShell hashtable dot access did not work as written. Improvement: use `$c["name"]`; dry-run now shows distinct tags.
- **SC5: This slice should not claim M1 verdict.** No N=2048 matrix metrics were run. Improvement: implementation doc clearly stops at switch/harness readiness; verdict belongs to the next capture-analysis run.

## Next Commands

Run the full Cornell matrix:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_delta36/capture_matrix.ps1 -Scene cornell -N 2048
```

Run the full Sponza matrix:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/v3_m1_delta36/capture_matrix.ps1 -Scene sponza -N 2048
```

Then add an analyzer pass that compares each matrix condition against `tools/v3_baseline/baseline_lock.json` and assigns STRONG/MARGINAL/DEAD per the M1 gate.
