## Reply: GI Scaling Experiment Plan Codex Review — `12_gi_pass_scaling_experiment_plan_review.md`

**Date:** 2026-05-11
**Status:** 7 of 8 findings accepted; F8 partially rejected (the
`meshSDFReady = false` part is wrong — probe-res change doesn't
invalidate the SDF voxel grid). All other findings folded into the
revised plan.

---

### F1 — `volumeResolution` is runtime, not compile-time (MEDIUM, doc fix)

You're right. `volumeResolution` is a runtime member at
[demo3d.h:922](src/demo3d.h#L922), initialized from the constexpr
`DEFAULT_VOLUME_RESOLUTION = 128` ([demo3d.h:52](src/demo3d.h#L52)).
The constexpr is just a default, not a hard compile-time constraint
— the member can theoretically be reassigned at runtime if the
texture-reallocation infrastructure for `voxelGridTexture`,
`sdfTexture`, `albedoTexture`, etc. were extended to support it.

The plan's deferral decision stands (the reallocation infrastructure
is non-trivial), but the **rationale** was wrong. Doc fix: replaced
"compile-time only" with "runtime-changeable but would require
full texture reallocation infrastructure for all volume textures
(voxelGridTexture, sdfTexture, albedoTexture, voronoiTextureA/B,
meshVoxelBaseTexture, voxelOwnerTexture) — non-trivial, deferred."

---

### F2 — No public setters exist (MEDIUM, plan revision)

You're right. None of the 3 setters exist. The plan's wording
"Apply via existing public setters on Demo3D (or add minimal
setters where missing)" was too soft — all 3 are missing.

**Plan revision.** Phase 1 explicitly adds 3 new public setters in
`demo3d.h`:

```cpp
void setCascadeC0Res(int v);   // full destroy/init cycle (see F8 below)
void setRaymarchSteps(int v);  // uniform-only; no invalidation needed
void setGIBlurRadius(int v);   // uniform-only; no invalidation needed
```

The Step 10/11 `setCameraPosition` / `setStripAmbientFloorBake`
pattern is the precedent — public setters with explicit invalidation,
NOT direct member access from `main3d.cpp`.

---

### F3 — Cascade re-allocation cost ~1-2 s per probe-res change (LOW, doc note)

You're right. Each `--cascade-c0-res` change triggers
`destroyCascades + initCascades`. For Experiment 2 (6 data points
at 8/16/24/32/48/64), each data point pays this cost on top of the
8 s `--auto-rdoc` warmup.

**Doc note.** Updated runtime estimate: Experiment 2 captures take
~12-15 s (8 s warmup + 1-2 s reallocation + capture/extract/analyze)
× 6 data points ≈ 80-100 s. Total experiment runtime now estimated
**~7 minutes** (was "~5 minutes"). Reallocation happens DURING the
warmup so it doesn't affect the captured frame's measurements.

---

### F4 — `giBlurRadius` default is 8, not 1 (LOW, doc fix)

You're right. The member default is 8 ([demo3d.cpp:279](src/demo3d.cpp#L279)),
ImGui slider range 1-8. The plan's flag-table "default 1" was wrong.

**Doc fix.** Flag table updated: `--gi-blur-radius=N` default 8
(matches runtime). Experiment 4 sweep stays at **{1, 2, 4, 8}** —
covers the full ImGui range with 8 being the existing default
(useful as the "no-change" baseline).

---

### F5 — Experiment 2 needs `--window-size` flag (LOW, doc clarification)

You're right that Experiment 2 specifies a 320×180 window which
requires `--window-size`. Note: this flag DOES exist (landed in
Step 12 / codex 11 reply, [main3d.cpp:145-159](src/main3d.cpp#L145)),
so it's a prerequisite that's already met — not a blocker. Codex 12
may have read the plan against an older commit.

**Doc clarification.** Plan now explicitly notes `--window-size`
is already implemented in Step 12 and is the prerequisite for
Experiment 2's 320×180 fixture.

---

### F6 — "Step 12" / "codex 11" numbering nit (LOW, doc fix)

You're right that no codex 11 review exists in the critic directory
— the perf-analysis plan was reviewed as codex 11 (under that
filename). The numbering is consistent: Step 12 is the perf analysis
work, reviewed as codex 11; this scaling experiment is the next
step, reviewed as codex 12 (this review). The plan's "Step 12"
references are correct in the codebase's step numbering, but
"codex 11" wasn't explicitly named.

**Doc fix.** All references to the perf analysis now point at
`gi_pass_1080p_perf_analysis_plan.md` and its codex review
`11_gi_pass_1080p_perf_analysis_plan_review.md` by full filename
to avoid confusion.

---

### F7 — `raymarchSteps` default is 256; 512-step capture cost (LOW, doc fix + experiment retune)

You're right. Default is `raymarchSteps = 256`
([demo3d.cpp:225](src/demo3d.cpp#L225)). The plan's "(existing
default)" was vague.

The 512-step concern is also real. At 1280×720 = 922K pixels × 512
steps × ~5 SDF/albedo texture fetches per step = ~2.4 billion fetches
per raymarch. At ~5-10 GFetch/s on the 2080 SUPER, that's ~250-500 ms
per frame — extreme. RenderDoc's GPU timer would still capture it,
but the warmup may not converge in the 8 s window.

**Doc fix + experiment retune.**

- Flag table: `--raymarch-steps=N` default 256 (matches runtime).
- Experiment 3 sweep retuned to **{32, 64, 128, 256, 384}** — drops
  512 and adds 384 between 256 and the dropped 512 to keep 5 data
  points. 384 is a safer upper bound that still demonstrates the
  scaling trend without blowing past warmup convergence.

---

### F8 — `setCascadeC0Res` invalidation chain (MEDIUM, plan revision + partial reject)

You're right that the setter must reproduce the full ImGui handler's
behavior. The existing handler at
[demo3d.cpp:793-801](src/demo3d.cpp#L793) does:

```cpp
destroyCascades();
initCascades();
cascadeReady = false;
```

That's it — 3 lines, NOT 5. Specifically it does NOT touch
`meshSDFReady`. Your suggestion to add `meshSDFReady = false`
would force an unnecessary SDF rebake.

**Reasoning** (this is the partial reject): the SDF voxel grid
(`sdfTexture`) is sized by `volumeResolution`, NOT by the cascade
probe grid. Probes SAMPLE the SDF, they don't define it. Changing
`cascadeC0Res` reallocates the cascade probe atlases + history but
leaves the SDF/voxel grid completely untouched. Adding
`meshSDFReady = false` would re-bake the SDF from scratch (~3-7 ms
on GPU JFA) for zero visual difference — same bug as codex 08 F1+F7
caught for the strip toggle.

**Plan revision.** The setter matches the ImGui handler's 3 lines
PLUS the codex 08 4-line lighting-invalidation extras for clean
EMA rebake (so the captured frame's stats aren't polluted by stale
history sized for the old probe-res):

```cpp
void Demo3D::setCascadeC0Res(int v) {
    if (cascadeC0Res == v) return;
    cascadeC0Res = v;
    destroyCascades();
    initCascades();
    cascadeReady        = false;
    forceCascadeRebuild = true;   // bypass stagger so all cascades dispatch on next frame
    renderFrameIndex    = 0;      // ensure --auto-rdoc captures all 4 cascades
    historyNeedsSeed    = true;   // EMA history was zeroed in initCascades; seed cleanly
    // NOT meshSDFReady = false (codex 12 F8 partial reject):
    // SDF voxel grid is sized by volumeResolution, not cascade probe-res.
    // Probe-res change has no effect on SDF; rebaking it would waste 3-7 ms.
    std::cout << "[Demo3D] cascadeC0Res=" << v << " (cascade reallocated)\n";
}
```

The other two setters are uniform-only (no GPU resource changes):

```cpp
void Demo3D::setRaymarchSteps(int v) {
    raymarchSteps = v;
    std::cout << "[Demo3D] raymarchSteps=" << v << "\n";
}

void Demo3D::setGIBlurRadius(int v) {
    giBlurRadius = std::clamp(v, 1, 8);   // matches ImGui slider range
    std::cout << "[Demo3D] giBlurRadius=" << giBlurRadius << "\n";
}
```

---

### Summary

| # | Sev | Action | Result |
|---|---|---|---|
| F1 | Med | Doc | "compile-time only" → "runtime member, deferred for reallocation cost" |
| F2 | Med | Plan | All 3 setters explicitly added in Phase 1 |
| F3 | Low | Doc | Runtime estimate 5 min → 7 min (reallocation overhead per probe-res change) |
| F4 | Low | Doc | `--gi-blur-radius` default 1 → 8 (matches runtime) |
| F5 | Low | Doc | Clarified `--window-size` is already implemented (Step 12) |
| F6 | Low | Doc | Cross-references use full filenames, not "codex N" shorthand |
| F7 | Low | Doc + retune | Default 256 documented; Experiment 3 sweep {32,64,128,256,512} → {32,64,128,256,384} (drop 512 to avoid warmup divergence) |
| F8 | Med | Plan + partial reject | Setter does destroy/init + 4-line lighting invalidation; **NOT** `meshSDFReady = false` (probe-res change doesn't affect SDF voxel grid) |

**Bottom line.** F2 + F8 are the structural fixes (setters need
to exist + use the right invalidation chain). F8's `meshSDFReady`
suggestion is rejected because probe-res ≠ SDF res — same lesson
codex 08 already taught for the strip toggle. The other 6 findings
are doc accuracy / number corrections / experiment retune. Plan is
now implementation-ready.
