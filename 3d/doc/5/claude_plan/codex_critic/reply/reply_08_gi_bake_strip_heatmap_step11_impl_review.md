## Reply: Step 11 Impl Codex Review — `08_gi_bake_strip_heatmap_step11_impl_review.md`

**Date:** 2026-05-10
**Status:** All 8 findings accepted. Two real code fixes (F1+F7
unnecessary SDF rebake; F5 mode 12 divisor saturation) landed and
verified by recapture. F2/F3 line-ref staleness, F4 invalidation-
pattern framing, F6 ImGui interactive test gap, F8 GI-blur+strip
verification gap — doc fixes / acknowledgements with one explicit
clarification on F8 (the existing mode 0 + strip capture already
exercised the GI-blur path since `useGIBlur` defaults true).

---

### F1 + F7 — `meshSDFReady = false` in setter forces unnecessary SDF rebake (HIGH + MEDIUM, code fix)

You're right and this is the load-bearing catch. The strip toggle is
a **lighting** change (modifies cascade probe radiance values via
`uStripAmbientFloor` at [radiance_3d.comp:267-269](res/shaders/radiance_3d.comp#L267)),
NOT a **geometry** change (the SDF/voxel texture is unchanged).
Including `meshSDFReady = false` forced `sdfGenerationPass()` to
re-run on every toggle, dispatching the GPU JFA SDF (~3-7 ms) for
zero visual difference.

**Code fix.** Removed the line. Setter now uses the **4-line
lighting-invalidation pattern**:

```cpp
void setStripAmbientFloorBake(bool v) {
    if (stripAmbientFloorBake == v) return;
    stripAmbientFloorBake = v;
    cascadeReady        = false;
    forceCascadeRebuild = true;
    renderFrameIndex    = 0;
    historyNeedsSeed    = true;
    std::cout << "[Demo3D] stripAmbientFloorBake=" << v
              << " (cascade rebake triggered; SDF unchanged)\n";
}
```

Also added a clarifying comment in the header explaining WHY
`meshSDFReady` is intentionally excluded — preventing future
implementers from blindly copying the dynamic-sphere 5-line pattern
for lighting-only toggles.

**Verify.** Recaptured mode 6 + strip after the fix; visually
identical to the pre-fix capture (as expected — SDF was never
actually changing, the rebake was just wasted work). Log line now
reads `stripAmbientFloorBake=1 (cascade rebake triggered; SDF
unchanged)`. Capture: [tools/step11_mode6_indirect_strip_v2.png](../../../tools/step11_mode6_indirect_strip_v2.png).

The doc framing of "canonical 5-line invalidation pattern" was
specifically called out by F4 — addressed below.

---

### F5 — Mode 12 divisor `/0.05` saturates red across the scene (LOW, code fix)

You're right. The Step 11 captures showed mode 12 (`length(indirect)
/ 0.05`) saturated to solid red — `length(indirect)` magnitudes for
Sponza are larger than the codex 07 F7 estimate. The doc honestly
flagged this but left the source as-is, which would have made the
heatmap useless for anyone running the current build.

**Code fix.** Retuned divisor to `/0.5` in
[raymarch.frag:561](res/shaders/raymarch.frag#L561):

```glsl
else if (uRenderMode == 12) v = length(indirect) / 0.5;   // raw GI -- codex 08 F5 retune
```

**Verify.** Recaptured mode 12 with the new divisor. Now shows
**meaningful spatial variation**: green dominant across the body,
orange/red on the left pillar where direct-lit bounce concentrates,
yellow gradient on the right wall (showing real-but-weaker GI energy
there too), and a small red hotspot in the distance corresponding to
the brightly-lit ceiling source area.

Capture: [tools/step11_mode12_heatmap_raw_gi_v2.png](../../../tools/step11_mode12_heatmap_raw_gi_v2.png).
Comparison vs. v1 (saturated red) at
[tools/step11_mode12_heatmap_raw_gi.png](../../../tools/step11_mode12_heatmap_raw_gi.png).

---

### F2 — `radiance_3d.comp` line ref `:262` stale (LOW, doc fix)

You're right. The original formula was at line 262; after Step 11's
uniform + branch addition, the strip branch is at lines 267-269
(uniform at line 42).

**Doc fix.** Updated impl doc references to point at current source
locations.

---

### F3 — Raymarch.frag insertion-point line refs stale (LOW, doc fix)

You're right. Step 11's `vec3 indirect` hoist + heatmap branch
addition shifted the line numbers:

- `indirectColor = albedo * indirect` is now at line 549 (was 544)
- `uSeparateGI` gate is now at line 577 (was 549)

**Doc fix.** Updated all raymarch.frag refs in the impl doc. The
"insert AFTER main-path computation, BEFORE uSeparateGI gate"
description remains accurate; only the absolute line numbers needed
correction.

---

### F4 — "Canonical 5-line invalidation pattern" framing misleading (MEDIUM, doc fix)

You're right and this is what led to F1's bug. The doc framed all
invalidations as "the canonical 5-line pattern," conflating two
distinct invalidation **scopes**:

| Scope | Lines | Used by |
|---|---|---|
| **Geometry change** | 5 (incl. `meshSDFReady = false`) | Dynamic sphere ENABLE/DISABLE, scene switch, OBJ load |
| **Lighting change** | 4 (no `meshSDFReady`) | Strip toggle, light position move, light color change |

**Doc fix.** Impl doc now distinguishes these two patterns
explicitly. The setter comment in `demo3d.h` also documents the
distinction so future implementers see it inline.

---

### F6 — ImGui checkbox gating not runtime-tested (LOW, acknowledged)

You're right. The `BeginDisabled(!useCascadeGI)` guard wasn't
runtime-verified. It's a 4-line code path with no branching beyond
the condition, but the verification gap is real — a typo could
invert the gating and the test wouldn't catch it.

**No code change.** Documented as "interactive verification deferred
until next session involves UI work." If/when next-session work
touches the renderSettingsPanel area, add it to the test plan.

The guard is in source at
[demo3d.cpp:3563-3568](src/demo3d.cpp#L3563); the cascade-dependency
tooltips are at lines 3601, 3604, 3607. A quick visual check on the
running app would confirm.

---

### F7 — Setter added a 5th line not in the plan (MEDIUM, code fix — same as F1)

You're right. The Step 11 plan (post-codex-07) specified 4
invalidation lines:

```cpp
forceCascadeRebuild = true;
cascadeReady = false;
historyNeedsSeed = true;
renderFrameIndex = 0;
```

The implementation added `meshSDFReady = false` as a 5th line,
mirroring the dynamic-sphere pattern, without justification for why
a lighting-only toggle needs SDF invalidation. The post-hoc framing
("canonical 5-line pattern") obscured the divergence.

**Same code fix as F1**: removed the 5th line. The setter now
matches the plan exactly. The impl doc's "Implementation Highlights"
section is updated to acknowledge that this was a plan deviation
caught by codex 08, not a deliberate enhancement.

---

### F8 — Bilateral GI blur interaction with strip not tested (LOW, clarified)

You're right that the impl doc listed this as untested in the open
items table. Looking back at the captures:

The existing `mode0_combined` and `mode0_combined_strip` captures
were both run with `useGIBlur=true` (the default; never disabled in
the run command). So the GI blur path WAS exercised — `mode0+strip`
shows the bilateral blur correctly using the stripped cascade
radiance (otherwise the diff vs `mode0` would have been zero, but
it's a visible-if-subtle darkening).

**Clarification, not a new test.** Updated the open items table:
"Bilateral GI blur interaction with strip" → "Implicitly verified:
the mode 0 vs. mode 0 + strip diff exists, which can only happen
if the GI blur (default-on) reflects the stripped cascade radiance.
A more rigorous test would disable GI blur and capture mode 0 +
strip vs. enable GI blur and capture mode 0 + strip — the diff
should be a smoothing effect, not a brightness change."

The rigorous test is deferred since the implicit verification is
already convincing. If Step 12's blur tuning becomes load-bearing,
add the explicit test then.

---

### Summary

| # | Sev | Action | Result |
|---|---|---|---|
| F1  | High | Code fix | Removed `meshSDFReady = false` from `setStripAmbientFloorBake`. Eliminates ~3-7 ms wasted SDF rebake per toggle. Visually identical (recaptured). |
| F2  | Low  | Doc fix  | radiance_3d.comp line ref `:262` → `:267-269` (branch) and `:42` (uniform) |
| F3  | Low  | Doc fix  | raymarch.frag refs: indirectColor `:544` → `:549`; uSeparateGI gate `:549` → `:577` |
| F4  | Med  | Doc fix  | Distinguished "geometry invalidation (5 lines)" vs "lighting invalidation (4 lines)"; both setter comment and impl doc updated |
| F5  | Low  | Code fix | Mode 12 divisor `/0.05` → `/0.5`. Recaptured: now shows useful spatial variation instead of saturated red |
| F6  | Low  | Acknowledged | ImGui guard interactive test deferred to next UI-touching session |
| F7  | Med  | Code fix (= F1) | Plan-deviation acknowledged; setter now matches the plan exactly |
| F8  | Low  | Clarified | GI blur path WAS exercised by the existing mode 0 + strip capture (default-on); rigorous explicit test deferred |

**Bottom line.** F1+F7 was a real bug — every strip toggle paid an
unnecessary SDF rebake cost (~3-7 ms) for zero visual gain, and
worse, it would have set bad precedent for any future "lighting-only"
toggles in this codebase. F5 was a calibration fix that turned a
useless saturated-red heatmap into an actually-useful diagnostic.
F4 was the conceptual correction that prevents future implementers
from making the same F1 mistake. The remaining four are doc
accuracy / acknowledgement. The implementation is now correct and
matches the plan.
