# Phase 1 — Implementation Notes: Mode 4 Depth-Aware Per-Bin Visibility

**Date:** 2026-05-13
**Status:** Implemented; smoke-verified only (build clean, shaders compile, mode 4 startup acknowledged). **Quality verification (Steps 0–7) is pending** — no FLIP/RMSE numbers, no per-cascade heatmaps, no RenderDoc timing yet. Mode 4 is opt-in until decision-gate metrics land.

**Critic chain extends:** [critic 05](critic/05_visibility_unified_plan_phase1_impl_review.md) → [reply 05](critic/reply/reply_05_visibility_unified_plan_phase1_impl_review.md). All 7 findings accepted; doc updated below.

**Plan source-of-truth:** [visibility_unified_plan.md](visibility_unified_plan.md) (post-critic-04 revision)
**Algorithm derivation:** [reply 03](critic/reply/reply_03_probe_visibility_acceleration_plan_review.md) (signed projection; superseded the original cone formulation)
**Critic chain:** [critic 04](critic/04_visibility_unified_plan_review.md) → [reply 04](critic/reply/reply_04_visibility_unified_plan_review.md)

Phase 2 (RGBA atlas + α-gated interval merge + bake-time leak fix) was explicitly held back per scope choice — gated on Phase 1 visual A/B.

---

## Summary

| Change | File | Effect |
|---|---|---|
| Prereq: AABB clamp now `.rgb`-only; final store uses `mix` for `.rgb` and `cur.a` for alpha | [res/shaders/temporal_blend.comp:78-89](../../../res/shaders/temporal_blend.comp#L78-L89) | Stops EMA from corrupting hit-distance carried in α; prerequisite for any consumer that reads atlas alpha |
| `uVisibilityMode` doc comment extended to mode 4 | [res/shaders/raymarch.frag:92-98](../../../res/shaders/raymarch.frag#L92-L98) | In-shader documentation |
| New `sampleProbeDirDepthAware(pc, normal, D, surfacePos)` — signed-projection sampler | [res/shaders/raymarch.frag:409-450](../../../res/shaders/raymarch.frag#L409-L450) | Per-bin visibility from probe's stored hit distance; no SDF traces |
| New `uVisibilityMode == 4` branch in `sampleDirectionalGI` | [res/shaders/raymarch.frag:494-506](../../../res/shaders/raymarch.frag#L494-L506) | Trilinear-renormalize over the 8 corners; visibility folded inside the sampler |
| `setVisibilityMode` clamp 0..3 → 0..4; log line updated | [src/demo3d.h:570-577](../../../src/demo3d.h#L570-L577) | Public API accepts the new mode |
| `visibilityMode` doc comment extended | [src/demo3d.h:883-902](../../../src/demo3d.h#L883-L902) | Header documentation |
| ImGui combo extended from 4 to 5 entries; tooltip updated | [src/demo3d.cpp:3632-3645](../../../src/demo3d.cpp#L3632-L3645) | Runtime selectable via UI |
| CLI `--visibility-mode=` comment updated | [src/main3d.cpp:281-289](../../../src/main3d.cpp#L281-L289) | Documents mode 4 (parsing was already integer-generic; clamp lives in `setVisibilityMode`) |

No new shaders, no new GPU resources, no new state members, no atlas format changes. Bake (`radiance_3d.comp`) is untouched.

---

## Algorithm — signed projection (3D-correct, derived in reply 03)

For each direction bin `(dx, dy)` of each of the 8 trilinear corners:

```glsl
vec4  a       = texelFetch(uDirectionalAtlas, ivec3(pc.x*D+dx, pc.y*D+dy, pc.z), 0);
float hitDist = a.a;
float wcos    = max(0.0, dot(bdir, normal));   // cosine weight (existing)

// Signed projection of surface→probe offset onto bin direction:
float t = dot(surfacePos - probeCenter, bdir);

float wvis;
if      (hitDist < 0.0)      wvis = 1.0;                      // sky sentinel (-1)
else if (hitDist < missEps)  wvis = 1.0;                      // miss / negligible-distance hit
else                         wvis = (t <= hitDist + missEps)  // surface on probe-side of geometry
                                    ? 1.0 : 0.0;
```

**Three geometric cases captured by the single `t ≤ hitDist` test:**

- `t < 0` — surface is behind the probe relative to `bdir` → falls through `t ≤ hitDist` → **visible**. (The bin's ray went away from the surface; the surface, being within ~probe-cell distance of the probe, receives similar incident radiance from `bdir`.)
- `0 ≤ t ≤ hitDist` — surface lies between probe and the geometry the probe hit → same side of the occluder as the probe → **visible**.
- `t > hitDist` — surface is past the geometry the probe hit → blocked by the same wall → **occluded**.

`missEps = 0.5 * voxelSize` — same scale-relative epsilon convention used elsewhere in the shader (`probeVisibility`, `sampleProbeDirPerBinOccluded`). Sky bins (sentinel `-1`) and zero/near-zero `hitDist` (volume miss) are treated as fully visible.

**`missEps` near-probe-surface tradeoff** (per critic 05 F2). The branch `hitDist < missEps → wvis = 1.0` conflates two cases:

1. True miss (`hit.a == 0` written by the bake when the ray exited without hitting anything inside the volume).
2. Near-probe hit (`0 < hit.a < missEps`) — the probe hit a surface less than half a voxel away.

The conservative-SDF band ([`sdf_3d.comp`'s `* sqrt(3)/2` subtraction per cerebrum 2026-05-07 do-not-repeat](../../../.wolf/cerebrum.md)) makes sub-voxel hit distances unreliable: the value could be a true near-surface hit or a band-overshoot artifact. Treating both cases as miss is the conservative choice — but the consequence is that **very-near-probe geometry (within ~0.5 voxels of the probe center) does not occlude that bin**.

At current SDF resolution (128³ in a ~4³ volume), `voxelSize ≈ 0.03125` and `missEps ≈ 0.016` world units. In a Sponza corridor where probes can land right against walls, bins pointing toward such a wall fall into this range and become "always visible" for that bin — a small slice of the per-bin contribution that mode 4 cannot occlude. The alternative (running the signed-projection test even for sub-voxel `hitDist`) lands in essentially the same place because `t ≤ hitDist + missEps` with both terms tiny is almost always true. The branch chosen here is the explicit one.

**Why this dominates the cone-correction approach** the original probe plan proposed (`distSP < hitDist*cosCone`):

- The original used `distSP = length(surfacePos − probeCenter)` (always positive, direction-blind). At `D=8` with `cosCone = cos(π/D/2) ≈ 0.98`, the test reduced to `distSP < hitDist + ε`, which is equivalent to "is the surface within `hitDist` of the probe in *any* direction" — geometrically meaningless and pathologically wrong for floor/ceiling pairs (a ceiling probe's downward `hitDist` would over-occlude a floor surface despite the floor being on the visible side along the ceiling's downward bin).
- The signed projection `t = dot(delta, bdir)` is direction-aware. Each bin's visibility is judged against its own ray's hit distance along its own direction.

**Quality mechanism (corrected per critic 05 F1 — the previous "threshold continuity" framing was wrong).** `wvis` is a binary step function — at the boundary plane `t = hitDist + missEps` it jumps from 1.0 to 0.0. There is no continuous transition in the *value* of `wvis`. What is continuous is the *location* of the boundary plane (a wall plane perpendicular to `bdir` at distance `hitDist`), which is fixed in world space — but the surface point's position relative to it varies continuously, so different surface positions trigger the flip at different bins.

The reason mode 4 eliminates visible banding (and modes 1/2 don't) is **renormalization absorbing fractional bin flips**:

- With D=8 there are 64 bins per corner. Each bin has its own `(bdir, hitDist)` and therefore its own flip plane.
- For most surface positions, most bins satisfy either `t ≪ hitDist` (stably visible) or `t ≫ hitDist` (stably occluded). Only the small fraction of bins whose flip plane is near the surface actually transitions as the surface moves.
- When those few bins flip, the per-corner sum changes by a small fractional amount, and the trilinear-renormalize across 8 corners (`num / wsum`) absorbs the change smoothly.
- Mode 1 has no such buffering: `probeVisibility()` is a single per-probe binary decision, so an entire corner's contribution (all 64 bins worth) flips at once → large discontinuous change in corner composition → visible band at the cell boundary.

So Mode 4's banding fix is **fractional-corner flips smoothed by renormalization**, not threshold continuity. The mechanism is the same family as mode 1 + renormalize, but at finer granularity (per-bin instead of per-corner).

**Cone correction** (lateral test against `tan(half_angle) × hitDist`) is omitted in this basic form. Filed as Phase 1.5 work if Step 5 verification reveals under-occlusion in practice.

### Known limitations (per critic 05 F5)

`voxelSize = worldSize.x / float(uVolumeSize.x)` — and therefore `missEps` — assumes the SDF volume is **cubic** (`worldSize.x ≈ y ≈ z`, `uVolumeSize.x ≈ y ≈ z`). Mode 4 inherits this assumption from `probeVisibility` and `sampleProbeDirPerBinOccluded`. Same issue critic 02 C3/C11 flagged for those functions; not introduced by this work, but propagated. If H5 (anisotropic `volumeSize`) is implemented, `voxelSize` needs per-axis derivation or use of the smallest axis as a conservative bound. Phase 2 should audit all three functions when the atlas format changes.

---

## Hard prerequisite — temporal_blend.comp patch

The bake writes `vec4(sanitizedRadiance, hit.a)` to the directional atlas: RGB carries radiance, alpha carries the per-bin hit distance (or sentinels for miss / sky). Mode 4 reads that alpha. Without the patch below, two things go wrong when temporal accumulation is enabled:

1. The AABB clamp at [temporal_blend.comp:79](../../../res/shaders/temporal_blend.comp#L79) clamped the entire `vec4` against the per-channel min/max of color statistics, which has no relationship to valid hit-distance ranges.
2. The final `imageStore(oHistory, coord, mix(his, cur, uAlpha))` EMA-blended `his.a` with `cur.a` — "blend an old hit distance with a new one" produces interpolated nonsense at hit/miss flicker boundaries.

**Patch (already landed):**

```glsl
// Clamp history to AABB — RGB only; alpha has its own valid range
// (negative sky sentinel, 0 = miss, positive = hit distance).
his.rgb = clamp(his.rgb, nMin.rgb, nMax.rgb);

// EMA-blend RGB but use fresh alpha. Same convention radiance_3d.comp:428
// already uses on the bake-write side.
vec4 blended = mix(his, cur, uAlpha);
blended.a    = cur.a;
imageStore(oHistory, coord, blended);
```

This convention now matches `radiance_3d.comp:428`'s bake-side rule (`vec4(blended_rgb, hit.a)` — fresh alpha, blended rgb), and is the same discipline Phase 2 will inherit when α becomes the transparency channel.

---

## Dispatch wiring in `sampleDirectionalGI`

Mode 4 mirrors mode 3's structure: trilinear-renormalize across 8 corners, no outer `probeVisibility()` gate, visibility folded into the sampler.

```glsl
} else if (uVisibilityMode == 4) {
    // Per-bin depth-aware path: visibility from probe's stored hit distance
    // via signed projection. No outer probeVisibility gate (would double-count;
    // visibility is per-bin inside the sampler).
    float ww = w[i];
    num  += sampleProbeDirDepthAware(pc, normal, D, pos) * ww;
    wsum += ww;
}
```

Out-of-bounds corners (probe coord outside the atlas grid) skip the entire corner contribution as in modes 1–3 — the renormalization in `num/wsum` lets the visible corners carry the missing weight.

---

## C++ surface

### Setter ([demo3d.h:570-577](../../../src/demo3d.h#L570-L577))

```cpp
void setVisibilityMode(int m) {
    if (m < 0) m = 0;
    if (m > 4) m = 4;                             // was: m > 3
    visibilityMode = m;
    std::cout << "[Demo3D] visibilityMode=" << visibilityMode
              << " (0=OFF 1=binary-renorm 2=soft-renorm"
              << " 3=per-bin-shadow 4=per-bin-depth-aware)\n";
}
```

### ImGui combo ([demo3d.cpp:3632-3645](../../../src/demo3d.cpp#L3632-L3645))

```cpp
const char* visibilityModeLabels[5] = {
    "0: OFF (pre-H6; smooth but leaks; cheapest)",
    "1: binary + renormalize (smaller dots; partial fix)",
    "2: soft + renormalize (marginally smoother than 1)",
    "3: per-direction-bin shadow trace (CORRECT; ~32x SDF cost)",
    "4: per-direction-bin depth-aware (CORRECT; near-mode-0 cost)"
};
int visModeEdit = visibilityMode;
if (ImGui::Combo("Probe visibility mode", &visModeEdit, visibilityModeLabels, 5))
    setVisibilityMode(visModeEdit);
```

Tooltip extended to describe mode 4 ("per-bin via signed projection against the probe's stored hit distance — same per-bin granularity as mode 3, no SDF traces, near-mode-0 cost") and notes the temporal_blend.comp prerequisite.

### CLI ([main3d.cpp:281-289](../../../src/main3d.cpp#L281-L289))

The CLI parser was already integer-generic — only the inline comment needed updating:

```cpp
} else if (arg.rfind("--visibility-mode=", 0) == 0) {
    // 0=OFF, 1=binary+renorm, 2=soft+renorm,
    // 3=per-bin shadow trace (correct + expensive),
    // 4=per-bin depth-aware via stored hit distance (correct + cheap).
    int v = std::atoi(arg.substr(18).c_str());
    demo->setVisibilityMode(v);
    std::cout << "[MAIN] --visibility-mode=" << v << "\n";
}
```

The clamp lives in `setVisibilityMode`, so `--visibility-mode=99` silently saturates to 4 with the same log line as `=4` (existing behavior preserved).

---

## Cost (predicted; verify in Step 5)

| Mode | Per-pixel work | vs mode 0 |
|---|---|---|
| 0 OFF | 8 corners × D² bins × `vec3` fetch | baseline |
| 1 binary corner gate | + 8 SDF traces (per-corner shadow ray) | small |
| 3 per-bin shadow trace | + 8 × D² SDF traces | large (~30×) |
| **4 depth-aware (signed projection)** | 8 × D² × `vec4` fetch (bandwidth ~unchanged; many drivers pad GL_RGB8 to 32-bit anyway) + 1 dot + 1 compare per bin | **expected small; verified in Step 5** |

The original probe plan claimed "~5 ALU + length per bin"; the corrected algorithm is leaner — 1 dot product (`dot(delta, bdir)`) plus 1 compare per bin. `delta = surfacePos - probeCenter` is computed once per (pixel, corner) outside the bin loop, so the per-bin work is genuinely just `dot + compare`.

The "~1.05×" figure from earlier plan revisions is **not** asserted here — the table says "expected small" and Step 5 (RenderDoc capture) is what establishes the actual ratio.

---

## Verification

### Done — smoke level only

This is the floor, not a quality claim. What's verified:

- **Build clean.** Release build succeeds with only pre-existing warnings (encoding C4819, signed/unsigned C4018, sscanf C4996, int→float C4244). No new warnings introduced.
- **Shaders compile at runtime.** `--visibility-mode=4` startup acknowledged: `[Demo3D] visibilityMode=4 (0=OFF 1=binary-renorm 2=soft-renorm 3=per-bin-shadow 4=per-bin-depth-aware)`. All 13 shader files load successfully (including the patched `temporal_blend.comp` and `raymarch.frag` with the new sampler). Log: [tools/app_run_mode4_smoke.log](../../../tools/app_run_mode4_smoke.log).

What is **not** verified yet: visual quality, FLIP/RMSE against mode 3, RenderDoc timing, per-cascade behavior. No quality claims should be inferred from "smoke OK."

### To do (per the unified plan's decision-gate protocol)

- **Step 0 — baseline regen.** Recapture mode 0 and mode 3 against current main at the cam.md viewpoint (Sponza interior + Cornell closed-box). Don't compare mode 4 against pre-Phase-1 captures — recent OBJ/camera changes have shifted the reference.
- **Step 2 — A/B captures.** `--visibility-mode={0, 3, 4}` at the same viewpoint.
- **Step 3 — visual A/B.** Mode 4 should match mode 3 (no dot banding, no over-darkening) and visibly beat mode 0 (no light leak through walls).
- **Step 4 — decision-gate metrics:**
  - **Primary:** FLIP < 0.05 between mode 4 and mode 3 reference at cam.md, Sponza.
  - **Secondary:** per-region RMSE on three crops (lit floor, shadowed alcove, vertical wall column) < 0.02.
  - **Failure threshold:** FLIP > 0.10 or any region RMSE > 0.05 → escalate.
- **Step 5 — RenderDoc timing.** Capture mode 0 and mode 4 raymarch pass cost. Record absolute ms and the actual ratio.
- **Step 6 — per-cascade visibility heatmaps.** For each cascade C0..C4, render `wvis` as the output color (instead of weighted radiance) and confirm: (a) `wvis` not always 1, (b) not always 0, (c) spatial distribution of `wvis=0` correlates with where geometry occludes probes at that cascade's spacing.
- **Step 7 — document results** in [sponza_gi_root_cause_hypothesis_test_impl.md](sponza_gi_root_cause_hypothesis_test_impl.md).

### Decision-gate outcomes

| Outcome | Next step |
|---|---|
| Pass primary + secondary, RenderDoc cost increase < 20% | Default = mode 4. Schedule Phase 2 as the long-term correctness fix. |
| Pass primary + secondary, but bake-time leaks visible (light bleeds through walls regardless of render mode) | Proceed to Phase 2 — only the α-gated bake merge can fix this. |
| Fail primary or secondary, before cone refinement | Investigate Phase 1.5 cone correction (`tan(half_angle) × hitDist` lateral test). One commit; rerun verification. |
| Fail primary or secondary, after cone refinement | Skip default flip; proceed to Phase 2 immediately. |

Mode 5 = mode 4 + 1 confirmation shadow ray was removed from this gate per critic 04 C6 — it was a deferral disguised as a plan branch.

---

## Architecture decisions (recap from the unified plan)

- **Atlas format unchanged.** Mode 4 reuses the alpha channel that the bake already writes; no `GL_RGB8 → GL_RGBA8` migration needed in Phase 1. Phase 2 will change it as part of the interval-merge refactor.
- **Bake (radiance_3d.comp) untouched.** Phase 1 is render-side only.
- **`uVisibilityMode` switch retained.** Modes 0–4 coexist for A/B comparison through the decision gate. Phase 2's α-gated merge will deprecate the entire switch.
- **No new GPU resources, no new uniforms, no state-member additions.** Surface area bounded to two shaders + three small C++ edits.
- **Default mode unchanged (still 0).** Plan deliberately holds the default flip until verification metrics land. **User-facing consequence (per critic 05 F3):** until Steps 0–5 verification passes, the default behavior remains pre-H6 — light leaks through walls. Mode 4 is opt-in via `--visibility-mode=4` or the ImGui combo. **Timeline:** default flip to mode 4 scheduled for the commit that lands the FLIP/RMSE captures and confirms primary < 0.05 + secondary < 0.02. If primary fails, default stays 0 and Phase 2 (interval atlas) advances on its own merit.

---

## Out of scope (deferred or rejected)

| Item | Status |
|---|---|
| Phase 1.5 cone correction (`tan(half_angle) × hitDist` lateral test) | Filed; only triggered by failed verification |
| Phase 2 (atlas RGBA + α-gated interval merge + bake-time leak fix) | Held back; gated on Phase 1 decision gate |
| Mode 5 (mode 4 + 1 confirmation shadow ray) | Removed from decision gate per critic 04 C6 |
| Half-resolution visibility (Strategy 6) | Deferred; revisit if Phase 2 per-bin α-fetch becomes bandwidth-bound |
| Soft α (0..1) instead of binary | Future Phase 2 refinement; explicit decision required |
| Phase 2 atlas memory measurement (GL_RGB8 vs GL_RGBA8 actual on-GPU footprint) | Phase 2 task — not relevant to Phase 1 |
| Hoist `probeCenter` / `delta` computation out of the per-corner sampler call (critic 05 F6) | Negligible at D=8 (sub-1% overhead vs D²=64 texelFetches); revisit only if D ever exceeds 16 |

---

## Files changed (commit boundaries)

**Commit A (prerequisite):**
- `res/shaders/temporal_blend.comp` — `.rgb`-only AABB clamp + fresh-`cur.a` final store
- **Cross-mode safety (per critic 05 F7):** the `blended.a = cur.a` change is invisible to modes 0–3 — those modes never read the directional atlas's alpha channel; only mode 4 reads `a.a` as `hitDist`. Commit A is therefore safe to land independently of commit B and remains useful even if commit B is reverted (any future consumer of atlas alpha — including Phase 2's interval merge — needs the same fresh-alpha discipline).

**Commit B (Mode 4):**
- `res/shaders/raymarch.frag` — `uVisibilityMode` doc, new `sampleProbeDirDepthAware`, dispatch branch
- `src/demo3d.h` — `setVisibilityMode` clamp + log line + `visibilityMode` doc comment
- `src/demo3d.cpp` — ImGui combo (4 → 5 entries) + tooltip
- `src/main3d.cpp` — `--visibility-mode=` inline comment

Two-commit shape preserves the rollback story from the plan: commit A is independent (helps any future consumer of atlas alpha, including Phase 2), commit B can be reverted alone if mode 4 introduces issues without losing the temporal-blend fix.
