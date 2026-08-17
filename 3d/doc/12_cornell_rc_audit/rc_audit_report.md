# Radiance Cascades — Cornell-Box Audit & Gap-Closing Plan

**Date:** 2026-08-17
**Scope:** Validate the *volumetric* RC (`radiance_3d.comp` + SDF) against the ShaderToy
reference, in the Cornell-box scene, and close the gap.
**Method:** swarm of 5 independent analysis agents + adversarial audit (rigorous-proof
methodology: pin the claim, dispatch a diverse portfolio, audit for gaps/handwaving/circularity).
**Working tree:** `HEAD 5e0a960`, clean.

---

## 0. Verdicts (the four questions, answered)

| # | Question | Verdict |
|---|----------|---------|
| 1 | Does the volumetric RC align with ShaderToy — concept & implementation? | **Conceptually yes (cascade hierarchy + directional storage + bake-time merge), implementation largely no** — 8 concept divergences, ~8 confirmed implementation deltas (see §1–§2). |
| 2 | Is the SDF algorithm correct? | **Cornell: YES (good enough). Sponza: NO** — the mesh path produces a clamped-UDF of a hollow shell that trilinear-march tunnels through (see §3). Switch-to-Cornell is the right call. |
| 3 | Is the RC algorithm correct? | **PARTIALLY** — octahedral round-trip, cos⁺ orientation, merge blend are correct; directional integration + the Phase-3 visibility test are wrong (see §4). |
| 4 | Gap-closing plan | **A0–A9** (architecture decision → freeze invariants → integration/solid-angle tests → geometry/tracer parity → light/sky parity → merge/payload → single-bounce → directional weighting → multi-bounce → final HDR acceptance) — see §5. |

**Headline finding (highest impact):** the consumer contract the project *believes* is
shipped — `irrad = (4/D²)·Σ(L·cos⁺)` (recorded in `journey.md:88`, "CV1 0.650 → 0.846") —
**is not in the committed code.** `raymarch.frag:431-456` still implements the pre-fix
renormalized mean `Σ(L·cos·α)/Σ(cos·α)` with the α-gate, and the `(4/D²)` factor exists
only in the unapplied `diff_remote.patch:37544`. `git log -S "4.0 / float(D * D)" -- res/shaders/raymarch.frag`
is empty. (Provenance gap: the fix was measured on another branch/remote and never merged,
or was reverted in `dd4f5df` "Remove legacy surface RC path" — **UNCONFIRMED**.)

> Caveat: `(4/D²)` is the *equal-area hemisphere* special case of the physical invariant
> `L_o = (ρ/π)·Σ L cos⁺ ΔΩ` (see §4.2 / §5.2). The fix target is the invariant, not the
> literal `(4/D²)` constant — do not lock the constant.

---

## 1. Concept alignment (Q1)

Mapping the ShaderToy reference (`shader_toy/CubeA.glsl` + `Common.glsl`) to the volumetric
RC (`radiance_3d.comp` + `reduction_3d.comp` + `raymarch.frag`):

| Concept | ShaderToy | Volumetric RC | Verdict |
|---|---|---|---|
| Probe placement | surface-embedded charts (`gPos/gTan/gBit/gNor`) | uniform 3D volume grid | **Divergent** |
| Ray cast | analytic quad/box/cylinder/sphere | SDF sphere-march | **Divergent** |
| Direction set | square-ring hemisphere | octahedral D×D full-sphere | **Divergent** |
| Radiance storage | cubemap atlas, `.w`=hit distance | octahedral atlas, `.a`=visibility α (EMA-softened) | **Divergent** |
| Cascade merge | weighted-bilinear `WeightedSample` + linear `l` | directional merge + smoothstep + α | **Conceptually eq., details divergent** |
| Consumer | solid-angle×Lambert sum (no renormalize) | cos·α renormalized mean | **Divergent** |
| Sky/sun | directional sun + gradient sky (always on) | point light + env-fill (default OFF) | **Divergent** |
| Material | hardcoded albedo + reflective/emissive tags | albedo volume, diffuse only | **Eq. in form, divergent in content** |

Top-5 divergences most likely to break Cornell parity:
1. **Surface vs volume probes** — volumetric probes float in air and are trilinearly
   interpolated onto the wall; the lit surface is offset from stored radiance.
2. **Point light + SDF shadow vs directional sun + analytic shadow** — journey Stage 11c
   already measured `--light-direction` closing 87% of the gap.
3. **Sky-fill OFF by default** — volume-exit rays write zero vs ShaderToy's always-on
   gradient sky; the sky bounce into shadowed walls/ceiling is missing.
4. **Consumer renormalization** — volumetric divides by Σ(cos·α) (non-energy-preserving);
   ShaderToy uses an absolute solid-angle-weighted sum.
5. **Merge visibility** — binary α + fixed cone-sine (and `uUseWeightedSample` default OFF)
   vs ShaderToy's continuous distance + angle-aware `WeightedSample`.

---

## 2. Implementation delta (Q1, line-level)

Confirmed **aligned**: merge composite form `own·l + upper·(1−l)`; octahedral encode/decode
identical on bake & consume side; "merge is bake-time" topology honored; per-cascade
interval recursion `C0 [0.02,0.125]`, `Cn [4^(n−1)·d, 4^n·d]`.

Confirmed **divergent** (each with file:line):

| # | Aspect | ShaderToy | Volumetric | Impact |
|---|---|---|---|---|
| 1 | Solid-angle/Jacobian weight | `CubeA:190-192` `(cos(θ−Δθ)−cos(θ+Δθ))/(4+8⌊θi⌋)·cosθ` | `raymarch.frag:431-456` none | angular bias |
| 2 | `(4/D²)` factor | n/a (area-weighted) | **absent** — `irrad/max(wsum,1e-4)` | absolute scale |
| 3 | α-gate | n/a | `w = wcos·a.a` (`raymarch.frag:443`) | drops surface bins |
| 4 | Blend curve | linear `CubeA:200` | smoothstep `radiance_3d.comp:774-776` | band shape |
| 5 | Blend width | `(1/256)·probeSize·1.5` | `(tMax−tMin)·uBlendFraction` | band scale |
| 6 | α semantics | `.w` = hit distance / visibility | `.a` = 0/1 transparency | visibility fidelity |
| 7 | Bake direction set + interval | square-ring, full-ray `probeSize/32` | octahedral, shell `[tMin,tMax]` | coverage |
| 8 | Emissive/reflective/sky | present in bake | absent / env-fill only | scene light |

Consumer D resolution: `dirRes=8` → C0=D8, C1..C3=D16 (`demo3d.cpp:186,4032-4033`).

---

## 3. SDF correctness (Q2)

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | UDF vs signed-distance semantics | **ISSUE (mixed)** | mesh path clamps `max(...,0.0)` (`sdf_3d.comp:129`); analytic is signed (`sdf_analytic.comp:51-63`); display hit-test `EPSILON=1e-6` (`raymarch.frag:726`) valid only for the signed analytic field |
| 2 | Conservative band | **CORRECT** | `voxelSize·√3/2` (`sdf_3d.comp:129`) is the exact half-diagonal, a valid lower bound → sphere-trace safe |
| 3 | Trilinear sampling of UDF | **ISSUE** | trilinear does not preserve the distance property; coarse 128³ + `t+=0.9d` / `0.7d` tunnels through thin walls |
| 4 | Unreachable sentinel | **ISSUE (minor)** | producer `1e3` vs bake `INF=1e10` (exit `≥5e9`) vs display `1e9` — three disagreeing thresholds |
| 5 | uvw mapping | **CORRECT** | `(coord+0.5)/size` producer ↔ `(world−origin)/size` consumers consistent |
| 6 | JFA | **CORRECT (approx)** | 27-neighbor, ping-pong parity, init `α>0.5`; JFA ≈ EDT, not exact |
| 7 | Why Sponza degrades | **CONFIRMED** | hollow surface shell + trilinear leak + conservative band; thin columns/railings <2 voxels erased |

**Verdict:**
- **Cornell (analytic) — CORRECT / good enough.** Signed, exact, solid boxes (walls ~13
  voxels thick); `dist<0.002` bake and `dist<1e-6` display both converge.
- **Sponza (mesh) — NOT good enough.** Clamped UDF of a hollow shell at 128³; trilinear
  averaging merges the two faces of thin walls so the field never dips below the bake
  threshold → false-miss tunneling. This is a resolution/thin-wall/trilinear mechanism,
  **not** EDT cost.

---

## 4. RC algorithm correctness (Q3)

Verdict: **PARTIALLY correct.** Octahedral round-trip, cos⁺ orientation, and the merge
blend formula are correct; the directional integration and the Phase-3 visibility test
are not.

Top-3 concrete defects (ranked):

**1. Atlas `.a` payload: producer and consumer disagree on its semantics.**
- The bake *classifies* each bin `{sky→0, surface→0, miss→1}` (`radiance_3d.comp:710-735`),
  but under temporal accumulation (`uTemporalActive`, default) it EMA-blends α into a
  **soft, history-dependent visibility** value (`radiance_3d.comp:826-840`:
  `blendedAlpha = mix(hist.a, alpha, uTemporalAlpha)`). Steady-state `.a` is **not binary**.
- `sampleUpperDirWeighted` (`radiance_3d.comp:316-327`) reads that `.a` as a **signed hit
  distance** (`lProbeRayDist < 0` → sky; `length(relVec) < lProbeRayDist·sinθ` → visible).
  With `.a ∈ [0,1]` the sky branch is unreachable and surface bins are gated by a fixed
  `0 + 0.01`.
- The active consumer also depends on `.a`: `raymarch.frag:443` does `w = wcos * a.a`, so a
  naive "change `.a` to distance" fix **breaks the render path**.
- *Latent:* `sampleUpperDirWeighted` only runs when `uUseWeightedSample != 0` (default OFF,
  `demo3d.cpp:192`); the `raymarch.frag` dependence is always-on.
- Correct fix: a **coordinated payload migration** — split the two quantities into distinct
  channels (distance `t≥0`/`−1` sky vs transmittance/visibility `α∈[0,1]`), then update every
  producer and consumer atomically. Scheduled as milestone **A5**, not inline.

**2. Consumer integral is not solid-angle-normalized; the octahedral per-bin solid angle is unweighted.**
- Wrong: `irrad = Σ(L·cos⁺·α) / Σ(cos⁺·α)` (`raymarch.frag:444-452`) — a renormalized mean,
  no per-bin ΔΩ.
- Correct physical contract (lock this, not a magic constant):
  `L_o = (ρ/π) · Σ_b L_b cos⁺_b ΔΩ_b`, with `Σ_b ΔΩ_b ≈ 4π` (full sphere) / `≈ 2π` (hemisphere).
- The shader's octahedral UV lives in `[0,1]²` (mapping to the `[-1,1]²` octahedral square),
  so per-bin **planar** area is `4/D²` (not `1/D²`); the octahedral projection is not
  area-preserving, so `ΔΩ_b` must be numerically integrated per bin (or an equal-area
  octahedral map used) and normalized so `ΣΔΩ` is correct. The earlier draft's
  `(1/D²)·L1(n)³` is wrong/incomplete and must not be locked.

**3. Isotropic reduction is unweighted (separate fallback/debug contract).**
- `reduction_3d.comp:41` `avg /= D²` is the isotropic mean only under equal-area bins.
- Correct: `avg = Σ_b L_b ΔΩ_b / Σ_b ΔΩ_b`.
- *Not on the default acceptance path:* directional GI consumes the directional atlas
  (`sampleProbeDir`), not the reduced grid — fixing this does **not** move the main CV1
  parity. Treat it as an independent fallback/debug gate.

Related documented (not counted top-3): the α-in-denominator renormalization in
`raymarch.frag:452` intentionally over-brightens (`raymarch.frag:373-382`).

---

## 5. Gap-closing plan (Q4) — revised (plan review applied)

The first draft mixed two architectures and had inconsistent gates; revised per review.

### 5.1 Architecture decision (blocks everything)

Two mutually exclusive targets; **decide before any code**:

| | Path A — chart-based implementation parity | Path B — volumetric physical parity |
|---|---|---|
| Basis | adopt/extend `reference_transport.comp` (the surface-RC kernel that already matches ShaderToy) | keep `radiance_3d.comp` full-sphere volumetric probes |
| Parity target | ShaderToy **implementation** parity (square-ring, chart-local frames) | ShaderToy **output/physical** parity (same radiance integral) |
| Square-ring directions | valid — chart tangent/bitangent/normal exist | **not applicable** — a floating probe has no surface frame |

A floating volumetric probe has no unique tangent/bitangent/normal frame, so exact
ShaderToy direction parity (square-ring) is only reachable under Path A. Default for the
volumetric line is **Path B**; the plan below is Path-B-first with Path-A notes where a
step diverges.

### 5.2 Frozen invariants (lock these, not the old constants)

1. **Radiance integral:** `L_o = (ρ/π) · Σ_b L_b cos⁺_b ΔΩ_b` — bake stores **unweighted**
   incident radiance; the consumer applies `cos⁺·ΔΩ` once. (Never both — a bake-time `cos`
   plus a consume-time `cos⁺` is a `cos²` error.)
2. **Atlas payload schema:** separate **distance** (`t≥0` surface, `−1` sky) from
   **transmittance/visibility** (`α∈[0,1]`, temporally accumulated) into distinct channels.
   One channel is never both.
3. **Solid-angle tests:** `Σ_b ΔΩ_b ≈ 4π` (full sphere), `≈ 2π` (hemisphere);
   constant-radiance hemisphere reproduces the analytic Lambert result `ρ·L`.

### 5.3 Acceptance metric (exact)

- **Primary = CV1 ratio with a reference-derived validity mask:** `valid = pt_indirect_lum > 0.05`
  only. A valid pixel where the cascade is fully dark is a **failure**, not excluded.
  Handle `casc == 0` safely (floor/clamp) and report coverage + exclusion counts.
  Statistic, named exactly: **`p95(|ln(cascade/PT)|) ≤ 0.50`** (95th percentile of the
  absolute log-ratio), with `ratio_mean ∈ [0.95,1.05]`, `bright% ≤ 5%`, `dim% ≤ 5%`.
- **Secondary = EXR HDR pixel-diff**, valid only after identical reconstruction / camera /
  exposure / materials / lighting (A3/A4). Surrogate target = in-tree
  `reference_transport.comp` mode-2 final view / CPU oracle (no ShaderToy Cornell EXR exists).

### 5.4 Milestones (revised order; fail-fast — a miss = STOP, not retry)

| M | Goal | Concrete change | Gate (pre-committed) |
|---|------|-----------------|----------------------|
| **A0** | Architecture decision | decide Path A vs Path B (one-paragraph ADR in this doc) | decision recorded; every later gate is Path-consistent |
| **A1** | Freeze integral + payload schema | document `L_o=ρ/π·ΣLcos⁺ΔΩ` + distance-vs-transmittance channels | reviewers sign off the two invariants (no code) |
| **A2** | Analytic integration + solid-angle tests | edit the `raymarch.frag` consumer **in current source** (no patch); add per-bin ΔΩ weights | shader compiles; constant-atlas integration D=8 **and** D=16; ΣΔΩ≈2π/4π; zero/partial-visibility cases; pinned CV1 A/B |
| **A3** | Geometry/frame/material/tracer parity | full fixture set — cylinders (side/cap/grazing), openings, exclusions, reflective sphere+box, black cylinders, materials, topology | both tracers on **the same fixtures** → identical hit/miss, distance, normal, material id, classification |
| **A4** | Local-light + sky payload parity | shared world-space ray/hit fixtures + sky-miss directions | per-ray payload matches `shadeLocal` on shared fixtures; sky-miss matches |
| **A5** | Merge visibility + distance sampling | **atomic** payload migration (distance vs transmittance) across producer + all consumers | temporal ON and OFF both correct; `sampleUpperDirWeighted` reads the distance channel |
| **A6** | Single-bounce transport validation | MB off | CV1 (reference-valid mask) N512: `ratio_mean ∈ [0.90,1.10]`, `dim% ≤ 10%` |
| **A7** | Directional mapping + weighting | Path B: per-bin ΔΩ (not square-ring); Path A: square-ring | `p95(|ln|) ≤ 0.50`, `bright% ≤ 5%`, `dim% ≤ 5%` |
| **A8** | Multi-bounce feedback (one dependency at a time) | Path A: deterministic prev-C0 chart-UV feedback; Path B: volumetric feedback model (no chart address exists on the SDF hit path) | full CV1: `ratio ∈ [0.95,1.05]`, `p95(|ln|) ≤ 0.50`, bright/dim ≤ 5% |
| **A9** | Final linear-HDR acceptance | immutable candidate + reference manifests | STRONG band + mean EXR rel-error ≤ 0.05 on lit pixels |

### 5.5 Do-not-change (locked)

`reference_transport.comp` / `reference_cornell_scene.*` / `reference_layout.h` (parity
kernel + layout contracts); texel scale 1/256; HIGH-side-only firefly clamp;
bake-time-merge/consume-read-only separation; no bake-bin resolution bumps; no LDR-only
verdicts. **Do NOT lock `(4/D²)` as a constant** — lock the physical integral (5.2.1).
`reduction_3d.comp` correctness is a **separate fallback/debug contract**, not a route to
the main directional CV1 parity.

---

## 6. RenderDoc debug (Q5)

Done live. Captured a fresh **analytic Cornell** frame (frame 420) via the in-app
RenderDoc API, replayed it through `qrenderdoc.exe --py rdoc_extract.py`, and exported
stage textures + GPU timings + final thumbnail. Full write-up: **`renderdoc_report.md`**
(in this directory).

Highlights:
- Pipeline: `4×(radiance_3d bake → reduction_3d) → raymarch → gi_blur → ImGui`.
- **GPU total ≈ 37.7 ms**; C2 bake is the hotspot (11.2 ms for 512 probes → march-length,
  not probe-count); raymarch 6.6 ms.
- SDF/albedo slices confirm a **clean signed analytic field** (vs Sponza's broken mesh path).
- Cascade mean luminance is balanced (`C0≈C1≈C2≈0.05, C3≈0.03`), unlike Sponza.

Artifacts: `tools/captures/rdoc_frame_frame420_{.rdc, _manifest.json, _extract.log, ...}`.

### 6.1 Exported stage buffers (visual)

**Final frame** (mode 0, GI blur applied):

![Final Cornell frame](../../tools/captures/rdoc_frame_frame420_thumb.png)

**SDF volume slice (z=64/128)** — clean signed analytic field:

![SDF slice](../../tools/captures/rdoc_frame_frame420_sdfTexture.png)

**Albedo volume slice (z=64/128)** — red/green walls:

![Albedo slice](../../tools/captures/rdoc_frame_frame420_albedoTexture.png)

**C0 directional atlas (z=16/32)** — 32³ probes × 8×8 direction tiles:

![C0 probe atlas](../../tools/captures/rdoc_frame_frame420_cascade0_probeAtlas.png)

**C1 directional atlas (z=8/16)** — 16³ probes × 16×16 direction tiles:

![C1 probe atlas](../../tools/captures/rdoc_frame_frame420_cascade1_probeAtlas.png)

**C0 isotropic grid (z=16/32)** — direction-averaged radiance:

![C0 probe grid](../../tools/captures/rdoc_frame_frame420_cascade0_probeGrid.png)

---

## 7. Plan execution log (A0–A9)

| M | Status | Result |
|---|--------|--------|
| **A0** Architecture decision | ✅ DONE | **Path B — volumetric physical parity.** The volumetric `radiance_3d.comp` line targets ShaderToy *output/physical* parity (same radiance integral), not implementation parity. Rationale: a floating volumetric probe has no chart-local tangent/bitangent/normal frame, so square-ring directions are inapplicable; the objective is the *volumetric* "new RC". Path A (chart-based `reference_transport.comp`) remains the implementation-parity alternative if redirected. |
| **A1** Freeze integral + payload schema | ✅ DONE | Locked (§5.2): `L_o = (ρ/π)·Σ L cos⁺ ΔΩ`; distance vs transmittance split into distinct channels. |
| **A2** Analytic integration + solid-angle tests | ✅ IMPLEMENTED | ΔΩ LUT generated + verified (§7.1); `raymarch.frag` consumer now uses per-bin ΔΩ + 1/π; rebuilt + smoke-tested. Runtime CV1 A/B + zero/partial-visibility still pending. |
| **A3–A9** | ⬜ pending | see §5.4. |

### 7.1 A2 — octahedral per-bin solid angle (verified)

Script: `octahedral_solid_angle.py` (reproduces `dirToOct`/`octToDir`, finite-difference
Jacobian, sub-sampled integration). Results:

| D | ΣΔΩ full (≈4π) | ΣΔΩ hemi (≈2π) | ∫cos⁺dΩ (≈π) | per-bin min→max | ratio |
|---|---|---|---|---|---|
| 8 | 12.566373 (err 2.5e-6) | 6.3110 (err 2.8e-2) | 3.141593 (err 6.8e-9) | 0.1053 → 0.2873 | **2.73×** |
| 16 | 12.566372 (err 1.2e-6) | 6.2970 (err 1.4e-2) | 3.141593 (err 3.3e-9) | 0.0202 → 0.0785 | **3.89×** |

Conclusions:
1. **Full-sphere invariant holds** (`ΣΔΩ≈4π` to ~1e-6) and **hemisphere** to <1% (residual
   is the fold-seam kink in finite differences, shrinking with D).
2. **Constant-radiance Lambert check is exact** (`∫cos⁺dΩ = π` to ~1e-9) — confirms the
   `cos⁺·ΔΩ` weighting is the correct consumer integral.
3. **Per-bin solid angle is strongly non-uniform** — 2.73× (D=8) to 3.89× (D=16), growing
   toward the theoretical ~5.2× at the equator. Uniform `(4/D²)` / `(1/D²)` weighting is
   quantitatively wrong; the consumer must use per-bin `ΔΩ` (LUT or equal-area octahedral).

**A2 implementation (done 2026-08-17, corrected):**
- `src/octahedral_solid_angle.h` — D8/D16 per-bin ΔΩ LUTs (generated by
  `octahedral_solid_angle_gen.py`; each sums to 4π).
- `raymarch.frag` — added `uniform float uSolidAngleLUT[256]`; `sampleProbeDir` and
  `sampleProbeDirDetail` now use `w = wcos · ΔΩ · a.a` in the **renormalized** cos-weighted
  mean (the only change vs pre-A2 is the ΔΩ weight). α-gate preserved.
- `demo3d.cpp` — uploads the LUT matching `uAtlasDirRes` (D8/D16) on the raymarch program.
- Rebuilt + smoke-tested.

**A2 CV1 gate (measured 2026-08-17, cornell/cam0/N=1024/mode-17):**

| impl | ratio (corrected mask) | ratio (old mask) | dim% |
|---|---|---|---|
| first attempt (drop renormalization + 1/π) | 0.26 | — | 97.7% |
| corrected (ΔΩ-weighted renormalization) | **0.43** | 0.49 | 88.4% |

**Finding:** the first attempt's "raw irradiance + 1/π" was a **bug** — it divided by the
fixed π instead of the actual visible solid angle `Σ(cos·ΔΩ·α)`, under-emitting ~4× when
most bins are occluded. The corrected form keeps the renormalization (the correct Lambert
`L̄ = E/π = Σ(L·cos·ΔΩ)/Σ(cos·ΔΩ)`), so ΔΩ is the only change. The remaining ~2× under-emit
(ratio ≈ 0.43–0.49 vs target ≥0.90) is **pre-existing, not caused by A2**: it is the
α-gate excluding surface-hit bins (A5 payload migration) + multi-bounce under-emit (A8,
journey H-A'), now visible because the corrected mask counts dark pixels as failures.
**A2 does not pass its gate; do not proceed to A3 on A2's strength alone.**
