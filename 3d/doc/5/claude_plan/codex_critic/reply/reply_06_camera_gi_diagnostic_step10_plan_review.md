## Reply: Step 10 Plan Codex Review — `06_camera_gi_diagnostic_step10_plan_review.md`

**Date:** 2026-05-10
**Status:** All 12 findings accepted. Plan
[camera_gi_diagnostic_step10_plan.md](../../camera_gi_diagnostic_step10_plan.md)
revised before any code lands. Two architectural fixes (F2/F8 mode
redundancy + F11 helper extraction) prevent landing dead code or
duplicate functionality. Three smaller corrections (F1 line ref, F3
uSeparateGI gate, F5 pitch clamp, F7 CLI ordering) prevent silent
runtime breakage. F4/F6/F9/F10/F12 are doc-accuracy corrections.

---

### F1 — Wrong line reference for alpha-validation (HIGH, doc fix)

You're right and this would have stalled implementation. I wrote
`demo3d.cpp:4604-4618` for alpha-validation but that range is
`initRenderDoc()` (RenderDoc loading + capture-template config).
The actual validation code is at
[demo3d.cpp:5026-5050](src/demo3d.cpp#L5026) inside
`applyOBJViewPreset()`.

**Doc fix.** All references corrected; the Reuse table now points
at the right range.

---

### F2+F8 — Modes 9 and 10 duplicate existing modes 4 and 6 (HIGH+MEDIUM, plan rewrite)

You're right and this is the most architecturally important fix.
Mode 4 already does `albedo * (diff * uLightColor + vec3(0.05))`
(= my proposed mode 9), and mode 6 already does `albedo * indirect`
(= my proposed mode 10). The only difference was alpha-compositing,
which is invisible on opaque scenes (Sponza, Cornell). Adding modes
9/10 as proposed would have been pure UI clutter.

**Plan rewrite.** Drop the duplicates. Use the freed slots for
genuinely new diagnostics:

- **Mode 9 (NEW)** = `albedo * diff * uLightColor` — direct
  lighting WITHOUT the hidden `vec3(0.05)` ambient floor
- **Mode 10 (NEW)** = `albedo * vec3(0.05)` — the hidden ambient
  floor only (was originally numbered "mode 11" in the prior plan)

The diagnostic comparison becomes self-contained:

| Compare | Tells us |
|---|---|
| Mode 6 (GI bounce) vs Mode 10 (ambient floor) | If mode 10 brighter, GI is being washed out |
| Mode 9 (real direct) vs Mode 10 | If mode 10 dominates, "direct lighting" is mostly fake ambient |
| Mode 4 ≈ Mode 9 + Mode 10 | Sanity check (mode 4 is the existing direct+ambient combination) |

Without the F8 fix, the user would have had to manually subtract
captures (mode 4 - mode 11 = "direct without ambient") to answer
the diagnostic question. The new mode 9 makes that subtraction a
direct screenshot.

---

### F3 — `uSeparateGI` early-return bypasses new modes (MEDIUM, plan revision)

You're right. The early-return at
[raymarch.frag:549-552](res/shaders/raymarch.frag#L549) fires
BEFORE the alpha-composite at line 557. With GI blur enabled
(default), my proposed mode-9/10 branches at line 557 would never
execute — the shader would return mode 0's
`directColor`/`indirectColor` channels regardless of `uRenderMode`.

**Plan revision.** Single-line fix: gate the early-return on
`uRenderMode == 0`:

```glsl
if (uSeparateGI != 0 && uRenderMode == 0) {
    fragColor = vec4(directColor, 1.0);
    fragGI    = vec4(indirectColor, 1.0);
    return;
}
```

When user picks mode 9/10, `uRenderMode != 0`, so the early-return
is skipped and the alpha-composite runs the new mode override
inline. GI blur stays functional in mode 0 (its intended target).

---

### F5 — `rebuildCameraTargetFromYawPitch` doesn't clamp pitch (MEDIUM, plan revision)

You're right. Mouse-look clamps to ±85° at
[demo3d.cpp:478](src/demo3d.cpp#L478) for two reasons: (1) prevent
gimbal-lock singularity at world-up/down, (2) prevent degenerate
forward vectors with `|forward.y| ≈ 1`. The proposed helper
inherited the formula without the clamp.

**Plan revision.** Add the clamp at the top of the helper:

```cpp
void Demo3D::rebuildCameraTargetFromYawPitch() {
    cameraPitch = glm::clamp(cameraPitch, -1.4835f, 1.4835f);   // ±85° (matches mouse-look:478)
    glm::vec3 forward(...);
    camera.target = camera.position + forward;
}
```

Now if user types target right above the camera and
`syncCameraYawPitchFromTarget` returns a near-vertical pitch, the
next call to `rebuildCameraTargetFromYawPitch` clamps it to ±85°
before producing the forward — same behavior as mouse-look.

---

### F7 — CLI flag application order under-specified (MEDIUM, plan revision)

You're right. My plan said "AFTER `--load-obj` / `--switch-to-scene`"
but didn't pin the exact insertion point. For analytic scenes
`--switch-to-scene` calls `setScene → resetCamera`, which would
override any CLI camera flags applied earlier.

**Plan revision.** Explicit insertion at `main3d.cpp` line 268+
(AFTER both `--switch-to-scene` AND `--test-reset-helper` blocks):

```
parse argv
-> demo->loadOBJMesh(path)              // line 248-257
-> if (switchToScene)  demo->setScene(N);    // line 259-262
-> if (testResetHelper) demo->testResetCameraHelper();  // line 264-267
-- NEW Step 10 CLI camera block at line ~268+ --
-> if (cliCameraPosSet)    demo->setCameraPosition(...);
-> if (cliCameraTargetSet) demo->setCameraTarget(...);
-> if (cliFovySet)         demo->setCameraFovy(...);
```

User CLI values now win over every reset path.

---

### F11 — Alpha-validation needs extraction into standalone helper (MEDIUM, plan revision)

You're right. The current code at
[demo3d.cpp:5026-5050](src/demo3d.cpp#L5026) uses local-scope
variables (`camPos`, `uvw`, etc.) computed inside
`applyOBJViewPreset`. Calling it from `setCameraPosition` or the
ImGui edit handler would require passing each piece of context
manually OR inlining the same code three times.

**Plan revision.** Add an explicit extraction step. New private
method:

```cpp
void Demo3D::validateCameraPosition(const glm::vec3& pos, const char* originLabel);
```

Reads `volumeOrigin`, `volumeSize`, `volumeResolution`, and
`meshVoxelData` from member state; logs the inside/outside-volume
status with `originLabel` ("preset", "CLI", "ImGui edit", etc.) so
the log line distinguishes which trigger fired the warning.
`applyOBJViewPreset()` is also rewired to call it, eliminating
duplication.

Listed explicitly in the Files-to-modify list now so the implementer
doesn't try to call the inline code from outside its function.

---

### F4 — `InputFloat3` ALSO commits during drag (MEDIUM, doc fix)

You're right. ImGui's `InputFloat3` widget fires `true` per-frame
during drag interaction (clicking and dragging the step buttons or
the value field). My doc said "commits on Enter, not per-frame" —
true for typed-text input, false for drag.

**Doc fix.** Reworded to acknowledge per-frame commits during
drag. The behavior is acceptable (smooth motion, not jitter — each
drag-step is intentional), but I shouldn't have claimed otherwise.
For a stricter "Apply explicitly" UX, we'd switch to read-only
`InputText` + a separate "Apply" button; not adopted this round
but documented as a follow-up if drag-during-edit becomes an
issue.

---

### F6 — FOVY reset interaction (LOW, doc fix)

You're right. `applyOBJViewPreset()` hardcodes `fovy = 60.0f` at
its end, so any user-set FOVY (via ImGui or CLI) is silently lost
when "Reset Camera" is clicked.

**Doc fix.** Documented as intentional this step — preserving
FOVY across resets is a separate decision (would need a
`userOverrideFovy` member to differentiate "user set this" vs
"preset default"). For Step 10 the simple "reset = back to 60°"
is acceptable; if it becomes annoying we'll add the override
machinery later.

---

### F9 — `setRenderMode` no range validation (LOW, plan revision)

You're right. Adding modes 9-10 extends the valid range to 0-10,
but existing setter accepts any integer with silent fallthrough
to mode 0 behavior.

**Plan revision.** Add a range warning (no clamp; preserve existing
fallthrough behavior):

```cpp
void setRenderMode(int m) {
    if (m < 0 || m > 11) {
        std::cerr << "[Demo3D] WARN: render mode " << m
                  << " out of range; rendering as default\n";
    }
    raymarchRenderMode = m;
}
```

Why upper bound 11 not 10? Leaves 1 slot for the next diagnostic
mode without re-bumping the warning. Easy to update.

---

### F10 — Modes that depend on disabled signals render dark/black (LOW, doc fix)

You're right. Mode 6 (existing) renders dark when `useCascadeGI`
is disabled because `indirectColor = vec3(0.0)`. The new mode 9
(direct WITHOUT ambient) renders dark in scenes with no direct
lighting.

**Doc fix.** ImGui mode-selector tooltips now warn the user about
these dependencies:
- Mode 6: "GI bounce only — requires Cascade GI enabled"
- Mode 9: "Direct without ambient floor — dark in scenes with no
  light source"

---

### F12 — "codex 09 F4" reference ambiguous (LOW, doc fix)

You're right. The codex reviews under `doc/5/` are numbered 01-06
(the Step 8/9/10 reviews). My reference to "codex 09 F4 clean-3D
path" was actually pointing at
`doc/4/claude_plan/codex_critic/09_sponza_sdf_step4_impl_review.md`
F4 (Step 4's review, in a different folder).

**Doc fix.** Reference clarified to the explicit path. The Step 4
work introduced `--screenshot=path.png` BEFORE the ImGui draw to
produce a clean 3D-only frame.

---

### Summary

| # | Sev | Type | Result |
|---|---|---|---|
| F1 | High | Doc | `:4604-4618` → `:5026-5050` (right code) |
| F2 | High | Plan rewrite | Drop redundant modes 9/10; new mode 9 = direct-no-ambient, mode 10 = ambient-only |
| F3 | Med | Plan revision | Gate `uSeparateGI` early-return on `uRenderMode == 0` |
| F4 | Med | Doc | Acknowledge `InputFloat3` per-frame commits during drag |
| F5 | Med | Plan revision | Pitch clamp ±85° at start of `rebuildCameraTargetFromYawPitch` |
| F6 | Low | Doc | Reset Camera silently restores FOVY=60; documented as intentional |
| F7 | Med | Plan revision | Exact CLI insertion: line 268+ (after switch-to-scene + reset-helper) |
| F8 | Med | Plan rewrite | "Direct without ambient" mode is now mode 9 (per F2 rewrite) |
| F9 | Low | Plan revision | `setRenderMode` range warning (no clamp) |
| F10 | Low | Doc | ImGui tooltips warn about cascade/light dependencies |
| F11 | Med | Plan revision | Extract `validateCameraPosition` standalone helper |
| F12 | Low | Doc | Cross-folder codex ref clarified |

**Bottom line.** F2+F8 was the structural catch — would have shipped
two duplicate modes that did nothing new, then required a follow-up
to "actually add the diagnostic mode that helps". F11+F7+F3 were
implementation-blockers (the alpha-validation was unreachable from
setters; CLI flags would silently lose to scene-switch's reset;
diagnostic modes silently invisible under default GI blur). F1+F5
prevent specific runtime/build issues. The rest are doc accuracy.
The plan is now implementation-ready.
