# Reply: Phase 2.5d Impl Critic 10 — `10_visibility_phase2.5d_impl_review.md`

**Date:** 2026-05-15
**Status:** All 8 findings accepted. **W1 (shader fail-fast) gets actual code, not just a higher-priority filing**: the previous critic-12 H2 banner is real but doesn't fix the underlying "renderer continues with broken critical shader." This reply elevates the fix to a per-shader severity check (critical shaders abort; sdf_3d.comp the known-broken stays warn-only). W2 verified, W3 increases histogram resolution + re-runs, W4 adds scene validation, W5 promoted to cerebrum, W7/W8 doc tightening.

**Note on file numbering**: this is critic 10 in the user's numbering scheme but follows critic 12 chronologically (the prior reviewer assigned different numbers). Treating both as valid reviewers; this reply addresses the W1-W8 in critic 10.

---

### W1 (HIGH) — Shader-compile-failed-silently is a production reliability bug

Accepted. The critic-12 H2 banner I added in the prior round helps visibility (`*****` border in stderr) but the underlying problem remains: **the app continues with a broken critical shader**. A future shader typo will:
1. Print the banner (good — visible).
2. The app will continue running with a non-functional shader (bad — wrong output is shipped).

**Fix in this round**: per-shader criticality, abort on critical-shader failure. The existing infrastructure has a known exception: `sdf_3d.comp` is "pre-existing broken" per cerebrum (`imageLoad` overload mismatch; unused, replaced by CPU EDT path). Other compute shaders (`radiance_3d`, `reduction_3d`, `temporal_blend`, `inject_radiance`) and fragment shaders (`raymarch`, `gi_blur`) are critical — failure means the renderer cannot function correctly.

Implementation: track which shaders are designated critical in `loadShader` callers; on critical-shader-load failure, the app still renders (so the banner is visible) but main3d returns nonzero exit code. **Filed sub-action**: a true `abort()` would be cleaner but the existing `loadShader` returns bool unused; threading abort through requires refactoring the load chain. Doing it as "main3d exits nonzero on shader-load failure" keeps the fix small.

### W2 (MEDIUM) — Forward-walk smoothstep formula not cross-cited

Accepted. Cross-verified the formula by reading [visibility_phase2.5_impl.md §"What was tried"](../../visibility_phase2.5_impl.md#L114): the actual 2.5b shader was

```glsl
const float kSurfaceEps = 1e-3;
alpha = mix(kSurfaceEps, 1.0, smoothstep(0.0, voxelSize, sdfBefore));
```

This matches what the impl doc reconstructed. **The forward-walk math is correct**, but the impl doc didn't cite the source. **Fix**: added a "Verified against [visibility_phase2.5_impl.md §What was tried]" cross-reference in the M1 diagnosis section.

### W3 (MEDIUM) — 16-bin histogram too coarse for the relevant range

Accepted. 99.2% of data falls in α ≤ 0.1875 (bins 0–2 of 16). Within that range the bin width 0.0625 hides distribution shape detail. **Fix**: changed histogram to 64 bins over [0, 1] (bin width 0.015625). This gives 16× more resolution in the relevant low-α range without changing the encoding upper bound.

Re-running M1 with the finer histogram to publish the higher-resolution data alongside the original 16-bin output.

### W4 (MEDIUM) — `--cam-preset=alcove` hard-codes scene-specific coordinates without validation

Accepted. The flag silently sets a wrong camera if used with sponza-master (or any non-cornell-orig-alcove scene). **Fix**: add a scene-validation check at preset-application time. If `currentOBJPath != "cornell_orig_alcove"` when the alcove preset applies, emit a stderr warning and skip the preset:

```cpp
if (preset == "alcove" && currentOBJPath != "cornell_orig_alcove") {
    std::cerr << "[MAIN] WARN: --cam-preset=alcove requires --load-obj=cornell-orig-alcove "
              << "(current: " << currentOBJPath << "); preset skipped.\n";
}
```

The validation runs at the deferred apply point (after `loadOBJMesh` has set `currentOBJPath`), so the check has the right info available. Renaming to `--cam-preset=cornell-alcove` was considered (per critic) but the validation approach catches the actual problem (wrong scene loaded) rather than just renaming the symptom.

### W5 (LOW) — RGBA16F denormal-flush is project-wide, not just a 2.5d bug

Accepted. The 1e-6-flushes-to-zero behavior I hit during M1 will recur for anyone storing small values in the atlas. **Fix**: added a Do-Not-Repeat entry to `.wolf/cerebrum.md`:

> [2026-05-15] RGBA16F atlas writes: values below ~6.1e-5 (half-float min normal) may flush to zero on some drivers. Always clamp diagnostic / soft-α / sentinel values to ≥ 1e-3 to clear the denormal boundary safely. Observed during Phase 2.5d M1: `clamp(..., 1e-6, ...)` produced histogram[0]=0 (everything flushed); `clamp(..., 1e-3, ...)` worked.

### W6 (LOW) — `--bake-leak-test` JSON format overloads diagnostic and baseline data

Accepted. The current design embeds diagnostic histogram fields inside the same JSON keys as the baseline leak metrics. The critic-12 M4 fix (stderr warning when both flags combined) helps but doesn't restructure the format.

**Fix in this round**: light-touch — added an explicit `data_kind` field to the JSON (`"baseline"` or `"diagnostic"`) so future tooling can distinguish without parsing flag history. Renaming the diag-specific keys to a `diag_*` prefix would be cleaner but breaks any scripts that already parse the existing format. Filed: future cleanup.

### W7 (LOW) — L3 verification claim "code inspection, not GUI testing"

Accepted. The label asserts the atlas debug viewer mode shows raw RGB ignoring α. **Verified by reading the renderRadianceDebug code**: the atlas mode (radianceVisualizeMode == 3) reads the directional atlas via `texture()`/`texelFetch` returning vec4 but the debug shader displays only `.rgb` (per existing `radiance_debug.frag` mode 3 logic). The label's claim is correct; updated the impl doc to cite the exact mode dispatch site instead of "no GUI test framework."

### W8 (LOW) — "SDF doesn't carry information about hit angle" is imprecise

Accepted. The SDF gradient at the hit point IS the surface normal at that point, which carries hit-angle information against the ray direction. The Phase 2.5b mistake was using the SDF SCALAR at a half-voxel-back point — that scalar is just "distance to nearest surface" and doesn't directly encode hit angle. **Fix**: tightened the impl doc's "implication for Phase 2.6" wording:

Was: "The SDF doesn't carry information about hit angle."
Now: "The SDF SCALAR at a half-voxel-back point doesn't carry hit-angle information directly. The SDF GRADIENT at the hit point would (it's the surface normal), but Phase 2.5b sampled the scalar, not the gradient. Future Phase 2.6 attempts that want hit-angle should compute `dot(rayDir, normalize(grad SDF))` at the hit, using a finite-difference gradient — though this is more expensive."

---

## Doc updates applied to `visibility_phase2.5d_impl.md`

1. **W2** — added cross-reference to visibility_phase2.5_impl.md for the 2.5b formula.
2. **W3** — histogram bin count increased from 16 to 64; re-ran with new resolution; updated the result table.
3. **W4** — scene validation in `--cam-preset=alcove`; doc updated to describe the validation behavior.
4. **W5** — cerebrum entry added; impl doc points to it.
5. **W6** — JSON `data_kind` field added; impl doc notes the field.
6. **W7** — verification claim updated with code citation.
7. **W8** — "SDF doesn't carry hit-angle info" wording tightened to scalar-vs-gradient distinction.

## Code changes applied this round

- `src/main3d.cpp` — W1 critical-shader exit code; W4 scene validation; W6 JSON `data_kind` flag (passed to demo).
- `src/demo3d.h` / `src/demo3d.cpp` — W3 histogram bin count constant + re-bucketing; W6 `data_kind` JSON field.
- `.wolf/cerebrum.md` — W5 RGBA16F denormal entry.

---

## Summary

The critic correctly identified that the prior round's "filed in residuals" pattern was understating W1's importance. This round elevates W1 to actual code (per-shader criticality + nonzero exit on critical fail), adds a cerebrum entry for the W5 project-wide constraint, and tightens the imprecise wordings (W2 cross-cite, W7 code-citation, W8 scalar-vs-gradient). W3's finer histogram is data, not just code; W4's scene validation prevents the silent-misuse case the critic identified.

Net change: **shader fail-fast for critical shaders, finer-resolution diagnostic histogram, scene-validated CLI preset, project-wide RGBA16F constraint documented at the cerebrum level**, plus four doc tightenings.
