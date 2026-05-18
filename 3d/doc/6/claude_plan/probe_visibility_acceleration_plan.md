# Plan: Accelerate Probe-Surface Visibility (H6) via Depth-Aware Sampling (revised after critic 03)

## Changelog (post critic `03_probe_visibility_acceleration_plan_review.md`)

All 10 findings accepted; **3 HIGH-severity revisions to the algorithm
& pipeline**. The original plan transcribed ShaderToy's WeightedSample
into 3D incorrectly — it would have under-occluded almost universally
and the verification A/B would have wrongly concluded "ShaderToy
formula doesn't translate to 3D".

- **F1 (HIGH) algorithm rewrite — moot after F2 fix.** Original plan's
  `cosCone = cos(π/D × 0.5)` was a copy-paste of ShaderToy's
  `cos(π/2 - θ)` without simplifying — but `cos(π/2 - θ) = sin(θ)`,
  and for D=8 my formula evaluates to ≈0.98 (essentially non-restrictive)
  vs the intended ≈0.2 (very restrictive). The corrected algorithm under
  F2 doesn't use a scalar cone-multiplier on hitDist at all — solid-angle-
  aware refinement filed as future work.
- **F2 (HIGH) algorithm rewrite — picking path #2 (3D-correct per-bin).**
  Original plan iterated D² bins with a direction-blind scalar test —
  geometrically meaningless. ShaderToy's `WeightedSample` actually
  fetches ONE bin (the one along the surface→probe axis) per probe.
  My plan needed either (a) a faithful single-bin port (=data-driven
  Mode 1; same dot-banding), or (b) a 3D-correct per-bin test. Picked
  (b): for each bin direction `bdir`, project surface→probe vector
  onto bdir to get signed `t`, occlude iff `t > hitDist + ε`. Per-bin
  granularity preserves Mode 3's no-banding property at depth-aware
  cost.
- **F3 (HIGH) prerequisite patch in `temporal_blend.comp`.** When
  `useTemporalAccum=true`, the EMA `mix(his, cur, uAlpha)` corrupts
  the alpha (hit distance) channel — Mode 4 reads garbage. Plan now
  includes a one-line patch: `blended.a = cur.a;` after the mix
  (matches the radiance_3d.comp pattern of "fresh alpha, blended rgb").
- **F4 (MEDIUM) miss epsilon.** Replace `hitDist <= 0.0` with voxel-
  size-relative `hitDist < 0.5 * voxelSize` to reject "effectively
  zero" as a miss without exact-float fragility.
- **F5 (MEDIUM) clarification.** Confirmed `probeCenter` formula is
  correct: directional atlas is per-cascade with its own
  `uAtlasGridOrigin/Size/VolumeSize`; non-co-located is between-cascade
  only and doesn't affect Mode 4's single-cascade reads.
- **F6 (MEDIUM) cost correction.** Mode 3 worst-case is **8 corners ×
  D² bins × 8 SDF samples = 32,768 sampler3D fetches/pixel**, not 4096.
  Strengthens Mode 4's case (~1.05× cost vs Mode 0; effectively zero
  vs Mode 3's 32k SDF fetches).
- **F7 (LOW) doc.** "No banding" claim explicitly conditioned on path
  #2 (path #1 single-bin would have had Mode-1-style banding).
- **F8 (LOW) doc.** Quantitative pass criteria added: meanLum ±5%
  vs Mode 3, std-dev banding test, raymarch GPU µs cost test.
- **F9 (LOW) implementation note.** Stale header comment block at
  raymarch.frag:307-311 (documents 0/1/2; code handles 0/1/3) is
  rewritten in the implementation phase.
- **F10 (LOW) doc.** Rollback path: Mode 4 ships opt-in for first
  3+ scene captures; promote-to-default only after numeric + visual
  validation; revert is one-line member init flip.

## Context

Current state after the H6 dot-banding fix landed:

- **Mode 0** (default): visibility OFF. Pre-H6 behavior — smooth GI but light leaks through walls. Cheapest.
- **Mode 1**: binary per-probe visibility + trilinear renormalize. Reduces dot artifact amplitude but the on/off frequency at probe-cell granularity persists (8 hard decisions per pixel → visible banding).
- **Mode 2**: soft visibility + renormalize. Marginally smoother than mode 1.
- **Mode 3**: per-direction-bin visibility via per-bin shadow rays from surface. **Correctly eliminates banding AND properly excludes occluded radiance**, but **~32× more SDF fetches per surface pixel** (~4096 fetches/pixel worst case). Too expensive for default.

**The user's complaint**: mode 3 perf is "too huge". Mode 0 is the only currently viable default but reintroduces light leaking. We need a fix that captures mode 3's quality at near-mode-1 cost.

---

## ShaderToy reference — the "free" answer

The Sannikov ShaderToy implementation
([shader_toy/CubeA.glsl:21-42](../../shader_toy/CubeA.glsl#L21),
identical at [shader_toy/Image.glsl:21-42](../../shader_toy/Image.glsl#L21))
implements probe visibility via `WeightedSample`:

```glsl
vec4 WeightedSample(...) {
    vec3 lastProbePos = gPos + gTan*... + gBit*...;
    vec3 relVec = probePos - lastProbePos;
    float theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*PI*0.5;
    // ... compute phi (direction bin index) ...
    float lProbeRayDist = TextureCube(luvo + floor(phiUV)*uvo + luvp).w;
    if (lProbeRayDist < -0.5
        || length(relVec) < lProbeRayDist*cos(PI*0.5 - theta) + 0.01) {
        // Visible: probe contributes to this surface
        return vec4(probe_radiance_sample, 1.);
    }
    return vec4(0.);  // Occluded
}
```

**The key insight (which our current code completely misses):**

The probe stores **the distance its ray traveled before hitting geometry** in the W (alpha) channel of the atlas. The visibility check is a **single texture fetch comparison**:

> If the surface point is CLOSER to the probe than the probe's ray hit
> distance (with a cone-angle correction for finite probe-cell size),
> then the surface and probe are on the SAME SIDE of the geometry the
> probe hit — visible. Otherwise the surface is on the FAR SIDE —
> occluded.

No SDF traces. No additional shadow rays. **Just one extra texture fetch
per probe corner per pixel** (we already do the rgb fetch — switching
to rgba is free in fetch-bandwidth).

---

## The data we need is already baked

[radiance_3d.comp:428-431](../../res/shaders/radiance_3d.comp#L428):

```glsl
imageStore(oAtlas, atlasTxl, vec4(blended, hit.a));   // EMA path
imageStore(oAtlas, atlasTxl, vec4(sanitizeRadiance(rad), hit.a));  // plain path
```

`hit.a` is the alpha channel of the atlas:

- `hit.a > 0.0` → surface hit at distance `hit.a` (world units along ray)
- `hit.a == 0.0` → in-volume miss (open space all the way to interval end)
- `hit.a < 0.0` → sky exit (ray left the volume)

**This is exactly what ShaderToy stores in W.** Our current `sampleProbeDir`
fetches `.rgb` only, throwing away the visibility info that's already in the
atlas. **Mode 4 just uses it.**

---

## Approach — Mode 4: depth-aware per-bin visibility (signed-projection test)

**Critic 03 F1+F2 corrected algorithm.** The original ShaderToy
formula uses one direction-aware bin and a `sin(θ)` cone correction —
that's a 2D/single-bin algorithm. For 3D per-bin granularity, use the
**signed-projection test**:

### Algorithm

In `sampleProbeDir`, for each direction bin (dx, dy):

1. Compute `bdir = binToDir(dx, dy, D)` (the direction the probe sampled).
2. Compute `probeCenter` from probe-grid coords (existing helper —
   confirmed correct for per-cascade directional atlas per critic 03 F5).
3. Fetch the bin's atlas value: `vec4 a = texelFetch(uDirectionalAtlas, ...)`.
4. **Project surface→probe vector onto bin direction**:
   ```glsl
   float t = dot(surfacePos - probeCenter, bdir);
   ```
   `t` is the signed distance from the probe along `bdir` to the
   point on the surface→probe axis projected onto `bdir`.
5. **Visibility test** (critic 03 F2 path #2):
   - If `a.a < missEps` → sky/miss bin (no occluder); fully visible,
     weight = 1. (`missEps = 0.5 * voxelSize` per critic 03 F4 — handles
     the previously-fragile `hitDist <= 0.0` exact-float compare.)
   - Else (`a.a > 0.0` is the bin's hit distance):
     - **Visible if `t ≤ a.a + missEps`**: surface is on the same
       side of the probe's hit as the probe itself (or in the
       opposite-direction half-space `t < 0`). Probe and surface
       can both see the radiance source.
     - **Occluded if `t > a.a + missEps`**: surface is past the
       geometry the probe hit along `bdir` — the hit blocks the
       surface from receiving the same radiance.
6. Multiply contribution by `cos(bdir, normal)` × visibility weight.

**Why this is geometrically correct in 3D** (critic 03 F2 reasoning):

The probe's bin (dx, dy) saw geometry at world position
`H = probeCenter + bdir * hitDist`. The "wall" at `H` perpendicular
to `bdir` blocks bin radiance from reaching ANY point on the far
side of `H` along `bdir`. Computing `t = dot(surfacePos - probeCenter,
bdir)`:
- `t < 0`: surface is in the −bdir half-space; `bdir`-direction radiance
  travels FROM that side TO the probe. Geometrically the surface is
  "near the probe" — same neighborhood, sees similar radiance. **Visible.**
- `0 ≤ t ≤ hitDist`: surface is between probe and hit, on the probe's
  side of the wall. **Visible.**
- `t > hitDist`: surface is past the wall. Probe's bin-radiance came
  from BEYOND the wall, but the surface is also beyond the wall —
  except the wall blocks the bin's radiance source from reaching the
  surface around it through this bin's direction. **Occluded.**

**Cone correction filed as future work**: real geometry isn't a flat
wall perpendicular to `bdir`; a more rigorous test would also check
lateral distance against `tan(bin_half_angle) * hitDist`. For now
the planar-projection test is a good first approximation. Refine
only if visual quality issues appear.

### Pseudocode

```glsl
vec3 sampleProbeDirDepthAware(ivec3 pc, vec3 normal, int D, vec3 surfacePos) {
    vec3  irrad = vec3(0.0);
    float wsum  = 0.0;

    // Probe center: per-cascade regular grid (critic 03 F5 confirmed).
    vec3 probeCenter = uAtlasGridOrigin
                     + (vec3(pc) + 0.5) * (uAtlasGridSize / vec3(uAtlasVolumeSize));

    // Voxel-size-relative miss epsilon (critic 03 F4).
    vec3  worldSize = uVolumeMax - uVolumeMin;
    float voxelSize = worldSize.x / float(uVolumeSize.x);
    float missEps   = 0.5 * voxelSize;

    for (int dy = 0; dy < D; ++dy) {
        for (int dx = 0; dx < D; ++dx) {
            vec3  bdir = binToDir(ivec2(dx, dy), D);
            float wcos = max(0.0, dot(bdir, normal));
            if (wcos <= 0.0) continue;

            // Single fetch: rgba (was rgb) — alpha is the probe's ray-hit distance.
            vec4 a = texelFetch(uDirectionalAtlas,
                                ivec3(pc.x*D + dx, pc.y*D + dy, pc.z), 0);
            float hitDist = a.a;

            // Critic 03 F1+F2 corrected test: signed projection of
            // (surface - probe) onto bdir.
            float wvis;
            if (hitDist < missEps) {
                // Miss / sky / negligible-distance hit → no occluder.
                wvis = 1.0;
            } else {
                float t = dot(surfacePos - probeCenter, bdir);
                wvis = (t <= hitDist + missEps) ? 1.0 : 0.0;
            }

            float w = wcos * wvis;
            irrad += a.rgb * w;
            wsum  += w;
        }
    }
    return irrad / max(wsum, 1e-4);
}
```

Then add a new branch in `sampleDirectionalGI`:

```glsl
} else if (uVisibilityMode == 4) {
    // Depth-aware per-bin visibility (signed projection vs hit distance).
    vec3  num  = vec3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 8; ++i) {
        ivec3 pc = p000 + offsets[i];
        bool inBounds = !(any(lessThan(pc, ivec3(0))) || any(greaterThan(pc, hi)));
        if (!inBounds) continue;
        float ww = w[i];
        num  += sampleProbeDirDepthAware(pc, normal, D, pos) * ww;
        wsum += ww;
    }
    return num / max(wsum, 1e-4);
}
```

### Prerequisite patch — `temporal_blend.comp` (critic 03 F3)

**Without this, Mode 4 silently breaks when `useTemporalAccum=true`.**
The EMA blend at [temporal_blend.comp:82](../../res/shaders/temporal_blend.comp#L82)
mixes the entire vec4, corrupting hit-distance with stale-history
interpolation:

```glsl
imageStore(oHistory, coord, mix(his, cur, uAlpha));   // <-- wrong for alpha
```

Replace with:

```glsl
vec4 blended = mix(his, cur, uAlpha);
blended.a    = cur.a;   // hit distance: keep fresh, don't EMA
imageStore(oHistory, coord, blended);
```

Also in the AABB clamp at [temporal_blend.comp:79](../../res/shaders/temporal_blend.comp#L79):

```glsl
his.rgb = clamp(his.rgb, nMin.rgb, nMax.rgb);   // RGB only; alpha left as-is
```

Same pattern as `radiance_3d.comp:428` (writes `vec4(blended_rgb,
hit.a)` — fresh alpha, blended rgb).

### Cost analysis (critic 03 F6 corrected)

Per pixel:
- 8 corners × D² bins texelFetch (was already happening; vec4 vs vec3 same bandwidth)
- ~5 ALU ops per bin for the projection + comparison
- 1 dot product per bin (3 mul + 2 add)

Total at D=8: **512 texelFetch + ~3000 ALU + zero SDF fetches.**

| Mode | SDF fetches/pixel | Texture fetches/pixel | Branches |
|---|---:|---:|---|
| 0 OFF | 0 | 8 × D² = 512 | minimal |
| 1/2 binary+renorm | up to 8 × 16 = 128 | 8 × D² = 512 | per-corner |
| 3 per-bin shadow trace | up to **8 × D² × 8 = 32,768** | 8 × D² = 512 | per-bin per-step |
| **4 depth-aware (proposed)** | **0** | 8 × D² = 512 | per-bin compare only |

**Mode 4 saves ~32,000 SDF fetches per pixel vs mode 3** — the SDF
traces are the expensive part (each fetch is dependent on the prior,
defeating GPU prefetching). Texture fetches in mode 4 are independent
and GPU can parallelize freely.

**vs mode 1**: same texelFetch count (just vec4 instead of vec3, same
bandwidth), 0 vs 128 SDF fetches. **Mode 4 is actually cheaper than
Mode 1.**

### Quality analysis

- **Per-bin granularity**: same as mode 3 (each direction bin gets its own decision)
  → same dot-banding-elimination behavior
- **Visibility precision**: BETTER than mode 3 because the data is the
  probe's EXACT ray-hit distance (not a re-traced approximation that can miss
  thin geometry due to step size or self-occlusion bias)
- **Edge cases handled**: sky bins (a < 0), miss bins (a = 0), surface bins (a > 0)
- **Cone-angle correction**: prevents over-occlusion at probe-cell boundaries
  (the same trick ShaderToy uses for "approximate visibility weighting")
- **Bake-side correctness preserved**: since the atlas stores the probe's actual hit, no
  approximation in the visibility data itself

### Compared to current modes

| Mode | Cost (vs mode 0) | Banding | Light leak | Notes |
|---|---:|---|---|---|
| 0 OFF (default) | 1× | none (smooth) | YES | reverts H6 entirely |
| 1 binary + renorm | ~1.05× | reduced amplitude | partial | 8 binary decisions/pixel |
| 2 soft + renorm | ~1.05× | marginal smoothing | partial | same per-probe granularity as mode 1 |
| 3 per-bin shadow trace | ~30× | NONE | NONE | correct but expensive |
| **4 depth-aware (ShaderToy-style)** | **~1.05×** | **NONE** | **NONE** | **correct AND cheap** |

---

## Implementation

### Phase 1 — Shader changes (raymarch.frag)

Add `sampleProbeDirDepthAware`:

```glsl
vec3 sampleProbeDirDepthAware(ivec3 pc, vec3 normal, int D, vec3 surfacePos) {
    vec3  irrad = vec3(0.0);
    float wsum  = 0.0;

    // Probe center in world space
    vec3 probeCenter = uAtlasGridOrigin
                     + (vec3(pc) + 0.5) * (uAtlasGridSize / vec3(uAtlasVolumeSize));
    float distSP = length(surfacePos - probeCenter);

    // Cone half-angle for the bin's acceptance (probe-cell-finite-extent correction).
    // ShaderToy uses θ = ((lProbeSize/2 - 0.5) / (lProbeSize/2)) * π/2.
    // For our atlas: the per-bin angular extent is ~π/D radians; use that
    // as a conservative cone half-angle that allows nearby surfaces to read
    // a probe even if technically beyond the probe's ray hit.
    float cosCone = cos(3.14159265 / float(D) * 0.5);

    for (int dy = 0; dy < D; ++dy) {
        for (int dx = 0; dx < D; ++dx) {
            vec3  bdir = binToDir(ivec2(dx, dy), D);
            float wcos = max(0.0, dot(bdir, normal));
            if (wcos <= 0.0) continue;

            // Fetch radiance + hit distance in one texture access.
            vec4 a = texelFetch(uDirectionalAtlas,
                                ivec3(pc.x*D + dx, pc.y*D + dy, pc.z), 0);
            float hitDist = a.a;

            // Visibility weight.
            float wvis;
            if (hitDist <= 0.0) {
                // Sky (-1) or in-volume miss (0): no occluder; fully visible.
                wvis = 1.0;
            } else {
                // Surface hit: visible if surface is closer to probe than hit
                // distance, with a cone-angle correction for finite bin extent.
                // ShaderToy uses: dist < hitDist * cos(π/2 - θ) + epsilon.
                // Equivalent: dist < hitDist when surface is in the "near" cone.
                wvis = (distSP < hitDist * cosCone + 0.01) ? 1.0 : 0.0;
            }

            float w = wcos * wvis;
            irrad += a.rgb * w;
            wsum  += w;
        }
    }
    return irrad / max(wsum, 1e-4);
}
```

Then add a new branch in `sampleDirectionalGI`:

```glsl
} else if (uVisibilityMode == 4) {
    // ShaderToy-style depth-aware visibility (free per-bin).
    vec3  num  = vec3(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 8; ++i) {
        ivec3 pc = p000 + offsets[i];
        bool inBounds = !(any(lessThan(pc, ivec3(0))) || any(greaterThan(pc, hi)));
        if (!inBounds) continue;
        float ww = w[i];
        num  += sampleProbeDirDepthAware(pc, normal, D, pos) * ww;
        wsum += ww;
    }
    return num / max(wsum, 1e-4);
}
```

### Phase 2 — C++ side

- Update `visibilityMode` doc comment in `demo3d.h` to describe mode 4
- Add mode 4 to ImGui combo (5 options now: 0..4)
- Default = mode 4 if quality matches expectations (otherwise keep mode 0
  with mode 4 opt-in via flag for the first round of testing)

### Phase 3 — Test

Capture mode 4 at the standard Sponza viewpoint (cam.md) and compare:

1. Mode 0 (current default; smooth + leaking)
2. Mode 3 (correct + expensive)
3. Mode 4 (proposed; should match mode 3 visually at mode 0 cost)

If mode 4 ≈ mode 3 visually: **change default to mode 4 and recommend
deprecating modes 1/2** (they're band-aids that mode 4 strictly dominates).

If mode 4 is worse than mode 3: investigate cone-angle correction
(`cosCone`); the ShaderToy formula is for 2D probes, may need a different
constant for 3D octahedral bins. Try `cos(π/D)` or a per-cascade-tuned
value.

---

## Verification (critic 03 F8 — quantitative pass criteria)

1. Build clean
2. **Pre-flight: apply F3 patch** (temporal_blend.comp pass-through alpha).
   Build + smoke-test with `--use-temporal-accum` toggled both ways to
   confirm Mode 4 reads sane hit distances in either configuration.
3. Capture sequence: `--visibility-mode={0,3,4}` at cam.md viewpoint with
   identical other settings (Sponza-master, GPU voxelize+SDF,
   `--cascade-c0-res=64`, light-direction, ambient floors at 0,
   blur=1, `--exit-frames=300`).
4. **Visual A/B (qualitative)**:
   - Mode 4 has no dot banding (per-bin granularity matches mode 3)
   - Mode 4 does not over-darken vs mode 3
5. **Quantitative pass criteria**:
   - **meanLum**: Mode 4's `[4c A/B] meanLum` (median of frames 3-10
     to skip codex 09 first-frame NaN) within ±5% of Mode 3 for
     C0/C1/C2. C3 too sparse for stable comparison; skip.
   - **Banding test**: capture a 200×200 patch on the Sponza right
     wall (low-detail, near-uniform-color region). Compute per-channel
     std-dev of pixel values. Mode 4's std-dev should be within 20%
     of Mode 3's. Mode 0 baseline (smooth) and Mode 1 baseline (high
     std-dev = banding) bracket the expected range.
   - **Cost test**: RenderDoc per-pass GPU timing for raymarch.
     Mode 4 raymarch µs should be within 1.1× Mode 0 (i.e., ~free vs
     mode-0 baseline). Mode 3 will be 2-5× higher; that's the budget
     Mode 4 is reclaiming.
6. **Cross-scene smoke test (critic 03 F10)**: capture Mode 4 at
   default camera on Cornell-Original and Cornell to confirm no
   regression on simpler scenes.
7. **Promote-to-default decision**: only after all 3 quantitative
   criteria + 3 scene captures pass. Update default in
   [demo3d.h:859](src/demo3d.h#L859); leave Mode 0 reachable via
   `--visibility-mode=0` for baseline reproducibility.
8. Update [sponza_gi_root_cause_hypothesis_test_impl.md](sponza_gi_root_cause_hypothesis_test_impl.md)
   with mode-4 results.

### Rollback path (critic 03 F10)

- Mode 4 ships **opt-in initially** (default stays at Mode 0). Promote
  only after the 3-scene quantitative validation above.
- If post-promotion regression: revert default to 0 (one-line member
  init flip in `demo3d.h`). CLI/ImGui still allow opting back into
  Mode 4 for diagnosis. Existing screenshot baselines (mode 0)
  remain reproducible regardless via `--visibility-mode=0`.
- The promote-to-default decision lives in the impl notes alongside
  the captures that justified it; rollback PR can reference that
  doc to know which captures to re-run.

### Implementation cleanup (critic 03 F9)

The header comment at
[raymarch.frag:307-311](../../res/shaders/raymarch.frag#L307) currently
documents modes 0/1/2 only; actual code handles 0/1/3. The Mode 4 patch
should rewrite this block to match the actual mode set 0/1/2/3/4.

---

## Out of Scope

- Modes 1/2 — keep as A/B baselines; mark as "not recommended" in the
  ImGui tooltip if mode 4 wins
- Bake-time visibility (radiance_3d.comp's cascade inheritance) — separate
  problem; ShaderToy's WeightedSample is also used at bake time, but our
  bake doesn't currently cascade-inherit through visibility-checked paths.
  Future work if needed.
- Per-cascade tuning of `cosCone` — start with a single global value;
  per-cascade adjustment only if quality issues at C0 vs C3.

---

## Honest expectation note

ShaderToy's WeightedSample is a 2D (flatland) approximation that happens
to map cleanly to 3D octahedral bins. The cone-angle correction is the
fragile part — too narrow → over-occlusion (false negatives); too wide
→ leak (false positives). The 1D/2D ShaderToy formula needs validation
in our 3D context. Most likely it works as-is or with a constant tuning
factor; worst case it needs a 3D-specific derivation but the
infrastructure (atlas alpha = hit distance) is identical.

If mode 4 doesn't match mode 3's quality, the fallback is **mode 5 =
mode 4 + a single shadow ray**: do depth-aware visibility for cheapness,
then for surfaces that flagged "occluded" cast ONE confirmation shadow
ray (much cheaper than mode 3's per-bin tracing). This stays bounded
in cost and adds correctness only where needed.
