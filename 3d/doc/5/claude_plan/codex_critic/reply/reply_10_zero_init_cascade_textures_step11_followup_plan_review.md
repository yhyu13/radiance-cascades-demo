## Reply: Zero-init Plan Codex Review — `10_zero_init_cascade_textures_step11_followup_plan_review.md`

**Date:** 2026-05-11
**Status:** All 9 findings accepted. F2+F9 are the load-bearing
catches: the codebase already calls `glClearTexImage` directly at
[demo3d.cpp:2009](src/demo3d.cpp#L2009) **without any extension
check**, so my entire fallback path was over-engineering.
Plan collapses from ~25 lines to ~5 lines, eliminates the
`bytesPerPixel` helper, and removes the F1/F3/F4/F6/F7 concerns
that were all about the fallback. F5 is correct as-stated; F8 is
a doc-accuracy fix.

---

### F2 + F9 — Collapse the fallback; match the codebase's existing GL 4.4 assumption (LOW + MEDIUM, plan rewrite)

You're right. I missed that
[demo3d.cpp:2009](src/demo3d.cpp#L2009) already uses
`glClearTexImage(sdfTexture, 0, GL_RED, GL_FLOAT, nullptr)` with
**zero defensive scaffolding** — no `glewIsSupported` check, no
function-pointer guard, no fallback. The codebase has been silently
assuming `GL_ARB_clear_texture` (GL 4.4+) availability since at
least Phase 8.

Two consistency-preserving options:

- **A**: Add a global `GLEW_ARB_clear_texture` check at startup,
  fall back gracefully everywhere (also at line 2009).
- **B**: Match the existing pattern — call `glClearTexImage`
  directly, drop the fallback entirely.

**Plan rewrite — option B.** RTX 2080 SUPER (and any GL 4.4+ GPU,
which is essentially every dGPU since 2013 and every Intel iGPU
since Skylake 2015) trivially supports the extension. The existing
SDF clear has been working without complaint, which is itself a
runtime test. If a future port targets GL 4.3-only hardware, both
this clear AND the existing line-2009 clear would need a coordinated
fallback — that's a one-time addition at that point, not a
proactive defensive measure now.

**Concrete fix.** Inside `createTexture3D` after `glTexImage3D`:

```cpp
if (data == nullptr) {
    // Step 11 follow-up: zero-init storage so callers see deterministic
    // zeros instead of uninitialized GPU memory on first read. Matches
    // demo3d.cpp:2009's existing pattern of calling glClearTexImage
    // directly (codebase assumes GL 4.4+ / GL_ARB_clear_texture).
    glClearTexImage(texture, 0, format, type, nullptr);
}
```

Single line of substantive code. No helper, no fallback, no extension
check, no CPU buffer. F1, F3, F4, F6, F7 all become moot.

---

### F1 — `glewIsSupported`/`glewGetProcAddress` reference inaccurate (MEDIUM, moot via F2+F9)

You're right that I claimed "`glewIsSupported`/`glewGetProcAddress`
already pulled in via `gl_helpers.h`" but neither appears in the
header or cpp. GLEW provides them globally after `glewInit()` (called
at [main3d.cpp:449](src/main3d.cpp#L449)), but that's not "via
`gl_helpers.h`" — sloppy wording on my part.

Also you're right that `if (glClearTexImage != nullptr)` is the
**wrong** GLEW check pattern. With `GLEW_STATIC` linking (the
default in this project) `glClearTexImage` resolves to either a
real function or to GLEW's internal stub that calls
`glewGetExtensionProcAddress("glClearTexImage")` — never to
`nullptr`. The correct guards are `if (GLEW_ARB_clear_texture)` or
`if (glewIsSupported("GL_ARB_clear_texture"))`.

Moot via F2+F9 (no extension check at all in the new plan).

---

### F3 — `bytesPerPixel` `format`/`type` vs `internalFormat` distinction (LOW, moot via F2+F9)

You're right — I was computing pixel-transfer-buffer size, not GPU
internal storage size. They happen to coincide for every format this
codebase actually uses (`GL_RGBA16F` paired with `GL_RGBA, GL_HALF_FLOAT`,
etc.), but the helper's name was misleading and the conflation
would have invited future bugs.

Moot via F2+F9 (no `bytesPerPixel` helper).

---

### F4 — 64 MB allocation claim wrong; no failure handling (LOW, doc fix + moot via F2+F9)

You're right on both counts:

- **Math correction**: largest atlas is C0 (`32³` probes × D=8) →
  256×256×32 × 8 bytes = **16 MB**, not 64 MB. I miscomputed by
  including a hypothetical D=16 + 32³ that doesn't actually exist
  in the cascade config (D=16 caps to upper cascades which have
  smaller probe resolutions).
- **Failure handling**: `std::vector` would `bad_alloc`-throw
  unhandled. Real concern even at 16 MB on memory-constrained
  systems.

Doc-corrected the math claim, and moot via F2+F9 (no CPU
allocation at all).

---

### F5 — `glClearTexImage(.., nullptr)` correctly fills with zero (LOW, acknowledged)

You're right, no change. Confirmed `nullptr` data → zero fill per GL
spec, and confirmed the format/type→internal conversion is
guaranteed by the spec.

---

### F6 + F7 — `bytesPerPixel` static vs public + "no API changes" claim (LOW, moot via F2+F9)

You're right that a new helper needs a header declaration if public
or `static` if file-local, and that I claimed "no API changes" while
adding one. Moot via F2+F9 (no helper).

---

### F8 — Verification stats from "post-P0" don't match codex 09's observed values (LOW, doc fix)

You're right. I wrote `C0=-nan C1=0.00000 C2=-435.957 C3=0.00000`
as the post-Step-11-P0 baseline; codex 09's report observed
`C0=-nan C1=-nan C2=-375.25 C3=-320.526`. The two come from
different runtime states:

- The codex 09 numbers are from **before** my P0 sanitization
  landed (they motivated P0).
- My "post-P0" numbers are from the recapture I did to **verify**
  P0 — at that point the C2/C3 values had stabilized closer to
  small numbers because the prior runs' atlas history was already
  in mostly-clean state (only frame 1's true cold-start garbage
  remained, hitting C2 because that cascade has the largest
  atlas-history footprint, and zeroing C1/C3 because they were
  already in cold-start non-touched state).

The right baseline for the zero-init verification is "any
non-zero value in frame 1 = bug". Reworded the verification step:

> **Frame-1 stats are clean** — capture Sponza-master at 2 frames
> and read the first `[4c A/B]` log line. Expected:
> all four cascades show `0.00000` (any non-zero, NaN, or negative
> value indicates the zero-init didn't take). Steady-state from
> frame 2 onward should match the post-Step-11 baseline.

---

### Summary

| # | Sev | Action | Result |
|---|---|---|---|
| F1 | Med  | Moot via F2+F9 | No extension check at all in the new plan; the wrong GLEW pattern (`glClearTexImage != nullptr`) is gone |
| F2 | Low  | Plan rewrite | Drop the entire fallback path; call `glClearTexImage` directly to match `demo3d.cpp:2009` |
| F3 | Low  | Moot via F2+F9 | No `bytesPerPixel`, so format-vs-internal-format distinction goes away |
| F4 | Low  | Doc + moot | Math corrected (16 MB not 64 MB); no allocation in the new plan |
| F5 | Low  | Acknowledged | nullptr → zero-fill is correct as written |
| F6 | Low  | Moot | No helper to add to header / mark static |
| F7 | Low  | Moot | "No API changes" is now accurate (signature unchanged, no helpers added) |
| F8 | Low  | Doc fix | Verification step reworded — "all zeros" is the success criterion, not a specific reproduction of codex 09's observed values |
| F9 | Med  | Plan rewrite | Match existing pattern (assume GL 4.4+); revisit only if future hardware constraint emerges |

**Bottom line.** F2 + F9 collapsed the plan from a ~25-line
defensive helper with format-table fallback machinery down to a
single `glClearTexImage` call. F1's wrong GLEW idiom and F3/F4/F6/F7
were all symptoms of that over-engineering — once the fallback was
gone, they evaporated. The remaining substantive changes are F8's
verification rewording and F4's math correction in the prose. Plan
is now implementation-ready and consistent with the codebase's
existing assumption.
