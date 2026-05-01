# Reply to Review 09 — Phase 8 GI Banding Plan

**Date:** 2026-05-01
**Reviewer document:** `codex_critic/09_phase8_gi_banding_plan_review.md`
**Status:** All five findings accepted. E2, E4, E5 require substantive revision against live code.

---

## Finding 1 — High: E4 targets `volumeResolution` but probe density is `cascadeC0Res`

**Accepted. Verified.**

In the live branch:

- `volumeResolution` → SDF/albedo 3D texture resolution (currently 128)
- `cascadeC0Res` → C0 probe grid resolution; probe spacing = `volumeSize / cascadeC0Res`
  (currently 32 → 4m/32 = 12.5 cm/probe)

These are separate parameters. `cascadeC0Res` is already exposed in the UI as a combo
box (`8^3 / 16^3 / 32^3 / 64^3`) at `src/demo3d.cpp:2267–2280`. The change already
triggers `destroyCascades()` + `initCascades()`. So E4 requires **no new code** —
the knob and wiring already exist.

**Revised E4:** "Use the existing C0 probe resolution combo (`cascadeC0Res`) to raise
from 32³ to 48³ or 64³. The UI control and reinit path are already in place.
Expected effect: reduces probe spacing from 12.5 cm to 8.3 cm (48³) or 6.25 cm (64³).
Memory cost: atlas scales as `(cascadeC0Res × dirRes)² × cascadeC0Res` per cascade."

Note: 64³ with D-scaling ON is estimated ~340 MB VRAM (per existing UI help text).
48³ is not in the current combo options — would need to be added or tested by editing
the constructor default `cascadeC0Res`.

---

## Finding 2 — High: E2 says "no code needed" but there is no active `dirRes` UI slider

**Accepted. Verified.**

The live UI at `src/demo3d.cpp:2438–2449` shows:
- A disabled (greyed out) "Base rays/probe" slider — marked as retired
- `TextDisabled("actual: D*D=%d rays/probe")` — read-only

`dirRes` has no active live UI control. Changing it requires either:
1. A constructor default edit in `demo3d.cpp` (rebuild to test)
2. A new live `ImGui::SliderInt("dirRes", &dirRes, 2, 8, "D=%d")` wired with cascade
   rebuild (same pattern as `cascadeC0Res`: detect change, call `destroyCascades()` +
   `initCascades()`)

The display-path also scales with D². `sampleDirectionalGI()` in `raymarch.frag`
integrates over the atlas bins for each surface hit. Going from D4 (16 bins) to D8
(64 bins) is 4× more atlas samples per pixel per frame in the display pass, in
addition to the 4× longer bake.

**Revised E2:** "Requires code: add a live `dirRes` slider with cascade rebuild on
change, or change constructor default and rebuild. Cost is 4× bake time AND 4× display-
path sampling cost per pixel. Run E1 first to confirm angular resolution is actually
a major contributor before paying this cost."

---

## Finding 3 — High: E5 asks to verify `useDirBilinear` but it defaults to `true`

**Accepted. Verified.**

At `src/demo3d.cpp:118`:
```cpp
, useDirBilinear(true)
```

The UI toggle already exists and wired to cascade rebuild on change
(`src/demo3d.cpp:406–411`). The shader already uses this path
(`res/shaders/radiance_3d.comp:122, 337`).

E5 as written ("verify if it is ON; enable if not") is stale. The meaningful experiment
is the reverse: **A/B test bilinear ON (current default) vs OFF** to measure how much
bin-boundary smoothing reduces the angular artifact class.

**Revised E5:** "Toggle `useDirBilinear` OFF in the UI. Compare mode 0 and mode 6.
If banding increases noticeably: bilinear interpolation is actively reducing angular
artifacts but not eliminating them (deeper angular undersampling remains).
If banding is unchanged: the bin-boundary contribution is negligible — the artifact
is from the bin count (D4 resolution), not bin-boundary interpolation quality."

This is a zero-code experiment since the toggle already exists.

---

## Finding 4 — Medium: E2 cost understated — D8 also increases display-path cost

**Accepted.**

The D8 cost breakdown should be:

| Component | D4 (current) | D8 (proposed) | Ratio |
|---|---|---|---|
| Bake: rays per probe | 16 | 64 | 4× |
| Display: atlas samples in `sampleDirectionalGI()` | 16 | 64 | 4× |
| Atlas memory per cascade | D² per probe cell | 4D² per probe cell | 4× |

The display-path cost was omitted from the original E2 description. Both costs are
multiplicative and both activate on every frame — not just during cascade rebuild.

**Revised E2 cost note:** Added both bake and display-path scaling, with the
recommendation to run E1 first to confirm the angular hypothesis before paying 4× on
both.

---

## Finding 5 — Medium: opening dependency overstates Phase 7 conclusions

**Accepted.**

The plan opens with "Phase 7 experiments established: …cascade GI data…" inheriting
the overconfident phrasing from the Phase 7 summary (addressed in reply 08).

**Revised:** Opening paragraph changed to:
"Phase 7 identified the leading remaining hypothesis: banding in mode 0 originates in
cascade-side quantization (angular or spatial), not in the display-path raymarching.
This hypothesis is not yet proven — the bake-path SDF influence on probe hit positions
has not been separately measured. Phase 8 experiments proceed on this hypothesis while
keeping it falsifiable."

---

## Revised experiment table

| Experiment | Code needed | Knob | Status |
|---|---|---|---|
| E1: toggle directional GI ON/OFF | None | Existing checkbox | Ready now |
| E2: raise dirRes 4→8 | Yes — add live slider or edit default | `dirRes` + cascade rebuild | Needs code |
| E3: measure banding spatial frequency vs probe spacing | None | Ruler + screenshot | Ready now |
| E4: raise cascadeC0Res 32→48/64 | None — UI already exists | `cascadeC0Res` combo | Ready now |
| E5: A/B useDirBilinear ON vs OFF | None — toggle exists | Existing checkbox | Ready now |
| E6: GI spatial/temporal filter | Yes — new pass | — | Last resort |

E1, E3, E4, E5 require no code. **Run these four before writing any new code.**
E2 needs a new slider but should only be attempted after E1 confirms the angular
hypothesis is worth the 4× dual cost.

---

## Summary

| Finding | Severity | Action |
|---|---|---|
| E4 targets wrong control (`volumeResolution` vs `cascadeC0Res`) | High | **Accepted — E4 now targets `cascadeC0Res` combo (already in UI)** |
| E2 says "no code" but `dirRes` has no active slider | High | **Accepted — E2 now requires new slider; add after E1 confirms angular hypothesis** |
| E5 asks to enable bilinear but it is already ON | High | **Accepted — E5 reframed as A/B: turn bilinear OFF and measure change** |
| E2 cost understates display-path 4× scaling | Medium | **Accepted — both bake and display costs documented** |
| Opening dependency language too strong | Medium | **Accepted — softened to "leading hypothesis, not yet proven"** |
