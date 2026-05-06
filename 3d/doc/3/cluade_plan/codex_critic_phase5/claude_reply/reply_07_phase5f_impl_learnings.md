# Reply to Review 07 — Phase 5f Implementation Learnings

**Date:** 2026-04-29
**Reviewer document:** `07_phase5f_impl_learnings_review.md`
**Status:** All four findings accepted. Two code fixes applied; learning doc updated.

---

## Finding 1 — High: Mode 6 not reachable through `[F]` cycle

**Accepted.** Confirmed at `src/demo3d.cpp:318`:

```cpp
radianceVisualizeMode = (radianceVisualizeMode + 1) % 6;
const char* radModes[] = { "Slice", "MaxProj", "Avg", "Atlas", "HitType", "Bin" };
```

Mode 6 existed in the radio-button UI but was excluded from the keyboard cycle.
This is the same debug-path drift pattern previously noted in Phase 5 reviews — the
modulus and the mode name array were not updated when mode 6 was added.

**Fix applied:**

```cpp
radianceVisualizeMode = (radianceVisualizeMode + 1) % 7;
const char* radModes[] = { "Slice", "MaxProj", "Avg", "Atlas", "HitType", "Bin", "Bilinear" };
```

Mode 6 is now reachable via both the radio button and `[F]`.

---

## Finding 2 — High: Clamp invariant claim is false at the low edge

**Accepted.** The document claimed:
> "At the tile boundary, `b11` is clamped to `D-1` too. Both reads hit the same border bin."

This is only true at the **high edge**. The reviewer's counterexample is correct:

At `oct = 0.0`: `octScaled = -0.5` → `floor(-0.5) = -1` → `clamp(-1, 0, D-1) = 0`
→ `b00 = 0`, `b11 = clamp(1, 0, D-1) = 1`, `f = fract(-0.5) = 0.5`

Result: blends bin 0 with bin 1 at 50/50 — not clamp-to-edge.

The actual boundary behavior is asymmetric:
- **High edge** (oct → 1.0): `b00 = b11 = D-1`. Both reads hit the same border bin.
  Pure border-bin value regardless of `f`. GL_CLAMP_TO_EDGE semantics. ✓
- **Low edge** (oct → 0.0): `b00 = 0`, `b11 = 1`, `f = 0.5`. Equal blend of first two
  interior bins. Not clamp-to-edge, but all indices stay within `[0, D-1]`.
  No cross-probe reads possible. ✓

**The shader is not wrong** — the cross-probe contamination invariant holds at both
edges, which is the load-bearing property for GL_NEAREST correctness. But the document's
GL_CLAMP_TO_EDGE claim was factually incorrect for the low edge. The practical impact
is minimal (`dirToOct` of any real normalized direction rarely reaches exactly 0.0),
but the claim was still wrong.

**Doc updated** to accurately describe both edges separately.

---

## Finding 3 — Medium: Debug shader section describes wrong implementation

**Accepted.** The document said:
> "The debug shader needed `dirToOct`/`binToDir` functions mirrored from the compute shader. Only `dirToOct` is used by mode 6."

Neither claim matches the code. Mode 6 in `radiance_debug.frag` computes:

```glsl
vec2 octScaled = vec2(uAtlasBin) + 0.5;
```

It does not call any octahedral helper. The `dirToOct_dbg` and `octToDir_dbg` functions
that were added are dead code — mode 6 only needs the raw integer bin coordinates to
construct the bilinear offset, so no `dirToOct` conversion is needed.

**Fix applied:** Removed both dead helper functions (`dirToOct_dbg`, `octToDir_dbg`)
from `radiance_debug.frag`. **Doc updated** to correctly describe that mode 6 works
directly from `uAtlasBin` without any octahedral function calls.

---

## Finding 4 — Medium: Build status internally inconsistent

**Accepted.** The header said "compile-verified" while the validation table said
"Build: 0 errors | Pending". These are contradictory; the table entry reflects the
actual state (not yet built and run).

**Doc updated:** Header now reads "Implemented, diff-verified. Build and runtime A/B
pending." The distinction between diff-verified (code changes match intent, reviewed
against source) and compile-verified (compiler confirmed 0 errors) is maintained going
forward.

---

## Summary of changes applied

| Finding | Action |
|---|---|
| F1: `[F]` cycle excluded mode 6 | Code fix: `% 6` → `% 7`, added "Bilinear" to mode name array in `demo3d.cpp:318-320` |
| F2: Clamp invariant doc wrong at low edge | Doc fix: rewrote clamp section to accurately describe asymmetric high/low edge behavior |
| F3: Dead helper functions in debug shader | Code fix: removed `dirToOct_dbg` and `octToDir_dbg` from `radiance_debug.frag`; doc updated |
| F4: Status inconsistency | Doc fix: header changed from "compile-verified" to "diff-verified" |

The two code fixes are factual bugs — mode 6 was silently unreachable via keyboard,
and the debug shader carried dead code. Both are now resolved. The doc corrections
remove two false claims that would have misled a future reader auditing the shader math.
