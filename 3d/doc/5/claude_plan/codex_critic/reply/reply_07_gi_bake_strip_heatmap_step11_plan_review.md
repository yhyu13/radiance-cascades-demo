## Reply: Step 11 Plan Codex Review — `07_gi_bake_strip_heatmap_step11_plan_review.md`

**Date:** 2026-05-10
**Status:** All 11 findings accepted. Plan
[gi_bake_strip_heatmap_step11_plan.md](../../gi_bake_strip_heatmap_step11_plan.md)
revised before any code lands. Three substantive plan revisions
(F3 structural reframing, F4 mode-6-with-strip decision framework,
F6 cascade dependency guards) prevent implementer confusion and
diagnostic captures without context. The other eight are doc
accuracy / precision improvements (line refs, uniform location,
performance notes, normalization rationale, scope acknowledgements).

---

### F1 — Wrong line reference for `forceCascadeRebuild` pattern (LOW, doc fix)

You're right. I wrote `demo3d.cpp:4687` for the "Step 8 dynamic-sphere
path" but that line is inside the RenderDoc `TriggerCapture()` prep
block, not the dynamic-sphere logic. The actual canonical example is
at [demo3d.cpp:623-627](src/demo3d.cpp#L623) (the dynamic-sphere
ENABLE branch) and [demo3d.cpp:636-640](src/demo3d.cpp#L636) (the
DISABLE branch). Both set the same five-line invalidation:

```cpp
meshSDFReady        = false;
cascadeReady        = false;
forceCascadeRebuild = true;
renderFrameIndex    = 0;
historyNeedsSeed    = true;
```

**Doc fix.** Reference corrected to `:623-627` with the comment that
the same pattern is used by RenderDoc + dynamic-sphere paths.

---

### F2 — Heatmap palette line references shifted from Step 10 changes (LOW, doc fix)

You're right. Step 10 added ~12 lines around the alpha-composite
(`uSeparateGI` gate + mode 9/10 branches). The palette references in
my plan were against the pre-Step-10 source:

| Mode | Plan said | Actual (post-Step-10) | Shift |
|---|---|---|---|
| 5 | `:576-582` | `:588-594` | +12 |
| 7 | `:486-495` | `:489-495` | +3 |

The palette code itself is unchanged — only the line numbers shifted.

**Doc fix.** Updated to `:588-594` (mode 5) and `:489-495` (mode 7).

---

### F3 — "Early-returns matching mode 4/6/7 pattern" structurally misleading (MEDIUM, doc fix)

You're right and this would have actively misled the implementer.
Mode 4 ([raymarch.frag:514-526](res/shaders/raymarch.frag#L514)) and
mode 6 ([raymarch.frag:498-512](res/shaders/raymarch.frag#L498))
**compute their own lighting terms** (their own `lightDir4`/`diff4`,
their own `indirect6`) BEFORE the main-path `directColor` and
`indirectColor` are computed at lines 535/544. The new heatmap modes
11/12/13 are structurally **opposite**: they CONSUME the main-path
`directColor`, `indirectColor`, and the hoisted `indirect` because
they're diagnostic visualizations OF the main-path math.

If an implementer follows my "matching mode 4/6 pattern" wording,
they'll insert the heatmap branches near the existing mode 4/6
branches (line ~498-526), where `indirect` doesn't exist yet — and
the GLSL won't compile.

**Doc fix.** Reworded to:

> Insert as **early-returns AFTER the main-path lighting computation**
> (after line 544 where `indirectColor = albedo * indirect`, BEFORE the
> Step 10 uSeparateGI gate at line 549). Heatmap modes consume
> main-path `directColor`/`indirectColor`/`indirect`, unlike modes 4
> and 6 which compute their own.

The plan's GLSL snippet is unchanged — it was already correct, only
the contextual wording was wrong.

---

### F4 — No discussion of what mode 6 looks like with strip ON (MEDIUM, plan addition)

You're right. The whole point of the strip-toggle is the comparison
`mode 6 vs. mode 6 + strip`, but the plan doesn't say what each
outcome would mean. Without a decision framework, the captures are
just "two pictures" — no actionable next step.

**Plan addition.** Added a "Decision framework for mode 6 + strip
comparison" subsection:

| Outcome | Interpretation | Implied next fix |
|---|---|---|
| **A: Mode 6 + strip is nearly black** | GI bounce signal is entirely driven by the 0.05 ambient floor amplified through cascade source surfaces. Cascade math works but bounce energy is fake. | Remove the 0.05 floor from BOTH `radiance_3d.comp:262` AND `raymarch.frag:535`. Possibly boost `indirectBrightness` if real direct lighting alone is too dim. |
| **B: Mode 6 + strip shows visible colored bleed** | GI math produces real direct-lit bounce; the 0.05 in the bake is unwanted padding. | Remove the 0.05 ONLY from the bake (`radiance_3d.comp:262`); keep the local floor at `raymarch.frag:535` for ambient-occlusion-floor purposes, OR remove it too if mode 0 looks better without. |
| **C: Mode 6 ≈ Mode 6 + strip** | The 0.05 contribution to cascade probe radiance is dwarfed by real direct contribution. | Toggle is a no-op for this scene; revisit with darker scenes (e.g. reduced light intensity). |

Each outcome maps to a Step 12 decision. Without this framework the
captures sit ambiguous.

---

### F5 — No discussion of what mode 0 looks like with strip ON (LOW, plan addition)

You're right. Mode 0 with the strip ON is a **mixed state**:

- `directColor = albedo * (diff * uLightColor + vec3(0.05))` — local
  0.05 floor at `raymarch.frag:535` is **still applied**
- `indirectColor = albedo * indirect_strip` — bake's 0.05 is **gone**

So mode 0 + strip shows: real direct (with local floor) + real
GI-bounce (no source floor). Comparing this against vanilla mode 0
tells us whether removing JUST the bake-floor is enough to recover
the apparent uniformity, or whether the local floor at line 535
also needs removal. This is the data point that drives the Step 12
decision between "F4 outcome A" and "F4 outcome B".

**Plan addition.** Added a `mode0_combined_strip` capture to the
verification list (10th capture) with the explanation above. The
diff `mode0 vs. mode0+strip` quantifies the bake-floor's
contribution to the final composite.

---

### F6 — No `uUseCascade` dependency guard (MEDIUM, plan revision)

You're right. The toggle is meaningless and the heatmap modes 11/12
produce all-green-no-info output when cascade GI is disabled.

**Plan revision.** Two additions:

1. ImGui checkbox wrapped in `BeginDisabled(!useCascadeGI)`:
   ```cpp
   ImGui::BeginDisabled(!useCascadeGI);
   bool s = stripAmbientFloorBake;
   if (ImGui::Checkbox("Strip 0.05 ambient floor from GI bake", &s))
       setStripAmbientFloorBake(s);
   ImGui::EndDisabled();
   if (!useCascadeGI && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
       ImGui::SetTooltip("Disabled — requires Cascade GI to be enabled.");
   ```

2. Tooltips on heatmap mode RadioButtons (modes 11/12, mode 13 also
   needs cascade for the indirect term to be non-zero):
   - Mode 11/12: "Requires Cascade GI — output is all-green when disabled."
   - Mode 13: "Requires Cascade GI — fraction approaches 0 when indirect is 0."

The checkbox AND the tooltips land in the same patch as the new
ImGui block.

---

### F7 — `/0.5` heatmap normalization divisor not justified (LOW, doc fix)

You're right. I picked `0.5` arbitrarily. Looking at the Step 10
mode-6 capture for Sponza-master at the auto-fit angle: most of the
visible scene is mid-tone brown with a single bright orange rim on
the ceiling. The rim's `length(albedo * indirect)` is probably
~0.3-0.5 (visibly orange ≈ vec3(0.3, 0.15, 0.05) → length 0.34); the
body is essentially `length(albedo * 0.05) ≈ 0.04`. So a divisor of
`0.5` puts the rim at red and the body at solid green — losing all
spatial variation in the body.

**Doc fix.** Documented two normalizations to try:

- **Default `/0.1`** for visible-GI (mode 11) — body of Sponza falls
  in the green-yellow midrange; rim hits red. Better spatial
  contrast for diagnosing the body's GI contribution.
- **Default `/0.05`** for raw-GI (mode 12) — `length(indirect)` is
  smaller than `length(albedo * indirect)` since indirect alone is
  un-multiplied; tighter divisor preserves spatial variation.
- Mode 13 (fraction) is naturally [0,1], no divisor.

If the captures show all-green or all-red, swap the divisor. Listed
explicitly in the verification section as "if heatmap output is
saturated, retune the divisor" rather than promising the right
value upfront.

---

### F8 — Mode 13 division-by-zero guard fragile but acceptable (LOW, acknowledged)

You're right. The `0.001` threshold is acceptable for typical Sponza
mid-tone albedos (~0.5) where shadowed-region `total ≈ 0.025-0.05`.
For very dark albedos (~0.1), `total ≈ 0.005` — still above 0.001 —
but for surfaces with albedo ~0.01 (none in current assets) it
would fail.

**Acknowledged, no change.** Documented in the plan that the
threshold is "diagnostic-acceptable; only fails for near-zero
albedo which doesn't exist in current assets". If a future asset
hits this, raise the threshold to `0.0001` or use
`max(total, 0.0001)` as denominator.

---

### F9 — `uStripAmbientFloor` uniform declaration location unspecified (LOW, doc fix)

You're right. The plan said "add a uniform" but didn't pin where
in the 17-74 uniform block. Looking at semantic groupings in
`radiance_3d.comp`:

- 17-37: cascade geometry (probe count, scale, etc.)
- 38-42: lighting (`uLightPos`, `uLightColor`, `uUseEnvFill`,
  `uSkyColor`, `uBlendFraction`)
- 43-74: cascade behavior + soft shadow

**Doc fix.** Specified insertion **after `uLightColor` (line 39)**:

```glsl
uniform vec3  uLightPos;
uniform vec3  uLightColor;
uniform int   uStripAmbientFloor;   // Step 11: strip vec3(0.05) from probe bake
uniform int   uUseEnvFill;
```

Same group as the lighting uniforms, since stripping the floor IS a
lighting-formula modification.

---

### F10 — `renderFrameIndex = 0` triggers all cascades in one frame (LOW, doc note)

You're right. Setting `renderFrameIndex = 0` bypasses the per-cascade
stagger interval test (`interval = 1 << i; 0 % anything == 0` →
all 4 cascades dispatch on the next frame instead of the usual 1).

For Sponza at typical settings this is roughly 4× the per-frame
cascade dispatch cost on the rebake frame — ~25 ms instead of ~7 ms.
For an interactive toggle this is a one-frame stutter the user might
notice; for headless capture it's invisible (the capture frame
already pays full bake cost).

**Doc note.** Added to "Performance notes" subsection:

> The first frame after toggling will run ALL 4 cascade dispatches
> (renderFrameIndex=0 bypasses the stagger). Expect a ~25 ms spike
> vs. the ~7 ms steady-state on Sponza. Subsequent frames return to
> staggered cadence.

If this turns out to be a real interactive issue (it likely won't
be for a one-time diagnostic toggle), the fix is to spread the
rebake across 4 frames by NOT resetting renderFrameIndex — but the
EMA history would then briefly show the stale-cascade transition.
Acceptable for diagnostic; revisit if the toggle becomes
interactive-tunable.

---

### F11 — Toggle persistence across scene changes not discussed (LOW, doc note)

You're right. `stripAmbientFloorBake` is a `Demo3D` member, so it
persists across `setScene()` and `loadOBJMesh()` calls. If the user
enables it for Sponza diagnosis, then loads Cornell, the Cornell
cascades also bake without the 0.05 floor.

**Doc note.** Documented as **intentional**:

> The toggle persists across scene/OBJ changes — the next scene's
> cascades bake with whatever strip-state was last set. This is
> deliberate: a user diagnosing the floor's effect across multiple
> scenes wants the toggle stable. If unexpected, the user toggles
> the ImGui checkbox or omits `--strip-ambient-floor-bake` on
> relaunch.

A future "reset toggles on scene change" UX would be a separate
decision (would also affect `useGPUSDF`, `useGPUVoxelize`,
`useDirectionalGI`, etc. — a coordinated change, not piecemeal).

---

### Verification gaps — folded into the plan's Verification section

Codex 07's verification gap list (`Capture mode 0 with strip` /
`Document outcomes` / `Reference mode 3 for normalization` / `Gray
out checkbox when cascade off` / `Tooltips on heatmap modes` /
`Verify uniform location not -1` / `Measure rebake frame perf`)
all map to F4-F10 above. They're folded into the implementation
steps (UI guards, uniform validation print, capture list)
rather than living as a separate gaps subsection.

The new capture list:

1. mode0_combined (existing)
2. mode0_combined_strip (NEW — F5)
3. mode4_direct_with_ambient
4. mode6_indirect (existing GI)
5. mode6_indirect_strip (the F4 load-bearing comparison)
6. mode9_direct_no_ambient
7. mode10_ambient_only
8. mode11_heatmap_visible_gi
9. mode12_heatmap_raw_gi
10. mode13_heatmap_gi_fraction

10 captures total (was 9 in the original plan). The new mode0_strip
capture quantifies the bake-floor's contribution at the final
composite level.

---

### Summary

| # | Sev | Type | Result |
|---|---|---|---|
| F1  | Low  | Doc  | `demo3d.cpp:4687` → `:623-627` (correct dynamic-sphere canonical example) |
| F2  | Low  | Doc  | Mode 5 ref `:576-582` → `:588-594`; mode 7 `:486-495` → `:489-495` (post-Step-10 line shift) |
| F3  | Med  | Doc  | "matching mode 4/6 pattern" → "early-returns AFTER main-path lighting computation; CONSUMES directColor/indirectColor unlike modes 4/6 which compute their own" |
| F4  | Med  | Plan | Decision framework for `mode 6 vs. mode 6 + strip` (Scenarios A/B/C with implied Step 12 fixes) |
| F5  | Low  | Plan | Added mode0+strip capture; documented mixed-state semantics |
| F6  | Med  | Plan | ImGui checkbox `BeginDisabled(!useCascadeGI)`; tooltips on heatmap RadioButtons noting cascade dependency |
| F7  | Low  | Doc  | Default divisors changed to `/0.1` (mode 11) and `/0.05` (mode 12); mode 13 unchanged; "retune if saturated" verification step |
| F8  | Low  | Doc  | Acknowledged; no change (current threshold safe for current assets) |
| F9  | Low  | Doc  | Uniform declaration location pinned: after `uLightColor` (line 39 in `radiance_3d.comp`) |
| F10 | Low  | Doc  | Documented one-frame ~25 ms cascade dispatch spike on toggle (4× normal) |
| F11 | Low  | Doc  | Documented toggle persistence across scenes as intentional |

**Bottom line.** F3 was the implementation-blocker — "matching mode
4/6 pattern" would have sent the implementer to insert heatmap
branches at line 498 where `indirect` doesn't exist; correct
location is after line 544. F4 was the diagnostic-blocker — without
a Scenario A/B/C framework, the captures wouldn't tell us what to
do next. F6 was the UX-blocker — the toggle is meaningless when
cascades are off, and the heatmaps degenerate to all-green. The
remaining eight are accuracy fixes (line numbers, normalization
choice, performance/persistence acknowledgements). The plan is now
implementation-ready.
