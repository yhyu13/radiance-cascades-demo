# Critic Review 10 - zero_init_cascade_textures_step11_followup_plan.md

Reviewed: 2026-05-11T11:17:28+08:00

Target: `doc/5/claude_plan/zero_init_cascade_textures_step11_followup_plan.md`

## Verdict

The plan correctly identifies the root cause of frame-1 NaN/Inf in probe stats: `gl::createTexture3D` at `gl_helpers.cpp:20-50` calls `glTexImage3D` with `data=nullptr`, which per the GL spec allocates storage without zero-initializing it. The misleading comment at `demo3d.cpp:2842` ("zero-initialized") is also confirmed — the history textures are NOT zero-initialized, they contain whatever GPU memory happened to be there. The proposed fix (zero-init inside `createTexture3D` when `data==nullptr`) is architecturally sound and touches a single point that automatically covers all 15+ 3D texture creation sites.

The plan has several issues: it references `glewIsSupported`/`glewGetProcAddress` as available in `gl_helpers.h` but these functions don't appear in `gl_helpers.cpp` or any header (GLEW provides `glewIsSupported` but the plan doesn't show the actual extension check code), `glClearTexImage` is already used directly at `demo3d.cpp:2009` without any extension check (suggesting the codebase assumes GL 4.4+ availability rather than checking it), the `bytesPerPixel` fallback needs to handle `GL_RGBA16F` as an internal format (the plan's helper only covers `format`/`type` pairs, but `GL_RGBA16F` is an internal format where `format=GL_RGBA, type=GL_HALF_FLOAT` maps to 8 bytes per pixel — which IS in the plan's table), the plan's `glClearTexImage != nullptr` check implies the function pointer is loaded via `glewGetProcAddress` but the codebase uses GLEW macros that make the function available directly if the extension is present, and the fallback path's `std::vector<uint8_t>` allocation for the largest cascade atlas (64 MB) would work but has no fallback for potential allocation failure on memory-constrained systems.

## Evidence Checked

- `doc/5/claude_plan/zero_init_cascade_textures_step11_followup_plan.md`.
- Current `src/gl_helpers.cpp`: `createTexture3D` at lines 20-50 (calls `glTexImage3D` with `data` parameter, no zero-init after allocation).
- Current `src/gl_helpers.h`: file exists but does NOT contain `glewIsSupported`, `glewGetProcAddress`, or `bytesPerPixel` declarations (checked by Select-String, no matches found).
- Current `src/demo3d.cpp`: misleading comment at line 2842 ("zero-initialized"), confirmed. `glClearTexImage` used directly at line 2009 without extension check. 15+ `createTexture3D` call sites confirmed (lines 144, 2685, 2690, 2695, 2700, 2705, 2710, 2716, 2720, 2724, 2733, 2830, 2843, 2855).
- Current `src/main3d.cpp`: GLEW initialized at line 449 (`glewInit()`).
- Current `res/shaders/radiance_3d.comp`: `sanitizeRadiance` function at line 94 (clamps NaN/Inf to [0, 100]).
- Current `res/shaders/reduction_3d.comp`: per-bin clamp at line 44 (`clamp(avg, vec3(0.0), vec3(100.0))`).

## What Looks Good

- The root cause analysis is correct: `glTexImage3D(..., nullptr)` allocates uninitialized storage, and the history textures (`probeAtlasHistory`, `probeGridHistory`) read uninitialized data on frame 1 via `imageLoad` in the EMA blend path. This produces NaN/Inf that the sanitization catches but still leaks into frame-1 stats.
- The misleading comment at `demo3d.cpp:2842` is confirmed: the text says "zero-initialized" but `createTexture3D` passes `nullptr`. After the proposed fix, the comment would become accurate.
- The single touchpoint approach (modify `createTexture3D` itself) is clean — all 15+ existing callers automatically benefit without per-site changes.
- The `if (data == nullptr)` guard preserves callers that pass real data (none currently do for compute-write textures, but it's defensive).
- The `bytesPerPixel(format, type)` helper covers the formats this codebase actually uses: `(GL_RGBA, GL_HALF_FLOAT)` → 8, `(GL_RGBA, GL_FLOAT)` → 16, `(GL_RGBA, GL_UNSIGNED_BYTE)` → 4, `(GL_RED_INTEGER, GL_UNSIGNED_INT)` → 4.
- The plan correctly identifies that zero-init can't break anything — all 3D textures are written by compute shaders before any read, so prior contents are irrelevant.
- The verification plan is thorough: frame-1 stats clean, settled stats unchanged, visual unchanged, other 3D textures still work, extension fallback tested.
- The out-of-scope section correctly defers additional `imageStore` sanitization and probe occupation tuning.

## Findings

### 1. `glewIsSupported`/`glewGetProcAddress` referenced but not in `gl_helpers.h`

Severity: Medium

The plan says "`glewIsSupported`/`glewGetProcAddress` already pulled in via `gl_helpers.h`". But `gl_helpers.h` does NOT contain these — Select-String finds no matches for either function in `gl_helpers.cpp` or `gl_helpers.h`. GLEW provides `glewIsSupported()` and `glewGetExtensionProcAddress()` globally after `glewInit()` (called at `main3d.cpp:449`), so they ARE available in the codebase, but they're not "pulled in via `gl_helpers.h`". The plan's wording suggests the extension check infrastructure already lives in the helper file, which is inaccurate.

More importantly, the plan doesn't show the actual extension check code. The pseudocode `if (glClearTexImage != nullptr)` implies a function-pointer check, but GLEW makes `glClearTexImage` available as a direct function call if `GL_ARB_clear_texture` is supported (via `#define glClearTexImage ...` in `glew.h`). The correct GLEW-based check would be `if (GLEW_ARB_clear_texture)` or `if (glewIsSupported("GL_ARB_clear_texture"))`, not a null-pointer comparison on the function itself.

### 2. `glClearTexImage` is already used without extension check at `demo3d.cpp:2009`

Severity: Low

The existing codebase uses `glClearTexImage(sdfTexture, 0, GL_RED, GL_FLOAT, nullptr)` at `demo3d.cpp:2009` without any extension availability check. This means the codebase already assumes `GL_ARB_clear_texture` is available on the target hardware. The plan's fallback path (CPU-side zero buffer + `glTexSubImage3D`) is defensive but inconsistent with the existing usage pattern — if `glClearTexImage` were not available, the SDF clear at line 2009 would already be failing.

Either:
- The codebase should add an explicit `GLEW_ARB_clear_texture` check at startup and fall back gracefully everywhere (including the existing SDF clear), OR
- The codebase should assume GL 4.4+ (which `GL_ARB_clear_texture` requires) is always available on target hardware (matching the RTX 2080 SUPER specification in the plan) and skip the fallback entirely.

The plan's hybrid approach (fast path + fallback in `createTexture3D` but direct call without check elsewhere) creates a maintenance inconsistency.

### 3. `bytesPerPixel` helper doesn't cover `GL_RGBA16F` internal format explicitly

Severity: Low

The plan's `bytesPerPixel(format, type)` helper maps `(GL_RGBA, GL_HALF_FLOAT)` → 8 bytes. This IS the correct mapping for `GL_RGBA16F` internal format (4 channels × 2 bytes per half-float = 8 bytes per pixel). However, the helper uses the `format` and `type` parameters (which are the pixel transfer format/type), not the `internalFormat` parameter. This works because the plan's fallback path uses `glTexSubImage3D` with the same `format` and `type` that were passed to `glTexImage3D`.

But there's a subtle issue: the `format`/`type` parameters in `createTexture3D` are the pixel transfer format, not necessarily matching the internal format. For example, `GL_RGBA16F` internal format could theoretically be uploaded with `GL_RGBA, GL_FLOAT` (16 bytes per pixel) instead of `GL_RGBA, GL_HALF_FLOAT` (8 bytes per pixel). The `bytesPerPixel` helper would return 16 in that case, allocating 128 MB for a 8M-pixel texture instead of 64 MB. This is wasteful but not incorrect — the upload would still zero-init correctly because `glTexSubImage3D` handles the format conversion.

The plan should note that `bytesPerPixel` computes the CPU-side buffer size (based on pixel transfer format), not the GPU-side storage size (based on internal format). These may differ, but the zero-init upload still works because GL converts between them.

### 4. 64 MB fallback allocation has no failure handling

Severity: Low

The fallback path allocates `std::vector<uint8_t> zeros(size_t(width) * height * depth * bpp, 0)`. For the largest cascade atlas (256×256×32 at `GL_RGBA16F` = 8 bpp), that's `256 × 256 × 32 × 8 = ~16 MB`, not 64 MB as the plan claims. Let me verify: C0 at 32³ probes with D=8 has atlas dimensions `32*8 = 256` per axis, depth = 32. That's `256 × 256 × 32 × 8 = ~16 MB`. C1 at 16³ with D=16 has `16*16 = 256` per axis, depth = 16, so `256 × 256 × 16 × 8 = ~8 MB`. The plan says "largest cascade atlas at D=16 is ~64 MB" but the actual largest is C0 at ~16 MB. The 64 MB claim may be from a different D/resolution configuration or an error.

Either way, a 16-64 MB transient allocation should succeed on any modern GPU-capable system. But `std::vector` will throw `std::bad_alloc` if allocation fails, which would crash the application. The plan should add a `try/catch` around the fallback allocation and fall back to per-slice clearing if the full allocation fails.

### 5. The `glClearTexImage` call is `nullptr` for clear value, which zeros the texture

Severity: Low

The plan's fast path uses `glClearTexImage(texture, 0, format, type, nullptr)`. Per the GL spec, when `data` is `nullptr`, `glClearTexImage` fills the entire texture with zeros. This is correct for zero-initialization. However, the `format` and `type` parameters here specify how to interpret the zero value — for `GL_RGBA, GL_HALF_FLOAT`, a zero value fills all RGBA channels with half-float 0.0. For `GL_RED_INTEGER, GL_UNSIGNED_INT`, it fills the red channel with integer 0. Both are correct for zero-init.

But note that `glClearTexImage` uses `format` and `type` to interpret the clear value, while the texture was created with `internalFormat`. The spec guarantees that `glClearTexImage` handles the conversion from the clear value format to the internal format, so this works correctly.

### 6. The plan modifies `gl_helpers.cpp` but doesn't address the `gl_helpers.h` header

Severity: Low

The plan adds a `bytesPerPixel(format, type)` helper to `gl_helpers.cpp` but doesn't mention adding its declaration to `gl_helpers.h`. The current `gl_helpers.h` presumably declares `createTexture3D` and other public functions. Adding a new helper without a header declaration would cause compilation errors if any other file tries to call it (unlikely since it's only used inside `createTexture3D`, but it should be declared anyway for consistency and potential future use).

If `bytesPerPixel` is only used inside `createTexture3D` as an internal helper, it could be a static function in `gl_helpers.cpp` (no header change needed). The plan should specify whether `bytesPerPixel` is public (header declaration) or private (static in cpp).

### 7. The plan claims "no API changes" but adds a `bytesPerPixel` helper

Severity: Low

The plan says "no new headers, no API changes (signature of `createTexture3D` unchanged)". The `createTexture3D` signature IS unchanged, but adding `bytesPerPixel` as a public helper would be an API addition to `gl_helpers.h`. If it's a static helper, this claim is accurate. The plan should clarify.

### 8. Verification step 2 references "post-Step-11 P0" stats that differ from codex 09 observed stats

Severity: Low

The plan says the expected frame-1 stats are `C0=-nan C1=0.00000 C2=-435.957 C3=0.00000` "post-Step-11 P0". But codex 09 observed `C0=-nan C1=-nan C2=-375.25 C3=-320.526` (with additional NaN in C1 and C3 having negative meanLum). The plan's expected values differ from what was actually observed in the runtime verification, suggesting the "post-P0" stats may be from a different run configuration or a different hardware/driver state. The verification should use the actual observed stats as the baseline to confirm improvement.

### 9. The plan doesn't discuss `GLEW_ARB_clear_texture` runtime check placement

Severity: Medium

The plan assumes `glClearTexImage` is available on the target hardware (RTX 2080 SUPER with GL 4.4+). But `gl_helpers.cpp` is a general utility that could be used on other hardware. The plan should specify where the `GLEW_ARB_clear_texture` check happens:

- Option A: Check at `createTexture3D` call time (every texture creation checks the extension). This is simple but redundant (checks 15+ times).
- Option B: Check once at GLEW init time and store a global `bool hasClearTexImage` that `createTexture3D` reads. This is more efficient and consistent.
- Option C: Assume GL 4.4+ is always available (matching the existing `glClearTexImage` usage at line 2009) and skip the fallback entirely.

The plan implicitly uses Option A (check inside `createTexture3D`) but doesn't specify how the function pointer or extension flag is obtained.

## Verification Gaps To Add

- Run `--exit-frames=2` capture and confirm frame-1 stats show `C0=0.00000 C1=0.00000 C2=0.00000 C3=0.00000` (all zeros, no NaN).
- Test the fallback path by temporarily disabling `glClearTexImage` (e.g., `#define glClearTexImage nullptr` or setting a global flag) and confirming the CPU-side upload still produces zero-initialized textures.
- Verify the 64 MB fallback allocation claim: measure the actual largest cascade atlas size at runtime (`256×256×32×8 = ~16 MB` for C0, not 64 MB).
- Add a `GLEW_ARB_clear_texture` check at startup (near `glewInit` at `main3d.cpp:449`) and log the availability. This makes the extension support explicit rather than assumed.
- If `bytesPerPixel` is a static helper, mark it `static` in `gl_helpers.cpp` and don't add it to `gl_helpers.h`. If it's public, add the declaration.
- Confirm that the existing `glClearTexImage` call at `demo3d.cpp:2009` (SDF texture clear) still works after the change — the `createTexture3D` zero-init doesn't affect textures that are later cleared explicitly.