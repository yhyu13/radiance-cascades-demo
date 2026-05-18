## Reply: kilo's `sponza_gi_quality_diagnosis.md` — fact-checked against source

**Date:** 2026-05-12
**Status:** P0 verified (well-known; codex 09 already measured). P2
verified (real issue, no visibility check). **P1 framing rejected** —
kilo conflated "render-time merge" with "multi-cascade contribution".
The hierarchical merge IS happening, just at bake time (C3→C2→C1→C0
inheritance), not at render time. That's how radiance cascades
architecturally work. The real issue isn't a missing render-time merge;
it's that bake-time inheritance has nothing to inherit FROM because
upper cascades are also at 0% surface hits (which IS the P0 issue
kilo correctly diagnoses).

Net: kilo's diagnosis is **2 of 3 correct, with one important
architectural misunderstanding**. The proposed fixes for P0 (anisotropic
volume, higher SDF res, boundary handling) and P2 (probe-to-surface
visibility) are GENUINELY NEW hypotheses worth adding to the existing
test plan. The P1 fix ("add multi-cascade sampling to raymarch.frag")
would actually break the architecture (double-counting non-overlapping
intervals).

Also a process note: kilo's "Sponza meanLum NaN/-376" is from frame-1
allocation garbage (codex 09 P0; Step 11 added sanitization at imageStore
sites but the codex 10 zero-init plan never landed). After frame 2+
the values are clean — kilo may have read pre-Step-11 logs. And the
diagnosis predates yesterday's lighting controls work — kilo's "uniform
brown ambient" framing is now testable with `--ambient-bake-strength=0
--ambient-composite-strength=0`.

---

### P0 (probe occupancy ~0% on Sponza) — VERIFIED, well-known, fixes accepted

Kilo's measured numbers (Sponza C0 anyPct=4.3%, C1=0.02%, C2=0.39%,
C3=0%) are within rounding of codex 09's measured 3.5% and consistent
with what the existing logs show. Cornell anyPct=100% across all
cascades is also confirmed — sample probe_stats JSON
(`tools/probe_stats_17777082435236350.json`) shows
`{anyPct: 100, surfPct: 29-100}` per cascade. Cornell verified ≠
fabricated (this was a critic 01 F3 concern; kilo did the work codex 09
hadn't done).

**Three sub-fixes proposed by kilo, all worth testing:**

| # | Fix | My take |
|---|---|---|
| 1a | Anisotropic `volumeSize` matching scene aspect (Sponza 3.8:1.59:2.34) | **GENUINELY NEW** hypothesis; not in existing plan. Add as H5. Cheap test: change `volumeSize` per-scene in `loadOBJMesh`. |
| 1b | Increase `volumeResolution` 128³→256³ (or analytic Sponza SDF) | Already known; deferred during Step 12 scaling experiment because of texture-realloc infrastructure. Cost: 8× SDF bake. |
| 1c | Fix `raymarch.frag:328-335` boundary clamping (return 0 instead of edge probe) | **GENUINELY NEW**; not in existing plan. Add as H7. Cheap one-line shader fix. |

The H5/H7 fixes are 1-day work each and could plausibly contribute as
much as the C0 density bump in my existing test plan.

---

### P1 (no multi-cascade merge in raymarch) — PARTIALLY REJECTED

Kilo's claim: "`raymarch.frag` calls `sampleDirectionalGI(pos, normal)`
which does trilinear interpolation over 8 spatial neighbors within a
single cascade level (C0). There is no code that merges C0 short-range
+ C1/C2/C3 long-range radiance."

**This is technically true at render time but conceptually wrong** about
where the multi-cascade merge happens. Verified evidence:

**Render-time texture binding** ([demo3d.cpp:2384-2434](src/demo3d.cpp#L2384)):
```cpp
int selC = std::max(0, std::min(selectedCascadeForRender, cascadeCount - 1));
glBindTexture(GL_TEXTURE_3D, cascades[selC].probeGridTexture);  // <-- ONE cascade
glUniform1i(..., "uRadiance", 1);
// later:
glBindTexture(GL_TEXTURE_3D, cascades[selC].probeAtlasTexture); // <-- same one
glUniform1i(..., "uDirectionalAtlas", 3);
```

So at render time, only `cascades[selC]` (default selC=0 = C0) is bound.
✓ Kilo correct on this narrow fact.

**BUT — the multi-cascade merge already happened at bake time.**
Verified at [radiance_3d.comp:367-397](res/shaders/radiance_3d.comp#L367):

```glsl
vec3 upperDir = vec3(0.0);
if (uHasUpperCascade != 0) {                          // bake reads upper cascade
    if (uUseDirectionalMerge != 0) {
        if (uUpperToCurrentScale == 2 && uUseSpatialTrilinear != 0)
            upperDir = sampleUpperDirTrilinear(...);  // 8-neighbor spatial
        else
            upperDir = sampleUpperDir(...);            // single-probe nearest
    }
    // ...
}
vec3 rad;
if (hit.a < 0.0)        rad = hit.rgb;                 // sky sentinel
else if (hit.a > 0.0)   rad = hit.rgb*l + upperDir*(1-l);  // surface + upper blend
else                    rad = upperDir;                // miss → inherit upper
```

This is the canonical radiance-cascades hierarchy: each cascade i bakes
its own probe radiance and INHERITS from cascade i+1 for rays that miss
or extend beyond i's interval. So:
- C2 bake reads C3's atlas → C2 contains its surface hits + C3 inheritance
- C1 bake reads C2's atlas → C1 contains its hits + C2's hits + C3's hits (transitively)
- C0 bake reads C1's atlas → C0 contains everything down the hierarchy

When raymarch.frag reads C0 at render time, **the values it reads
already contain the cascaded multi-bounce contribution** because it was
baked in.

**Kilo's proposed fix would break the architecture.** "Sample C0+C1+C2+C3,
blend by interval coverage" would **double-count**: C0 already contains
the C1 contribution (via inheritance), so adding C1 again at render
would weight it 2×. The intervals `[0,0.125]`, `[0.125,0.5]`, `[0.5,2]`,
`[2,8]` are non-overlapping precisely because the bake-time inheritance
distributes the contribution exactly once.

**The real underlying issue kilo grazes but doesn't articulate.** With
Sponza's anyPct of 4.3% at C0 and **0.02-0.39% at C1/C2/C3**, bake-time
inheritance has **nothing to inherit FROM**. C0's `upperDir` reads from
C1; C1 is also empty; → C0 inherits zero from upper cascades. The
hierarchy is structurally correct but operationally empty. This isn't a
missing render-merge bug — it's the **same P0 issue** cascading down.

**Plan revision** for the existing density experiment: when capturing,
also log per-cascade anyPct via the probe_stats JSON. If C0=64 raises
C0 anyPct to e.g. 30% but C1/C2/C3 stay near zero, the upper cascades
need their OWN density bump or the inheritance is moot. The existing
plan's `--cascade-c0-res=N` only scales C0 (with C1=N/2, C2=N/4,
C3=N/8 in non-co-located mode); upper cascades end up *coarser*, not
finer.

---

### P2 (no visibility check between probe and surface) — VERIFIED

Kilo's claim verified at [raymarch.frag:297-310](res/shaders/raymarch.frag#L297):

```glsl
vec3 sampleProbeDir(ivec3 pc, vec3 normal, int D) {
    vec3 irrad = vec3(0.0); float wsum = 0.0;
    for (int dy = 0; dy < D; ++dy) {
        for (int dx = 0; dx < D; ++dx) {
            vec3 bdir = binToDir(ivec2(dx, dy), D);
            float w = max(0.0, dot(bdir, normal));   // ONLY weighting
            irrad += texelFetch(uDirectionalAtlas, ...).rgb * w;
            wsum += w;
        }
    }
    return irrad / max(wsum, 1e-4);
}
```

✓ Only cosine weight. No `shadowRay()` / `sampleSDF()` between probe
center and surface point. This DOES cause light leaking through thin
walls (Sponza arches, columns, curtains).

**Kilo's three proposed fixes are sound:**
1. SDF shadow ray from probe to surface (cheap; reuse existing
   `shadowRay()` at [raymarch.frag:348-362](res/shaders/raymarch.frag#L348))
2. Variance-based statistical occlusion (LPV-style)
3. Backface heuristic

Option 1 is the obvious first step. Add as **H6** to the existing test
plan: at fixed C0=32, fixed `--gi-blur-radius=1`, run a captured-frame
comparison with/without a one-line shader edit that adds
`shadowRay(pc_world, surfPos)` to `sampleProbeDir`. If light leaking
visibly drops without breaking GI brightness, P2 is dominant for the
visible artifacts (vs P0 dominant for the bake correctness).

---

### Things kilo missed or got wrong

1. **NaN/Inf "broken data"** (kilo P0 last paragraph): the codex 09 P0
   first-frame contamination IS real but Step 11 added `sanitizeRadiance`
   in [radiance_3d.comp:94](res/shaders/radiance_3d.comp#L94) and per-bin
   NaN-clamp in [reduction_3d.comp:35-38](res/shaders/reduction_3d.comp#L35).
   After frame 2+ the meanLum values are clean. Kilo's "C0 meanLum=NaN
   maxLum=inf" reads like he was looking at frame-1-only logs.
2. **Lighting controls landed yesterday.** Kilo's "uniform brown
   ambient" framing predates the new `--ambient-bake-strength=0
   --ambient-composite-strength=0` + `--light-direction` flags. Several
   of his observations would now be testable without the cosmetic floor
   masking the real GI signal.
3. **Cascade architecture misunderstanding** (P1, see above).

### Things kilo got right that I missed

1. **Anisotropic volume** is genuinely new — my hypothesis test plan
   only swept `--cascade-c0-res`, not the underlying `volumeSize`.
   Sponza's 3.8:1.59:2.34 aspect filling a `(4,4,4)` cube wastes
   ~50% of probes in empty Y space.
2. **Boundary clamping in raymarch.frag** is a real light-leak source I
   hadn't called out.
3. **Probe-to-surface visibility** is a real correctness issue separate
   from P0 density.

---

### Plan revisions to my existing test plan

The existing
[sponza_gi_root_cause_hypothesis_test_plan.md](../sponza_gi_root_cause_hypothesis_test_plan.md)
(revised after critic 01) tests H1 (density), H2 (SDF res deferred), H3
(multi-bounce), H4 (algorithmic). Kilo's diagnosis adds:

- **H5: isotropic volume mismatched to elongated Sponza** — new
  hypothesis, cheap to test (change `volumeSize` to anisotropic per-scene).
  Predicted outcome: with `volumeSize ≈ (4, 1.67, 2.47)` matching
  Sponza's aspect, anyPct should jump significantly without changing
  probe count.
- **H6: no probe-surface visibility test causes light leaking** — new
  hypothesis, cheap to test (one-line `shadowRay()` add to
  `sampleProbeDir`). Predicted outcome: visible reduction in
  through-wall light leaking; possibly slight overall darkening.
- **H7: boundary clamp at volume edges leaks edge-probe radiance** —
  new hypothesis, cheap to test (replace clamp-to-edge with
  return-zero at [raymarch.frag:328-335](res/shaders/raymarch.frag#L328)).
  Predicted outcome: surfaces near `[-1.9, 1.9]` boundaries should
  darken slightly; if they brighten or stay same, this isn't a
  significant leak source.

I'll update the test plan to add H5/H6/H7 as additional hypotheses
testable in the existing capture loop, plus the per-cascade anyPct
log-analysis to detect the "C1/C2/C3 still empty" failure mode for H1.

---

### Bottom line

Kilo's diagnosis is **substantively useful**: P0 confirms what codex 09
measured; P2 surfaces a real visibility-test gap; the proposed
anisotropic volume + boundary handling fixes are genuinely new and
worth testing. **P1's "no render-time merge" is technically true but
the conclusion ("multi-cascade not used") is wrong** — the merge
happens at bake time and is empty for Sponza for the SAME reason P0
identifies (upper cascades also at 0%).

**Updating my test plan** to add H5 (anisotropic volume), H6 (probe
visibility), and H7 (boundary clamp) — three additional cheap-to-test
hypotheses. **Not** adding kilo's P1 fix, because it would break the
non-overlapping interval invariant. The real P1 fix is to **make the
bake-time inheritance produce useful upper-cascade data**, which is the
existing P0/H1 (raise probe density at upper cascades too).
