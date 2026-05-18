## Reply: Probe Visibility Acceleration Plan Critic 03 — `03_probe_visibility_acceleration_plan_review.md`

**Date:** 2026-05-12
**Status:** All 10 findings accepted. **F1+F2 fundamentally rewrite the
mode 4 algorithm** — my plan transcribed ShaderToy's WeightedSample
into 3D incorrectly. F3 is a real silent-corruption footgun
(temporal_blend.comp EMA-blends the alpha channel). The other 7 are
algorithm clarifications + documentation fixes. Will revise the plan
before any shader code lands.

---

### F1 (HIGH) — Wrong trig function — `cos(π/2 - θ) = sin(θ)`

You're right, and I missed the trig identity entirely. ShaderToy's
`cos(PI*0.5 - theta)` is `sin(θ)` — for small θ, ~0 → restrictive
test. My `cos(π/D × 0.5)` for D=8 ≈ 0.98 → essentially non-restrictive
(`distSP < hitDist + ε`). The plan would have under-occluded almost
universally; verification A/B would have shown "Mode 4 ≈ Mode 0" and
I'd have wrongly concluded "ShaderToy formula doesn't translate to 3D".

**Plan revision**: the trig question becomes moot once F2 lands —
the corrected per-bin algorithm doesn't use a cone-angle scalar
multiplier on hitDist; it does a signed projection comparison
(`t < hitDist + ε`) with no angular correction in the basic form. A
solid-angle-aware refinement is filed as future work (see "future
quality refinement" in the revised plan).

---

### F2 (HIGH) — Direction mismatch: scalar distSP vs per-bin hitDist

You're right and this is the load-bearing rewrite. ShaderToy fetches
**one bin only** — the one matching the surface→probe direction —
and tests `length(relVec) < lProbeRayDist * sin(θ) + ε`. It's a
single-bin per-probe visibility check (data-driven Mode 1
equivalent). My plan iterated all D² bins and applied a
direction-blind scalar test, which is geometrically meaningless and
inverts visibility per-bin in pathological cases (your floor/ceiling
example).

**Plan revision (committing to path #2 — 3D-correct per-bin):**

For each bin direction `bdir`, project the surface→probe vector onto
`bdir` to get a signed scalar `t`:

```glsl
float t = dot(surfacePos - probeCenter, bdir);
```

Three cases:
- `t < 0` → surface is on the OPPOSITE side of probe from `bdir`.
  Probe's `bdir` ray went away from surface → bin's radiance came
  from a region the surface can't see directly via that direction.
  But the bin's radiance is incoming radiance to the probe FROM
  `bdir` — and since the surface is geometrically near the probe
  (distance bounded by ~probe cell size), the surface receives
  similar radiance from the same direction. **Visible.**
- `0 ≤ t ≤ hitDist` → surface is between probe and the geometry the
  probe hit. Surface is on the SAME side of the hit as the probe.
  **Visible.**
- `t > hitDist` → surface is PAST the geometry the probe hit. The
  hit blocks the surface from "seeing" the radiance source the probe
  saw. **Occluded.**

```glsl
float wvis = (t <= hitDist + missEps) ? 1.0 : 0.0;
```

(With `missEps` from F4 below.)

This gives **per-bin granularity** (each of D² bins independently
visible/occluded based on its own direction and hit) — eliminates
mode-1's dot-banding (which came from per-PROBE binary decisions).

**Why this is geometrically correct in 3D** (where ShaderToy's 2D
formula isn't directly useful):

The bin direction `bdir` is the direction the probe's ray traveled
to find geometry. The "wall plane" perpendicular to `bdir` at
distance `hitDist` from the probe is a reasonable proxy for the
geometry the probe saw. Any surface point on the FAR side of that
plane (`t > hitDist`) is geometrically occluded from receiving the
same radiance the probe collected for that bin. Surfaces on the
NEAR side or in any other direction can still receive it (because
the geometry blocks `bdir`-going light, not light from any other
direction).

**Cone correction**: the projection assumes the geometry is a flat
wall perpendicular to `bdir`. Real geometry is curved/oriented
arbitrarily; a more rigorous test would compare lateral distance
against `tan(bin_half_angle) × hitDist`. For now, the basic
projection test is a good first approximation; refine if quality
issues appear.

---

### F3 (HIGH) — Temporal blend corrupts alpha

You're right and this is a real silent-corruption footgun. The
temporal blend at
[temporal_blend.comp:82](res/shaders/temporal_blend.comp#L82) does:
```glsl
imageStore(oHistory, coord, mix(his, cur, uAlpha));
```
on the entire vec4 — alpha (hit distance) gets EMA'd with stale
history. Mode 4 reading that alpha would compare against a
nonsensical interpolation.

**Plan revision (two-line patch in temporal_blend.comp):**

```glsl
vec4 blended = mix(his, cur, uAlpha);
blended.a    = cur.a;   // hit distance: use fresh, not blended
imageStore(oHistory, coord, blended);
```

Same pattern radiance_3d.comp already uses ([:428](res/shaders/radiance_3d.comp#L428)
writes `vec4(blended_rgb, hit.a)` — fresh alpha, blended rgb).

Likewise the AABB clamp at
[temporal_blend.comp:79](res/shaders/temporal_blend.comp#L79) clamps
the vec4; since hit distance is bounded
naturally (always ≥ 0 for surface hits, ≥ -1 for sky), the clamp
isn't actively corrupting it but is unnecessary work. Patch:
```glsl
his.rgb = clamp(his.rgb, nMin.rgb, nMax.rgb);
// his.a left unchanged from imageLoad
```

This makes Mode 4 robust regardless of `useTemporalAccum` state.
Documented as a Mode-4 prerequisite patch.

---

### F4 (MEDIUM) — Exact float compare

You're right. `hitDist <= 0.0` is brittle — both upstream
floating-point near-zero values and (post-F3-fix) any remaining
near-zero noise would be misclassified. Replace with a
voxel-size-relative epsilon:

```glsl
vec3  worldSize = uVolumeMax - uVolumeMin;
float voxelSize = worldSize.x / float(uVolumeSize.x);
float missEps   = 0.5 * voxelSize;
if (hitDist < missEps) {
    wvis = 1.0;  // miss / sky / negligible-distance hit → fully visible
}
```

Sky bins still use `hitDist < 0.0` separately (per the radiance_3d
sentinel convention) — those explicitly stored negative values won't
be confused with miss-bins.

---

### F5 (MEDIUM) — `probeCenter` formula vs non-co-located layout

You're right to flag this. **Confirmed compatible** by reading the
binding site: `uAtlasGridOrigin` and `uAtlasGridSize` are per-cascade
uniforms ([demo3d.cpp:2418-2419](src/demo3d.cpp#L2418)) and
`uAtlasVolumeSize` is the per-cascade probe grid resolution. Within
a single cascade's directional atlas, probes ARE on a regular grid
with cellSize = `uAtlasGridSize / uAtlasVolumeSize`. The
non-co-located concept (Phase 5d) is BETWEEN cascades — it affects
how cascade i samples cascade i+1 during bake-time inheritance, NOT
how a single cascade's atlas indexes its own probes at render time.

So the formula `probeCenter = origin + (pc + 0.5) * (size /
volumeSize)` is correct for the single-cascade atlas Mode 4 reads.

**Plan revision**: added an explicit comment in the algorithm
section noting this. Mode 4 only ever touches `cascades[selC]`'s
atlas — single-cascade — so the non-co-located layout is moot here.

---

### F6 (MEDIUM) — Mode 3 cost understated by 8×

You're right. Mode 3's `sampleProbeDirPerBinOccluded` does up to
**8 SDF samples per bin** (the inner shadow trace `for (int i = 0;
i < 8 && t < maxLen; ++i)`), so total per-pixel cost is:

`8 corners × D² bins × 8 SDF samples = 32,768 SDF fetches at D=8`

Not 4096. **Plan correction**: the cost-comparison table updated:

| Mode | SDF fetches/pixel | Texture fetches/pixel | Branches |
|---|---:|---:|---|
| 0 OFF | 0 | 8 × D² | minimal |
| 1/2 binary+renorm | up to 8 × 16 | 8 × D² | per-corner branch |
| 3 per-bin shadow trace | up to **8 × D² × 8** | 8 × D² | per-bin per-step |
| **4 depth-aware (proposed)** | **0** | 8 × D² | per-bin compare only |

Mode 4 strictly dominates in fetch budget vs all visibility-aware
modes; for SDF traces it ties with mode 0 (none).

---

### F7 (LOW) — "No banding" claim depends on F2 resolution

You're right. With F2 path #1 (single-bin port), Mode 4 reduces to
data-driven Mode 1 — same banding. With F2 path #2 (3D per-bin
projection — what I'm picking), Mode 4 has per-bin granularity and
should match Mode 3's no-banding behavior.

**Plan revision**: explicitly note that the "no banding" property
holds **only with path #2** (which is the path being taken). If
verification shows banding still present, fall back to mode-5
hybrid (depth-aware + single confirmation shadow ray).

---

### F8 (LOW) — Numeric pass criterion

You're right. **Plan revision**: add quantitative pass criteria:

- **Visual A/B (mode 3 vs mode 4)**: meanLum from
  `[4c A/B]` log line should be within ±5% across cascades C0/C1/C2.
  C3 is too sparse for stable comparison; skip.
- **Banding test**: capture a near-uniform-color region of the
  Sponza right wall (e.g., 200×200 pixel patch at known coords),
  compute pixel-value standard deviation. Mode 0 ≈ low (smooth);
  Mode 1 high (banding); Mode 3 ≈ low (no banding); Mode 4 should
  match Mode 3's std-dev within 20% to claim "no banding".
- **Cost test**: RenderDoc capture per mode, compare raymarch GPU
  µs. Mode 4 should be within 1.1× of Mode 0 (i.e., NOT 32×).

---

### F9 (LOW) — Stale visibility-mode comment

You're right. The header comment at
[raymarch.frag:307-311](res/shaders/raymarch.frag#L307) documents
modes 0/1/2 only; actual code handles 0/1/3/4 (post-Mode-4). **Plan
revision**: include a one-shot rewrite of that comment block in the
implementation phase to match the actual mode set.

---

### F10 (LOW) — No rollback plan for default flip

You're right. **Plan revision**: added "Rollback path" section:

- Mode 4 ships **opt-in initially** — not flipped to default until
  3+ scene captures (Sponza, Cornell, Cornell-Original) confirm
  visual + numeric pass.
- If post-promotion regression: revert the default in
  [demo3d.h](src/demo3d.h#L859) (one-line: `int visibilityMode = 0;`).
  CLI/ImGui still allow opting back into Mode 4. Existing screenshot
  baselines (mode 0) remain reproducible via `--visibility-mode=0`.
- The promote-to-default decision is documented in the impl notes
  with the captures that justified it.

---

### Summary

| # | Sev | Action | Plan revision required |
|---|---|---|---|
| **F1** | HIGH | Algorithm rewrite (moot under F2 fix; no scalar cone correction in basic form) | YES |
| **F2** | HIGH | Algorithm rewrite — picking path #2 (signed-projection per-bin) | YES |
| **F3** | HIGH | Patch temporal_blend.comp to pass-through cur.a | YES (prereq patch) |
| F4 | Med | Use voxel-size-relative miss epsilon | YES |
| F5 | Med | Document directional atlas is per-cascade regular grid | doc note |
| F6 | Med | Correct mode 3 cost number (×8 higher) | doc fix |
| F7 | Low | "No banding" claim conditioned on path #2 | doc fix |
| F8 | Low | Add quantitative pass criteria (meanLum ±5%, std-dev test) | doc fix |
| F9 | Low | Rewrite stale visibility-mode header comment | implementation note |
| F10 | Low | Document rollback path | doc fix |

**Bottom line.** F2's correction is the substantive algorithm change
— Mode 4 is now "per-bin signed-projection visibility test" (path #2),
not "ShaderToy's WeightedSample replicated D² times" (which my plan
attempted incorrectly). F3 is a real silent-corruption fix needed in
`temporal_blend.comp` regardless of Mode 4. F1 evaporates because the
cone-correction-as-scalar-multiplier wasn't the right approach for
3D anyway. The other 7 are correctness/documentation improvements.
Plan is being revised in place; will update separately.
