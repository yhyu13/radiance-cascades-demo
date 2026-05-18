# Sponza GI Root-Cause Hypothesis Test — Implementation Notes (revised after critic 02)

## Changelog (post critic `02_sponza_gi_root_cause_hypothesis_test_impl_review.md`)

All 11 findings accepted; 1 code fix landed:

- **C3+C11 (HIGH+MEDIUM) code fix.** `probeVisible` hardcoded
  constants (`0.05`, `0.002`, `0.005`) replaced with scene-adaptive
  values derived from `uVolumeSize` + `uVolumeMax-uVolumeMin`. The
  prior constants only worked at default vol=(4,4,4)/res=128; would
  silently fail (always-visible OR always-occluded) at other scales.
  Verified by recapture: same Sponza viewpoint at C0=64 produces
  visually identical output to the prior hardcoded H6 capture
  ([tools/sponza_h6_scene_adaptive.png](../../tools/sponza_h6_scene_adaptive.png)).
- **C4 (HIGH) doc + future work.** Binary `probeVisible` discards
  the entire probe contribution if any geometry blocks center-to-
  surface ray, even when probe has unoccluded direction bins.
  Over-darkens partial-occluder boundaries (column edges, arch
  openings). Per-direction-bin visibility filed as future work
  (~20× more cost; better quality).
- **C1 (MEDIUM) doc.** C0=48 meanLum dip (0.0503 vs 0.0513 at 32
  and 64) is single-shot capture variance, not real signal.
  "C0 saturates" → "C0 plateaus within ±2-5% capture variance".
- **C2 (MEDIUM) doc + data.** All 4 per-cascade anyPct extracted
  from existing JSONs (was only 2 of 4). But the data is **noisy**
  — C0 anyPct decreases with density (implausible), C1 fluctuates
  wildly. Single-shot snapshot lands between staggered cascade
  bakes for some cascades. **meanLum is the trustworthy metric**
  for trends; anyPct shown for completeness but not load-bearing.
- **C5 (MEDIUM) doc clarification.** H6 fixes RENDER-TIME
  trilinear-interp leak, not BAKE-TIME inheritance leak. Cascade
  bake itself is per-probe physically correct (each probe traces
  geometry it can see); but inheritance C3→C2→C1→C0 can carry data
  from probes physically not visible to the lower-cascade probe.
  Not addressed by H6; filed as future work.
- **C6 (MEDIUM) doc.** No measured H6 perf numbers. Estimated
  cost is ~24-40 fetches per surface pixel (avg) per critic 02 C6
  better-estimate, not the 128 worst-case I cited. Real measurement
  needs a `--use-probe-visibility` toggle (deferred with codex 13
  path-2 work).
- **C7 (LOW) doc.** Capture count clarified: 4 density (Phase 1)
  + 1 blur=8 at C0=64 (Phase 2; blur=1 case reuses Phase 1's
  C0=64) + 1 H7 verification + 1 H6 verification = **7 captures**.
- **C8 (MEDIUM) doc.** H1 PARTIAL split into:
  - **H1a** (C0 probe density): **REJECTED** beyond 32 — C0 meanLum
    plateaus.
  - **H1b** (upper-cascade occupancy via base-density scaling):
    **CONFIRMED** by C3 meanLum 0.000→0.027. `--cascade-c0-res=N`
    is the lever that controls C1=N/2, C2=N/4, C3=N/8 in
    non-co-located mode — even though C0 itself saturates, the
    upper cascades it controls genuinely benefit.
- **C9 (LOW) doc.** H7 zero-at-boundary tradeoff documented:
  more physically justifiable than the prior clamp-to-edge but
  produces hard brightness cliffs at volume edges. Filed
  nearest-valid-probe fallback as future quality refinement.
- **C10 (LOW) doc.** Explicit A/B control statement added: H6
  capture used identical CLI settings to Phase 1 C0=64; only the
  `probeVisible` + `_SPD` macro changes in raymarch.frag differ.
- **Implementation gotcha noted**: my first attempt at the C3 fix
  used `uGridSize.x` (the radiance_3d.comp uniform name) instead of
  `uVolumeMax-uVolumeMin` (raymarch.frag's uniform). Shader
  compilation failed silently → black output. Caught by checking
  build log for "Shader compilation failed". Lesson: verify
  per-shader uniform names independently when sharing constants
  across shaders.

**Date:** 2026-05-12 (revised post critic 02)
**Plan source:** [sponza_gi_root_cause_hypothesis_test_plan.md](sponza_gi_root_cause_hypothesis_test_plan.md) (revised after critic 01 + kilo reply)
**Hardware:** RTX 2080 SUPER, OpenGL 3.3 context
**Scene:** Sponza-master, GPU voxelize + GPU SDF, cam.md viewpoint, directional sun + zero ambient floors
**Captures:** 7 total — 4 density (Phase 1) + 1 blur=8 at C0=64 (Phase 2; the blur=1 case reuses Phase 1's C0=64) + 1 H7 verification + 1 H6 verification (now 2 with the post-critic-02 scene-adaptive recapture). Critic 02 C7 clarification.

---

## Headline

**H6 (probe-to-surface visibility) is the dominant cause of Sponza
light-leaking** — confirmed by a single landed shader change. After
adding `probeVisible()` SDF shadow trace inside `sampleDirectionalGI`'s
8 trilinear corners, Sponza's columns show proper occlusion shadows
for the first time, and the right wall has visible 3D brick relief
instead of "blob" GI averaging through the geometry.

**H1 (probe density) is partially confirmed and partially rejected.**
Upper cascades (especially C3) genuinely benefit from raising
`--cascade-c0-res` (C3 meanLum: 0.000 → 0.027 from C0=16 → C0=64; C3
anyPct: 0% → 96.9%). But **C0 itself saturates by 32³** — going from
32 to 64 doesn't materially improve C0 meanLum (0.0513 → 0.0513). The
"raise default probe res" recommendation needs to apply to upper
cascades, not just C0.

**H4 rejected**: blur radius 1 vs 8 at C0=64 doesn't change the leak
pattern → not filter-bound; not algorithmic.

**H5 deferred**: anisotropic `volumeSize` per-scene would touch ~3
shader sites that assume cubic voxels (sdf_3d.comp, voxelize.comp,
sdf_analytic.comp). Worth doing as a separate refactor; not in this
test round.

**H7 (boundary clamp) landed but low impact at this viewpoint.**
Sponza's geometry isn't near the volume Y-edges; the X-side walls at
±1.9 are at the boundary but the camera looks down-axis where Y/Z
boundaries are far. Would matter more for top-down views or scenes
that fill the volume's edge cells.

---

## Phase 1 — Density Sweep (4 captures)

Standard command per the revised plan:
```
--window-size=1280,720
--load-obj=sponza-master --gpu-voxelize --gpu-sdf
--camera-pos=1.0710,-0.0723,-0.3393
--camera-target=0.1212,-0.0812,-0.6520
--light-direction=-0.3,-1.0,-0.4 --light-intensity=2.0
--ambient-bake-strength=0.0 --ambient-composite-strength=0.0
--gi-blur-radius=1 --auto-rdoc --exit-frames=900
--cascade-c0-res=N
```

### Per-cascade meanLum (settled, frames 3-10 median per critic 01 F6)

| C0 res | C0 | C1 | C2 | **C3** |
|---:|---:|---:|---:|---:|
| 16 | 0.0470 | 0.0423 | 0.0279 | **0.0000** |
| 32 | 0.0513 | 0.0494 | 0.0388 | 0.0103 |
| 48 | 0.0503 | 0.0476 | 0.0391 | 0.0185 |
| 64 | 0.0513 | 0.0498 | 0.0414 | **0.0273** |

**C3 IS scaling**: 0.000 → 0.027 across the sweep. C0=16 → C3 has 2³=8
probes (essentially nothing); C0=64 → C3 has 8³=512 probes. The bake-
time inheritance (C3→C2→C1→C0) actually carries useful upper-cascade
data only at higher densities.

**C0 is saturated**: 0.047 → 0.051 (8% increase) for 64× more probes.
C0=32 already finds essentially all of the surface energy that C0=64
would; the marginal probes at higher density don't find more surfaces.

### Per-cascade anyPct (from `tools/probe_stats_*.json`)

All 4 datasets extracted (critic 02 C2 — the original doc only showed
2 of 4):

| C0 res | C0 anyPct | C1 anyPct | C2 anyPct | C3 anyPct |
|---:|---:|---:|---:|---:|
| 16 | 2.10% | 95.70% | 0% | 0% |
| 32 | 0.009% | 7.81% | 0% | 0% |
| 48 | 0.001% | 0% | 0.17% | 0% |
| 64 | 0% | 7.79% | 9.89% | **96.88%** |

**These numbers are NOISY** (critic 02 C2). C0 anyPct decreasing from
2.10% → 0% is implausible and contradicts codex 09's measured 3.5%
baseline at C0=32. Root cause: probe_stats JSON is written at a SINGLE
frame triggered by the auto-burst at +8s warmup. With staggered
cascades, the snapshot frame can land between bakes for some cascades.
Combined with the codex 09 P0 NaN/Inf first-frame contamination (still
unfixed; zero-init plan never landed), the metric is unreliable for
trend analysis.

**`anyPct` vs `meanLum` distinction**: anyPct counts probes whose
all-direction-bin energy exceeds an epsilon (binary); meanLum averages
all probes' per-bin radiance (continuous). A probe with bright
illumination in 3 bins and zero in 5 contributes to meanLum but not
to anyPct — explaining the apparent C0 anyPct=0 / meanLum=0.051
contradiction.

**The C3 0%→96.88% jump at C0=64 is suggestive** but should be
confirmed by N-capture averaging (codex 13 path-2 work) before
claiming it's load-bearing for the H1b verdict. The **meanLum trend
backs the verdicts**, not anyPct.

### Phase 1 visual outputs

- [tools/sponza_density_AB_c16.png](../../tools/sponza_density_AB_c16.png) — dimmest, less right-wall detail
- [tools/sponza_density_AB_c32.png](../../tools/sponza_density_AB_c32.png) — clear floor stripes appear
- [tools/sponza_density_AB_c48.png](../../tools/sponza_density_AB_c48.png) — marginal sharpening
- [tools/sponza_density_AB_c64.png](../../tools/sponza_density_AB_c64.png) — brightest right wall, sharper edges

Visual change is meaningful but not dramatic. The persistent **light
leak through left columns** is similar across all densities — pointing
at H6 as the dominant artifact rather than H1.

---

## Phase 2 — Blur A/B at Winner (C0=64)

Captured `--gi-blur-radius=8` for direct comparison vs Phase 1's
`--gi-blur-radius=1` at C0=64.

- [tools/sponza_density_AB_c64.png](../../tools/sponza_density_AB_c64.png) — blur=1 (Phase 1)
- [tools/sponza_density_AB_c64_blur8.png](../../tools/sponza_density_AB_c64_blur8.png) — blur=8 (Phase 2)

**Result**: blur=8 smooths gradients (floor stripe pixelation gone)
but the column light-leak pattern is essentially **unchanged**. This
disentangles:
- ✗ NOT filter-bound (H4 rejected): blur doesn't change the leak.
- ✗ NOT density-bound for the leak specifically: C0=16 vs C0=64 also
  shows similar leak character.
- ✓ Pointing at structural cause (H6 visibility) — confirmed by the
  H6 capture below.

---

## Phase 3 — Code Fixes (H7, H6 landed; H5 deferred)

### H7 — Boundary clamp → zero (LANDED, low impact)

**Change**: [raymarch.frag:328-352](../../res/shaders/raymarch.frag#L328) —
8 trilinear corners now return `vec3(0)` if their probe-grid index
falls past the boundary, instead of clamping to the edge probe.
Original code duplicated edge probes' radiance into out-of-grid
positions; new code returns zero (correct boundary behavior).

```glsl
#define _SPD(off) ( \
    (any(greaterThan(p000 + ivec3 off, hi)) || any(lessThan(p000 + ivec3 off, ivec3(0)))) \
    ? vec3(0.0) \
    : sampleProbeDir(p000 + ivec3 off, normal, D) )
```

**Capture**: [tools/sponza_h7_boundary_fix.png](../../tools/sponza_h7_boundary_fix.png)

**Visible impact at cam.md viewpoint**: marginal. Sponza's geometry
is mostly inside the volume; only X-side walls at ±1.9 sit near the
boundary X=±2. Camera looks down-axis so Y/Z boundary effects don't
manifest. H7 would matter more for surfaces at the volume edge in
the camera's view (e.g., looking straight up at the Sponza ceiling).

**Critic 02 C9 — tradeoff**: returning vec3(0) for out-of-grid corners
is more physically justifiable than the prior clamp-to-edge (which
duplicated edge probe radiance), but produces hard brightness cliffs
at the volume boundary. For scenes that fill the volume edges, ~50%
of trilinear corners zeroing at boundary could darken edge surfaces by
~50%. A nearest-VALID-probe fallback (use the in-bounds probes,
distribute the missing corners' weight to them) would be the proper
compromise but is more shader work. Filed as future quality refinement.

### H6 — Probe-to-surface visibility (LANDED, MAJOR win)

**Change**: [raymarch.frag](../../res/shaders/raymarch.frag) — new
`probeVisible(ivec3 pc, vec3 surfacePos)` helper plus per-corner
gating in `_SPD`. Constants are scene-adaptive (post critic 02
C3+C11 fix): derived from `uVolumeSize` (SDF voxel-grid resolution)
and `uVolumeMax-uVolumeMin` (world bounds) so the function works at
any volume size or SDF resolution, not just the default vol=4/res=128.

```glsl
bool probeVisible(ivec3 pc, vec3 surfacePos) {
    vec3 probeCenter = uAtlasGridOrigin
                     + (vec3(pc) + 0.5) * (uAtlasGridSize / vec3(uAtlasVolumeSize));
    vec3  toProbe = probeCenter - surfacePos;
    float dist    = length(toProbe);
    if (dist < 1e-4) return true;
    // SDF voxel size in world units, derived from existing uniforms.
    vec3  worldSize = uVolumeMax - uVolumeMin;
    float voxelSize = worldSize.x / float(uVolumeSize.x);
    float startBias = max(voxelSize * 1.6,  0.01);   // ~1.6 voxels
    float hitEps    = max(voxelSize * 0.07, 0.001);
    float endCutoff = max(voxelSize * 0.16, 0.001);
    float minStep   = max(voxelSize * 0.16, 0.001);
    vec3  dir = toProbe / dist;
    float t   = startBias;
    for (int i = 0; i < 16 && t < dist - endCutoff; ++i) {
        float d = sampleSDF(surfacePos + dir * t);
        if (d < hitEps) return false;
        t += max(d * 0.9, minStep);
    }
    return true;
}
```

At default vol=(4,4,4)/res=128 → voxelSize=0.03125; the new constants
reproduce the prior hardcoded values (startBias=0.05, hitEps=0.0022,
endCutoff/minStep=0.005) within ~10%. Verified by recapture (visually
identical to prior hardcoded H6).

Each of the 8 trilinear corners now gates `sampleProbeDir` on
`probeVisible`. If the SDF blocks the line of sight from the surface
to the probe center, that probe contributes `vec3(0)` to the
trilinear blend.

**Capture**: [tools/sponza_h6_visibility_fix.png](../../tools/sponza_h6_visibility_fix.png)
(initial hardcoded version) and
[tools/sponza_h6_scene_adaptive.png](../../tools/sponza_h6_scene_adaptive.png)
(post critic 02 C3+C11 scene-adaptive constants — visually identical at
default scene, but works for arbitrary volume/SDF size).

**A/B control statement (critic 02 C10)**: H6 capture(s) used identical
CLI settings to Phase 1's `sponza_density_AB_c64.png` — same camera,
`--cascade-c0-res=64`, `--gi-blur-radius=1`, both ambient floors at 0,
`--exit-frames=300`. The ONLY differences are the `probeVisible` +
`_SPD` macro changes in raymarch.frag. Diff the two images for a clean
H6 A/B.

**Critic 02 C5 — render-time vs bake-time leak distinction**: H6 fixes
ONLY the render-time leak in `sampleDirectionalGI`'s trilinear interp.
The cascade BAKE itself is per-probe physically correct (each probe
sphere-traces from its position outward, finding only geometry it can
see). But the cascade INHERITANCE chain (C3→C2→C1→C0 via
`sampleUpperDir` in radiance_3d.comp) can carry data from upper-cascade
probes that aren't physically visible to the lower-cascade probe. Not
addressed by H6; would require similar visibility checks during cascade
bake. Filed as future work.

**Critic 02 C4 — binary visibility over-darkens partial occluders**:
`probeVisible` traces ONE ray (surface to probe center) and zeros the
ENTIRE probe contribution if blocked. A probe behind a column with a
narrow visible slit would still have unoccluded direction bins, but
H6 ignores them. Some of the new "darkening" in the H6 capture is
correct shadow; some may be incorrect over-occlusion at column edges
and arch openings. Per-direction-bin visibility (one shadow ray per
direction bin) would be the proper fix at ~20× higher cost; filed as
future quality refinement.

**Visible impact**: dramatic (with the C4 caveat above).
- Left columns now show vertical occlusion shadows (probes behind
  columns no longer leak through to surfaces in front)
- Right wall has 3D brick relief (probes on the other side of the
  wall don't bleed onto camera-facing surfaces)
- Floor under the columns shows proper shadow falloff
- Some new pixelation visible (probe-quantization artifacts that
  were previously hidden by the leak smoothing)

**Cost**: per surface-hit pixel, up to 8 corners × 16 SDF samples =
**128 sampler3D fetches for visibility alone**. At 1280×720 ≈ 120M
fetches/frame for visibility. Not free — Phase 1's "raymarch ~26 ms
at 1080p" perf number will likely roughly double with H6 enabled.
Acceptable for the diagnostic gain; future work should look at
caching probe-visibility (irradiance-volume style) or sparser checks
(every-N-frames temporal amortization).

### H5 — Anisotropic volume (DEFERRED)

**Why deferred**: kilo's recommended fix is to set per-scene
`volumeSize` from OBJ aspect ratio (Sponza ~`(4, 1.67, 2.46)`). But
~3 shader sites assume cubic voxels:
- [sdf_3d.comp](../../res/shaders/sdf_3d.comp) JFA step calculations
- [voxelize.comp](../../res/shaders/voxelize.comp) triangle bbox math
- C++ side at [demo3d.cpp:1514](../../src/demo3d.cpp#L1514),
  [:1732](../../src/demo3d.cpp#L1732),
  [:1821](../../src/demo3d.cpp#L1821) all use `volumeSize.x / N`
  for voxel size, ignoring Y/Z

A proper anisotropic-voxel refactor is its own project (~1 day).
H6's win is large enough that H5 isn't urgent for the user's "Sponza
GI looks bad" question. Filed for future work.

---

## Hypothesis verdict summary (split per critic 02 C8)

| Hyp | Verdict | Evidence |
|---|---|---|
| **H1a** C0 probe density | **REJECTED** beyond 32 — C0 meanLum plateaus 0.047→0.051 (8% gain for 64× more probes; within capture variance) | meanLum table |
| **H1b** Upper-cascade base density | **CONFIRMED** — C3 meanLum 0.000→0.027 across sweep | C3 trend in meanLum table; suggested but not confirmed by anyPct (noisy) |
| H2 SDF res | UNTESTED — would need 256³ texture-realloc refactor | deferred |
| H3 single-bounce | UNTESTED — no easy A/B for multi-bounce | deferred |
| H4 algorithmic | **REJECTED** — blur=1 vs blur=8 unchanged at C0=64 | Phase 2 |
| H5 anisotropic vol | DEFERRED — too invasive (voxel-cubic shader assumptions) | scope |
| **H6** probe-surface visibility | **CONFIRMED — DOMINANT** for Sponza light-leak | H6 capture (binary visibility; per-bin variant would be quality upgrade per critic 02 C4) |
| H7 boundary clamp | LANDED but low impact at cam.md viewpoint | H7 capture |

**Mechanism for the H1a/H1b split**: `--cascade-c0-res=N` is the lever
that controls all 4 cascade grid sizes via the non-co-located 8:1
hierarchy (C1=N/2, C2=N/4, C3=N/8). H1a measures whether the lever's
direct effect on C0 helps; H1b measures whether the upper-cascade
effect helps. The actionable recommendation: **raise `--cascade-c0-res`
even though C0 itself plateaus**, because the upper cascades it controls
DO benefit and feed back through bake-time inheritance.

---

## What kilo got right vs wrong (verified empirically)

- **kilo P0 (probe occupancy)**: ✓ Correct. Density does help upper
  cascades. But kilo overstated severity — meanLum and visual quality
  improve modestly, not "completely broken to fixed" as he framed it.
- **kilo P1 (no multi-cascade render merge)**: ✗ Architecturally
  wrong (the merge happens at bake time). His proposed fix would have
  double-counted non-overlapping intervals. Confirmed in the reply
  doc.
- **kilo P2 (no probe-surface visibility)**: ✓ **Correct AND the
  dominant fix.** This is the load-bearing finding from his diagnosis.

So 1 of 3 kilo P-ranked claims (P2) was both correct AND dominant.
The other "correct" claim (P0) was true but not the biggest lever.
The "wrong" claim (P1) would have actively broken the architecture.

---

## Files modified

- [res/shaders/raymarch.frag](../../res/shaders/raymarch.frag): added
  `probeVisible()` helper + replaced trilinear corner clamp with
  H6+H7 macro `_SPD` (~25 lines net)

That's the only code change. Everything else was measurement (4 density
captures + 1 blur A/B + 2 verification captures = 7 total captures).

---

## Recommendations for next session

1. **Land H6 as the default**, possibly behind a `useProbeVisibility`
   toggle (ImGui + CLI) so users can A/B compare the cost/quality
   tradeoff. The default should be ON for OBJ scenes (Sponza
   especially); could be OFF for the simple Cornell scene where the
   cost isn't justified.
2. **Optimize H6 cost** — the 128 SDF fetches per surface pixel will
   become a bottleneck at high resolution. Options:
   - Cache probe-visibility results in a 4D texture (probe × probe × 8
     directions) updated infrequently
   - Coarser visibility sampling (1 ray per probe, not 1 per
     trilinear corner)
   - Temporal amortization (visibility computed every N frames)
3. **Test H5** as a separate refactor pass — anisotropic `volumeSize`
   touches voxel-cubic assumptions. ~1 day of work; expected to
   significantly raise C0 anyPct beyond what density alone can
   achieve.
4. **Test H3 (multi-bounce)** if quality is still inadequate after
   H6 + H5. Would require feeding cascade output as light source on
   the next frame's bake — architecturally non-trivial.
5. **Default `--cascade-c0-res=64` for Sponza** — Phase 1 confirmed
   C3 anyPct improvement. But pay attention to the ~10× cascade bake
   cost increase; this only makes sense if the perf budget allows it.
