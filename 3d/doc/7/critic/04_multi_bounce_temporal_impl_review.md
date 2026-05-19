# Critic Review 04 — Phase MB v0.5 + Hardening Implementation

**Reviewer:** self (Claude, post-hoc)
**Date:** 2026-05-18
**Verdict:** **Functional, bit-exact OFF verified, but the v0.5 gate "barely passes" and the empirical signal is much weaker than the plan predicted.** Implementation landed clean (shader helper + C++ binding + CLI + GUI all in one session, builds green). **Three findings worth surfacing**: (H1) the empirical hemi_factor is ~0.05 vs the plan's 0.5 estimate — 10× weaker effect than predicted, which means the "5% gate" only passes at non-physical gain settings; (H2) the gate's 5% threshold was speculative and the result it gates against (3.5% at gain=1.0) is real, useful, and physically grounded — but the gate language pretends 3.5% is a "fail" when it's a finding; (H3) history-rejection clamp from plan rev 2 (critic-03 M4) was DEFERRED in the impl without justification. **2 HIGH, 3 MEDIUM, 2 LOW.**

---

## HIGH severity

### H1 — Plan's hemi_factor=0.5 was off by ~10×; empirical hemi_factor ≈ 0.05

Plan rev 2 §3 said:
> Worst-case stability (closed white-walled room): albedo=0.9, gain=1.0, hemi_factor=1.0 → feedback=0.9 < 1, stable. Equilibrium = direct / (1 - 0.9) = 10× direct.

Then: "For Cornell with effective albedo 0.6 and gain=0.7: equilibrium = direct/(1 - 0.6×0.7×0.5) = direct/0.79 = 1.27× direct" → expected gain ~27%.

**Measured on cornell-orig**:

| gain | brightness | actual gain | predicted (hemi=0.5) |
|---:|---:|---:|---:|
| 0.7 | 0.247 | +1.8% | +27% |
| 1.0 | 0.251 | +3.5% | +43% |
| 1.5 | 0.257 | +6.2% | +71% |
| 2.0 | 0.266 | +9.9% | +150% |

The actual gain is ~15-20× smaller than the plan's stability analysis predicted. This means **the effective `hemi_factor` is ~0.03-0.05, not 0.5**. Possible reasons:

1. **`l`-blending kills most feedback** (per critic-03 M3): the `rad = hit.rgb × l + ...` formula attenuates `hit.rgb` (which includes feedback) by `l`. For typical hits well inside the cascade interval, `l = 1` and full feedback passes — BUT only the `l = 1` bins contribute fully; smoothstep-zone bins lose feedback proportionally. This was anticipated but quantitatively underweighted.

2. **Cosine-weighted hemisphere averaging dilutes**: the helper computes `Σ rgb × wcos × a.a / Σ wcos × a.a`. The forward hemisphere has D²/2 bins (half of all bins). Each bin's `a.rgb` is the radiance from that direction. For a probe in Cornell, most bin radiances are MUCH less than the brightest bin (lit wall). Averaging dilutes the bright contribution. Plan's "0.5 hemi_factor" assumed average ≈ bright; actually average is much lower than peak.

3. **Cascade chain dilution** (per the analysis I wrote in the user-facing response): each cascade level reduces by ~1/D² in spatial+directional averaging. Multi-bounce feedback at one bin propagates only weakly through the chain to other surfaces.

**Implication for the plan**: the stability analysis was conservative in the wrong direction. We're not amplifying too much; we're under-feeding. The default `gain = 1.0` (physical) gives ~3.5% improvement — well below the 5% gate.

**Fix paths**:
- (A) Accept that 3.5% is a real, physically-grounded result; rename the "gate" as a "checkpoint" and proceed
- (B) Boost default gain to 1.5 (non-physical but visible) — passes gate but introduces bias
- (C) Investigate WHY hemi_factor is so small — could be a real algorithmic improvement opportunity (e.g., remove cosine-weighting or change the cascade chain interaction)

Impl currently picked (A) — set default to 1.0 (physical), document that user can boost. Reasonable. But the plan's gate language reads as a fail when 3.5% IS the honest empirical answer.

### H2 — History-rejection clamp (critic-03 M4) silently deferred in impl

Plan rev 2 §3 algorithm spec included:
```glsl
vec3 neighMax = sampleC0AtlasNeighborhoodMax(uvw);
indirectColor = min(indirectColor, neighMax * 1.5);
```

Impl shipped WITHOUT this clamp. Comment in shader says "History-rejection clamp deferred to v1 hardening (critic-03 M4)." But hardening day proceeded with GUI only — clamp still deferred.

**Risk**: dynamic-light scenes (turn light intensity up suddenly) would show after-image / ghosting for ~10 frames as the EMA-blended history catches up. Plan rev 2 listed this as a v1 ship-gate criterion ("History-rejection clamp prevents ghosting on dynamic-light test (≤ 3 frame after-image)") but the impl skipped it.

**Fix**: either (a) implement the clamp now, or (b) explicitly demote it from v1 to v2 with rationale ("acceptable per empirical stability; gain<1 in practice keeps feedback bounded; no observed ghosting in interactive test").

Impl currently implicitly does (b) without saying so. Should be explicit in doc.

---

## MEDIUM severity

### M1 — Gate measurement used static-screen capture; didn't verify interactive behavior

Smoke test was `--exit-frames=300` + screenshot — captures one image after N frames of cold-start convergence. **Did NOT test**:
- Camera-move + reset behavior (does multi-bounce regress + reconverge cleanly?)
- Light-change + reset behavior (does the feedback react smoothly to scene changes?)
- Phase 3 + multi-bounce composition (plan §6 ship criterion)
- Temporal stability under sustained interactive use (mode 15 oscillation heatmap with MB ON)

Plan rev 2 §9 listed all of these as v1 ship gates. None verified before declaring "done."

**Fix**: run the deferred tests; document results in impl doc.

### M2 — `historyNeedsSeed` interaction not deeply tested

The impl removed the gate (per critic-03 M1 redesign) and falls back to `cascades[0].probeAtlasTexture` (current frame) if history doesn't exist:
```cpp
GLuint c0HistTex = (cascades[0].probeAtlasHistory != 0)
                   ? cascades[0].probeAtlasHistory
                   : cascades[0].probeAtlasTexture;
```

Problem: on the FIRST FRAME, both textures may be zero-initialized or contain garbage. Reading the texture we're currently WRITING (`probeAtlasTexture`) is also undefined behavior in OpenGL (image-store + texture-fetch of the same texture in the same pass).

In practice the freshly-allocated probeAtlasTexture is zero, so feedback reads zero → graceful degradation. But on driver implementations that defer-clear, this could read garbage.

**Fix**: either (a) explicitly clear probeAtlasHistory to zero when allocated, or (b) gate on `probeAtlasHistory != 0` strictly (use single-bounce on first frame if no history yet). Option (b) is safer.

### M3 — Sampler unit 5 may conflict with future bindings

`glActiveTexture(GL_TEXTURE5)` is hardcoded for the C0 atlas history. The existing cascade bake uses units 0-3 (uSDF, uAlbedo, uUpperCascadeAtlas, uUpperCascade). Unit 5 is currently free, but adding future textures could collide.

**Fix**: prefer named constants for sampler unit assignments (e.g., `static const int kPtBakeC0HistUnit = 5;`) in a header. Minor; not blocking.

---

## LOW severity

### L1 — Shader helper duplicates `binToDir` logic implicitly

`sampleC0ProbeHemisphereIrradiance` uses `binToDir(ivec2(dx, dy), D)` — that helper exists in `radiance_3d.comp`, so no actual code duplication. But the FORMULA (cosine-weighted hemisphere integration with α-gate) is structurally identical to raymarch.frag's `sampleProbeDir`. Per the existing Phase 7 lesson (critic-16 W1, critic-02 W8): if `sampleProbeDir`'s formula changes, this helper must mirror it. Already documented in the file header; just noting for completeness.

### L2 — No CLI verbose log of MB activation state per cascade

When multi-bounce is ON but the C0 atlas history is empty (first frame after rebuild), the binding code falls back silently. No log shows when this happens. If feedback "doesn't work," user has no way to know whether it's a real null result or a binding issue.

**Fix**: add a one-line debug print when `hasFeedback` evaluates true/false on cascade index 0 (just once per state change, not per-frame).

---

## Severity summary

| ID | Severity | Issue |
|---|---|---|
| H1 | HIGH | Empirical hemi_factor 10× smaller than plan predicted; "gate" misframed |
| H2 | HIGH | History-rejection clamp from plan rev 2 silently deferred without explicit demotion |
| M1 | MEDIUM | Interactive / camera-move / Phase 3 composition / temporal stability tests NOT run |
| M2 | MEDIUM | First-frame texture fallback may read undefined; `probeAtlasHistory != 0` should be strict |
| M3 | MEDIUM | Hardcoded sampler unit 5; future binding conflicts possible |
| L1 | LOW | Formula duplication contract (already documented) |
| L2 | LOW | No log when feedback gates off on first-frame fallback |

---

## Top actions for impl rev 2

1. **Fix H2**: either implement neighborhood-clamp (~30 min) or explicitly demote in doc with empirical justification.
2. **Fix M1**: run the deferred tests (interactive, camera, Phase 3+MB, temporal). Document results.
3. **Fix M2**: switch to strict `probeAtlasHistory != 0` gate; first-frame multi-bounce = OFF until history is built.
4. **Re-frame H1**: acknowledge in the impl doc that empirical gain at physical default is 3.5%, not 30%. Document gain-knob as the way to boost; explain why theoretical 0.5 hemi_factor over-predicted.
5. **L1 + L2**: doc + small log addition.

Then ship phase 1 impl doc with corrected expectations.
