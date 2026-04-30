# Reply to Review 13 — Phase 5h Implementation Learnings

**Date:** 2026-04-29
**Reviewer document:** `13_phase5h_impl_learnings_review.md`
**Status:** Both findings accepted. F1 requires a live UI text fix and doc update.
F2 requires a doc label correction only.

---

## Finding 1 — Medium: live HelpMarker text understates the difference from bake `inShadow()`

**Accepted.** The implementation note correctly described the normal-offset origin as
strictly better than the bake shader's fixed-bias approach. But the live UI text still
read:

```
"Uses the same SDF-march as the cascade bake inShadow()."
```

This is directionally true (both use the same `sampleSDF()` loop) but misleading about
the origin bias — the two implementations differ in a meaningful way. The final renderer
has the surface normal at the hit point; the bake shader does not. Using the normal to
push the origin clearly outside the surface is analytically superior to the fixed
`t=0.05` scalar offset. Saying "same SDF-march" collapses that distinction.

**Fix applied** (`src/demo3d.cpp:2247-2254`):

```cpp
HelpMarker(
    "ON  (default): casts a 32-step SDF shadow ray from each surface hit\n"
    "     toward the light. Uses a normal-offset origin (normal*0.02 +\n"
    "     ldir*0.01) -- better than the bake shader's fixed t=0.05 bias\n"
    "     because the surface normal is known in the final renderer.\n"
    "     Gives hard binary shadow in the direct term.\n\n"
    "OFF (Phase 1-4 baseline): no shadow check in direct path. ...");
```

**Doc updated** in `phase5h_impl_learnings.md` — the C++ changes table row now notes
"HelpMarker (updated in review 13 fix)" and a new "HelpMarker accuracy" subsection
records the original text and the correction for future reference.

---

## Finding 2 — Medium: "verified at runtime" stated with same confidence as code facts

**Accepted.** The line:

> "Verified at runtime — no false-positive shadow or acne visible."

presents an empirical manual observation at the same level of certainty as structural
code facts (zero-regression GLSL path, volume-exit condition, etc.). These are different
categories: the code facts are derivable from static analysis; the runtime quality claim
requires actually running the program in the right scene.

**Fix applied** in `phase5h_impl_learnings.md`:

> *Manual runtime observation*: no false-positive shadow or acne visible in the Cornell
> Box scene during implementation session. This is empirical, not statically verifiable
> from code alone — different scenes or SDF configurations may require tuning.

The factual content is preserved. The epistemic label is now accurate.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| Live HelpMarker understates difference from bake `inShadow()` | Medium | **Fixed**: HelpMarker updated to describe normal-offset origin; learnings doc updated |
| "Verified at runtime" stated as code fact | Medium | **Relabeled**: marked as manual runtime observation in learnings doc |

The core implementation — 32-step shadow ray with normal-offset origin, applied to
mode 0 and mode 4, display-path only, no cascade rebuild — is unchanged and correct.
