# Plan: Step 11 follow-up — Zero-init cascade probe textures (revised after codex 10)

## Changelog (post codex `10_zero_init_cascade_textures_step11_followup_plan_review.md`)

All 9 findings accepted. Plan collapsed substantially after F2+F9
caught that the codebase already assumes GL 4.4+:

- **F2+F9 (low+medium) plan rewrite.**
  [demo3d.cpp:2009](../../../src/demo3d.cpp#L2009) already calls
  `glClearTexImage` directly with no extension check / fallback. My
  proposed defensive scaffolding (`glewIsSupported` check, CPU-buffer
  fallback path, `bytesPerPixel` helper) was over-engineering. Plan
  now matches the existing pattern: a single `glClearTexImage` call
  inside `createTexture3D`, no fallback, no helper.
- **F1 (medium) moot via F2+F9.** My pseudocode `if (glClearTexImage
  != nullptr)` was the wrong GLEW idiom anyway (correct is
  `if (GLEW_ARB_clear_texture)` since GLEW resolves the symbol to
  an internal stub, never `nullptr`). Gone with the fallback.
- **F3 (low) moot via F2+F9.** No `bytesPerPixel` helper → no
  format-vs-internal-format conflation concern.
- **F4 (low) doc fix + moot.** Largest cascade atlas is **~16 MB**
  (C0: 256×256×32 × 8 bpp), not 64 MB as I claimed — D=16 only
  applies to upper cascades which have smaller probe resolutions.
  Moot via F2+F9 (no allocation at all in the new plan).
- **F5 (low) acknowledged.** `glClearTexImage(.., nullptr)` correctly
  fills with zero per GL spec; format/type → internal-format
  conversion is spec-guaranteed.
- **F6+F7 (low) moot via F2+F9.** No helper to mark `static` or
  declare in header. The "no API changes" claim is now strictly
  accurate.
- **F8 (low) doc fix.** Verification step's reference baseline was
  inconsistent with codex 09's actual observations (different runtime
  states). Reworded: success criterion is simply "all four cascades
  show 0.00000 in frame 1" — any non-zero/NaN/negative value
  indicates the zero-init didn't take.

Net effect: ~25 lines of plan code → ~5 lines. Single substantive
change inside `createTexture3D`, plus the comment fix at
demo3d.cpp:2842.

## Context

Step 11 codex 09 P0 added shader-side `sanitizeRadiance` clamps in
[radiance_3d.comp](../../../res/shaders/radiance_3d.comp) and
[reduction_3d.comp](../../../res/shaders/reduction_3d.comp) — these catch
NaN/Inf and prevent indefinite propagation through the EMA mix. But
the **frame-1 garbage** still appears once on every startup:

- [src/gl_helpers.cpp:34](../../../src/gl_helpers.cpp#L34) calls
  `glTexImage3D(..., data=nullptr)` — per the GL spec this allocates
  storage WITHOUT zero-initializing it.
- On frame 1, the radiance bake's `imageLoad(probeAtlasHistory, ...)`
  reads whatever bytes happened to be in that GPU memory. Interpreted
  as `rgba16f`, that's typically NaN/Inf/random.
- The sanitization clears it during frame 1's reduction, so frame 2
  onward is clean — but the corrupt frame-1 stats still leak into
  probe-stats logs, RenderDoc captures, and any
  `--exit-frames=1` headless run.

There's also a literal misleading comment at
[demo3d.cpp:2842](../../../src/demo3d.cpp#L2842):
`// Phase 9: temporal history atlas — same layout as probeAtlasTexture (zero-initialized)`
The texture is **not** zero-initialized — `createTexture3D` passes
`nullptr`. This plan corrects both the behavior and the comment.

The change is defense-in-depth (sanitization remains as the runtime
safety net) and small (~15 lines). It eliminates the frame-1
garbage from startup so downstream tooling sees clean data from
the very first frame.

---

## Approach

Modify `gl::createTexture3D` itself to zero-init storage after
allocation. Single touchpoint covers ALL 3D textures
(`probeAtlasTexture`, `probeAtlasHistory`, `probeGridTexture`,
`probeGridHistory` per cascade — plus `voxelGridTexture`, `sdfTexture`,
`albedoTexture`, `voronoiTextureA/B`, `meshVoxelBaseTexture`,
`voxelOwnerTexture`, etc.). All 3D textures in this codebase are
written by compute shaders before any read, so zero-init can't
break anything that relied on prior contents (none do).

**Codex 10 F2+F9: match the existing pattern.** The codebase
already calls `glClearTexImage` directly at
[demo3d.cpp:2009](../../../src/demo3d.cpp#L2009) with no extension
check, no fallback — implicitly assuming `GL_ARB_clear_texture`
(GL 4.4+) availability. Every dGPU since 2013 and Intel iGPU since
Skylake 2015 supports it. Match the pattern; no defensive scaffolding.

```cpp
// gl_helpers.cpp — inside createTexture3D, AFTER glTexImage3D and
// AFTER setTexture3DParameters (so the texture is fully initialized):
if (data == nullptr) {
    // Step 11 follow-up: zero-init storage so callers see deterministic
    // zeros instead of uninitialized GPU memory on first read. Matches
    // demo3d.cpp:2009's existing pattern of calling glClearTexImage
    // directly (codebase assumes GL 4.4+ / GL_ARB_clear_texture).
    glClearTexImage(texture, 0, format, type, nullptr);
}
```

Single line of substantive code. The `if (data == nullptr)` guard
preserves existing callers that pass real data (none in this
codebase actually do for compute-write textures, but defensive).

If a future port targets GL 4.3-only hardware, BOTH this clear AND
the existing line-2009 clear would need a coordinated fallback —
that's a one-time addition at that point, not a proactive
defensive measure now.

---

## Files Modified

- [src/gl_helpers.cpp](../../../src/gl_helpers.cpp) — extend `createTexture3D`
  with a single `glClearTexImage` call after `glTexImage3D` when
  `data == nullptr` (~5 lines net)
- [src/demo3d.cpp](../../../src/demo3d.cpp) — fix the misleading `// Phase 9:
  temporal history atlas (zero-initialized)` comment at line 2842
  (one-line edit; the comment is now true after the helper change)

No new headers, no API changes (signature of `createTexture3D`
unchanged), no helpers, no shader changes, no per-cascade
allocation-site changes.

---

## Reuse from existing code

- `gl::createTexture3D` itself ([gl_helpers.cpp:20](../../../src/gl_helpers.cpp#L20)) —
  the change lives inside this single function, so all 11+ existing
  callers automatically benefit
- `glClearTexImage` direct-call pattern at
  [demo3d.cpp:2009](../../../src/demo3d.cpp#L2009) — the new code
  matches it exactly (no extension check, no fallback)
- The Step 11 shader sanitization (`sanitizeRadiance` /
  reduction-pass per-bin clamp) stays as the runtime safety net —
  this plan does not touch it

---

## Verification

1. **Build clean** — `cmake --build build --config Release`. No new
   warnings expected.
2. **Frame-1 stats are clean** — capture Sponza-master at 2 frames
   and read the first `[4c A/B]` log line. **Success criterion:
   all four cascades show `0.00000`** (any non-zero, NaN, or
   negative value indicates the zero-init didn't take).
   Codex 09 observed values like `C0=-nan C1=-nan C2=-375.25
   C3=-320.526` at this stage; my post-P0 recapture saw similar but
   different garbage (`C0=-nan C1=0 C2=-435.957 C3=0`) — the exact
   pre-fix garbage varies by GPU memory state, so the success
   criterion is "all zeros" not "matches a specific pre-fix value".
   ```
   --load-obj=sponza-master --gpu-voxelize --gpu-sdf --exit-frames=2
   --camera-pos=1.0710,-0.0723,-0.3393
   --camera-target=0.1212,-0.0812,-0.6520
   ```
3. **Settled stats unchanged** — capture 300 frames, confirm
   `C0≈0.0169 C1≈0.0160 C2≈0.0140 C3≈0.0080` (matches Step 11
   post-P0+P2 baseline). Zero-init affects only frame 1.
4. **Visual unchanged** — capture mode 13 at the cam.md viewpoint;
   diff against `tools/step11_verify_heatmap13_postP0P2.png` should
   be visually identical.
5. **Other 3D textures still work** — confirm `voxelGridTexture`,
   `sdfTexture`, `albedoTexture`, `meshVoxelBaseTexture`, and
   `voronoiTextureA/B` (all created via the same helper) still
   render Sponza correctly. Mode 0 capture should match prior
   baseline.
6. **No extension fallback to test** — codex 10 F2+F9 collapsed
   the plan to match `demo3d.cpp:2009`'s direct-call pattern. If
   the existing line-2009 clear works (it has been since Phase 8),
   the new clear works.

---

## Out of Scope

- Sanitization at the remaining `imageStore` sites
  (`temporal_blend.comp:82`, `inject_radiance.comp:336`) — defense
  in depth would be nice but the upstream radiance_3d/reduction_3d
  clamps already cover the propagation path
- Tuning probe occupation (codex 09 P1 — user chose to skip)
- Removing the shader-side sanitization (it stays as a runtime
  safety net for any future bake-formula bug)
- Adding `glObjectLabel`-style instrumentation around the new
  clear calls (the existing labels at
  [demo3d.cpp:2861-2864](../../../src/demo3d.cpp#L2861) are sufficient for
  RenderDoc identification)
