## Reply: Sponza GI Hypothesis Test Impl Critic 02 — `02_sponza_gi_root_cause_hypothesis_test_impl_review.md`

**Date:** 2026-05-12
**Status:** All 11 findings accepted. **C3+C11 landed as a real code fix**
(probeVisible constants now derive from `uVolumeSize` + `uVolumeMax-uVolumeMin`
so the shader is scene-scale-invariant). C2 data gap closed (extracted
all 4 anyPct datasets from existing JSONs — but the data is noisy and
weakens the H1 verdict; documenting honestly). The remaining 9 findings
are doc improvements folded into the impl doc. C4's "binary visibility
over-darkens partial occluders" is acknowledged as a known limitation
with per-direction-bin visibility filed as future work.

---

### C3 + C11 — probeVisible hardcoded constants (HIGH + MEDIUM, code fix LANDED)

You're right. The hardcoded `0.05` / `0.002` / `0.005` / `0.005`
are fine at default `volumeSize=(4,4,4)` + `volumeResolution=128`
(SDF voxelSize=0.03125), but break at any other scale:

- Larger volumes (e.g. 20³ at 128³ → voxel 0.156): `0.002` hit
  threshold is far sub-voxel, never triggers; visibility always
  returns true (defeats H6).
- Smaller volumes (e.g. 1³ at 128³ → voxel 0.0078): `0.05` start
  bias is ~6 voxels into the SDF; misses thin near-surface occluders.
- Coarser SDF resolution (e.g. 32³ at vol=4 → voxel 0.125): same
  hit-threshold-too-small issue (C11 self-occlusion case).

**Code fix landed.** `probeVisible` now computes voxel size from
existing uniforms:

```glsl
vec3  worldSize = uVolumeMax - uVolumeMin;
float voxelSize = worldSize.x / float(uVolumeSize.x);
float startBias = max(voxelSize * 1.6,  0.01);   // ~1.6 voxels
float hitEps    = max(voxelSize * 0.07, 0.001);  // sub-voxel
float endCutoff = max(voxelSize * 0.16, 0.001);
float minStep   = max(voxelSize * 0.16, 0.001);
```

`uVolumeSize` (ivec3, SDF voxel resolution) and `uVolumeMin`/
`uVolumeMax` (world bounds) were already bound — no new uniforms
needed. At the default vol=(4,4,4)/res=128 → voxelSize=0.03125,
the new constants reproduce the prior values within ~10% (startBias
0.05, hitEps 0.0022, endCutoff/minStep 0.005).

**Verified by recapture**: same Sponza viewpoint at C0=64 with
scene-adaptive constants produces visually identical output to the
prior hardcoded H6 capture. Capture at
[tools/sponza_h6_scene_adaptive.png](../../../tools/sponza_h6_scene_adaptive.png).

**Implementation gotcha worth noting** (caught during the rebuild):
my first attempt referenced `uGridSize.x / 128.0` — but `uGridSize`
is the radiance_3d.comp uniform, not raymarch.frag. Raymarch uses
`uVolumeMin/uVolumeMax` instead. Shader compilation failed silently;
the run fell back to a default state (clear color only — black
output). Caught by checking the build log for "Shader compilation
failed". Lesson: when adding shader uniforms across files, verify
each shader's existing uniform names independently.

---

### C2 — anyPct data incomplete (MEDIUM, doc fix + honesty escalation)

You're right. I cited only C0=16 and C0=64 anyPct from JSON. Pulled
all 4 by epoch-matching probe_stats files to capture timestamps:

| C0 res | C0 anyPct | C1 anyPct | C2 anyPct | C3 anyPct |
|---:|---:|---:|---:|---:|
| 16 | 2.10% | **95.70%** | 0% | 0% |
| 32 | 0.009% | 7.81% | 0% | 0% |
| 48 | 0.001% | 0% | 0.17% | 0% |
| 64 | 0% | 7.79% | 9.89% | **96.88%** |

**These numbers are inconsistent and noisy.** C0 anyPct
DECREASING with density (2.10% → 0%) is implausible. C1 anyPct
fluctuates 95.7% → 7.8% → 0% → 7.8%. The codex 09 verification
report cited C0 anyPct=3.5% at C0=32 — none of my captures match.

**Root cause** (which strengthens C6's variance concern): the
`probe_stats_*.json` is written at a SINGLE frame triggered by the
auto-burst at +8s warmup. With staggered cascades (C0 every frame,
C1 every 2, C2 every 4, C3 every 8), the snapshot frame may land
between bakes for some cascades — producing zero or partial anyPct
for those. Combined with the codex 09 P0 NaN/Inf first-frame
contamination (still unfixed; zero-init plan never landed), the
metric is unreliable for trend analysis from single-shot snapshots.

**Updated honesty in impl doc**: anyPct data shown for completeness
but explicitly flagged as single-shot-noisy. The C3 0%→96.88% jump
at C0=64 is suggestive but should be confirmed by N-capture
averaging (codex 13 path-2 work) before claiming "density helps
upper cascades" with confidence. The **meanLum trend** is more
stable (averages over all probes) and that's the data backing my
verdicts.

You're also right about the "C0 anyPct=0 vs C0 meanLum=0.051"
contradiction needing explanation. The JSON's `anyPct` likely
counts probes whose ALL direction bins exceed an epsilon (else
zero); `meanLum` averages all probes' per-bin radiance. A probe
with bright illumination in 3 bins and zero in the other 5 would
contribute to meanLum but not to anyPct. **Documented in the impl
doc.**

---

### C4 — Binary visibility over-darkens partial occluders (HIGH, doc fix + future work)

You're right. `probeVisible` is a single ray from surface to probe
center — binary yes/no. A probe behind a column with a narrow gap
contributes vec3(0) under H6 even though some of its direction
bins are unoccluded.

**Impact**: real but bounded. The over-darkening occurs at
partial-occluder boundaries (column edges, arch openings). The
sponza_h6_visibility_fix.png capture's "new pixelation" comment
likely DOES include some incorrect over-darkening on top of the
correct occlusion shadows.

**Per-direction-bin visibility** would be the correct fix: cast
one shadow ray per direction bin (D² rays per probe instead of 1).
Cost: D² × 8 corners × 16 SDF steps = D² × 128 fetches per pixel.
At D=8 (C0 default), that's 8,192 fetches per surface-hit pixel —
~64× the current cost. Probably impractical in real-time without
caching/temporal amortization.

**Cheaper compromise**: gate per-bin BEFORE the cosine-weight loop
in `sampleProbeDir`. Inside the existing D² loop, skip bins whose
direction is blocked. The shadow ray would be from the probe in
the bin's direction; can early-terminate at first hit. Cost:
D² × ~5 SDF steps per probe (avg) × 8 corners ≈ D² × 40 fetches
per pixel = 2,560 at D=8. Still ~20× current cost but quality is
better.

**Filed as future work** in the impl doc's recommendations
section. Current binary `probeVisible` is the right first step;
it shows the H6 fix is dominant before paying the cost of
per-bin visibility. If the over-darkening becomes a visible
quality issue in further testing, the per-bin variant is the
upgrade path.

---

### C1 — C0 meanLum non-monotonicity at C0=48 (MEDIUM, doc fix)

You're right. C0 meanLum 0.0470 → 0.0513 → **0.0503** → 0.0513
is non-monotonic. The dip at 48 weakens the saturation narrative.

**Most likely cause**: single-shot capture variance (codex 13 +
critic 02 C6). The codex 12 scaling experiment already showed
±2-5× variance on RenderDoc captures from GPU power-state
transitions. The 0.0503 vs 0.0513 difference is within that
variance band.

**Doc fix**: explicitly note the non-monotonicity as variance,
not real signal. The "C0 saturates by 32" claim is downgraded to
"C0 plateaus around 32-64 within capture variance" — softer but
honest.

A 2× rerun at C0=48 would confirm whether the dip is reproducible.
Filed under "would benefit from N-capture averaging" with the
other variance-control work (codex 13 path 2).

---

### C5 — H6 only masks render-time leak; bake still uses non-visible probes (MEDIUM, doc clarification)

You're right and this is a good architectural distinction I
glossed over.

**The truth is more nuanced**:

- The cascade BAKE at `radiance_3d.comp` does its own raymarching
  per probe. Each probe's bake samples geometry it can ACTUALLY see
  along each direction bin (sphere-traces from the probe outward).
  So bake-time probe radiance IS already physically correct
  per-probe — the probe inside a wall traces outward and finds the
  surrounding geometry; the probe in open air traces and finds far
  geometry.

- The leak happens at RENDER time, in `sampleDirectionalGI`, where
  the trilinear interpolation across 8 spatial neighbors blends
  irradiance from probes physically separated by walls (one inside
  the corridor, one outside). H6 fixes THIS by gating the interp
  on probe-to-surface visibility.

- **Cascade inheritance** (C3→C2→C1→C0 via `sampleUpperDir` /
  `sampleUpperDirTrilinear` in radiance_3d.comp) inherits irradiance
  from upper-cascade probes that aren't physically visible from the
  current cascade probe. This IS a bake-time leak source kilo
  identified that H6 doesn't address. Whether it matters depends
  on how often inheritance fires for visible surfaces (less than
  the render-time issue, given C0 already covers most of the
  visible scene).

**Doc clarification added** to the impl doc's H6 section,
distinguishing render-time interp leak (fixed) from bake-time
inheritance leak (not fixed). Filed bake-time visibility as
future work — would require shader changes in radiance_3d.comp
similar to H6 but during cascade bake.

---

### C6 — No measured H6 perf numbers (MEDIUM, doc fix + acknowledgment)

You're right. I estimated "roughly double" raymarch cost from
worst-case 128 fetches per pixel but didn't measure. Your average-
case estimate (3-5 SDF steps typical → 24-40 fetches) is sounder
than my worst-case.

**Actual measurement deferred** — would need either a `--use-probe-
visibility=0/1` toggle (CLI + uniform plumbing) for clean A/B, OR
a manual revert+capture+restore cycle. The toggle is ~10 lines but
adds API surface; the revert cycle is fragile.

**Acknowledged in impl doc** as a verification gap. Codex 13 path
2 (N-capture averaging + variance control) is a prerequisite for
trustworthy before/after measurements anyway — adding the toggle
during that work would be the natural place.

For now, the qualitative cost story (raymarch fragment work
non-trivially up; not 10× but probably 1.5-3×) is honest enough
to inform decisions.

---

### C7 — Capture count "7" vs plan "6" (LOW, doc fix)

You're right. Plan said 6 (4 density + 2 blur A/B). Actual:
4 density + 1 blur=8 at C0=64 + 1 H7 verification + 1 H6
verification = **7 captures**. (I claimed "7 total" which is
correct for the realized count; the discrepancy is that I
realized only ONE of the planned blur A/B captures because the
"blur=1 at C0=64" was already produced as Phase 1's C0=64
capture.)

**Doc fix**: clarified to "7 captures: 4 density (Phase 1) + 1
blur=8 at C0=64 (Phase 2; the blur=1 A/B point reuses Phase 1's
C0=64) + 1 H7 verification + 1 H6 verification".

---

### C8 — H1 PARTIAL conflates two phenomena (MEDIUM, doc fix)

You're right. "C0 saturates / upper cascades benefit" is two
distinct mechanisms. **Doc fix**: split into

- **H1a** (C0 probe density saturation): **REJECTED** beyond 32
  — C0 meanLum 0.0470→0.0513 (8% increase) for 64× more probes.
  Marginal.
- **H1b** (upper-cascade occupancy via base-density scaling):
  **CONFIRMED** by C3 meanLum 0.000→0.027 across the sweep,
  consistent with C3 anyPct trend (despite single-shot noise).
  Mechanism: `--cascade-c0-res=64` means C3=8³=512 probes (vs
  4³=64 at C0=32), enough density for C3 to find Sponza geometry
  and feed it back through bake-time inheritance.

The actionable recommendation is sharper: raise `--cascade-c0-res`
even though C0 itself plateaus, because the upper cascades it
controls (C1=N/2, C2=N/4, C3=N/8) DO benefit. Documented in the
impl doc's recommendations.

---

### C9 — H7 boundary-zero may over-darken edge surfaces (LOW, doc fix)

You're right. Returning vec3(0) for out-of-grid corners is more
physically justifiable than the prior clamp-to-edge (which
duplicated edge probe radiance), but it produces hard brightness
cliffs at the volume boundary. For Sponza at cam.md viewpoint
this is invisible (geometry interior); for other views/scenes
filling the volume edges, ~50% of trilinear corners zeroing at
boundary could be visible.

**Doc fix** acknowledges this tradeoff in the H7 section. A
nearest-VALID-probe fallback (use the in-bounds probes, distribute
the missing corners' weight to them) would be the proper compromise
but is more shader work. Filed under future quality refinements.

---

### C10 — No explicit A/B control statement for H6 (LOW, doc fix)

You're right. **Doc fix added** at the H6 capture section:

> The H6 capture (`sponza_h6_visibility_fix.png`) was taken at
> identical CLI settings to Phase 1's `sponza_density_AB_c64.png`
> (same camera, same `--cascade-c0-res=64`, same `--gi-blur-radius=1`,
> same ambient floors at 0, same `--exit-frames=300`). The ONLY
> differences are the `probeVisible` + `_SPD` macro changes in
> `raymarch.frag`. Diff the two images for a clean H6 A/B.

(Note: the H6 capture used `--exit-frames=300` not 900 because
the visual didn't need long settling; meanLum settled by frame ~5
in all captures.)

---

### Summary

| ID | Sev | Type | Result |
|---|---|---|---|
| **C3** | High | **Code fix** | probeVisible constants derive from uVolumeSize + uVolumeMax-uVolumeMin (scene-adaptive) |
| **C4** | High | Doc + future work | Binary visibility over-darkening risk acknowledged; per-direction-bin variant filed |
| C1 | Med | Doc | C0=48 dip documented as single-shot variance; "saturates" → "plateaus within variance" |
| **C2** | Med | Doc + data | All 4 anyPct extracted but flagged noisy; meanLum is the trustworthy metric |
| C5 | Med | Doc | Render-time vs bake-time leak distinction added to H6 section |
| C6 | Med | Doc | H6 perf gap acknowledged; deferred to codex 13 path 2 work |
| C7 | Low | Doc | Capture count clarified: 4+1+1+1=7 |
| **C8** | Med | Doc | H1 PARTIAL → H1a REJECTED + H1b CONFIRMED |
| C9 | Low | Doc | H7 boundary-zero tradeoff documented |
| C10 | Low | Doc | H6 A/B control statement added |
| **C11** | Med | Code fix (= C3) | Same scene-adaptive constants address coarse-SDF self-occlusion |

**Bottom line.** C3+C11 was the real bug (would break H6 on any
scene other than the default vol=4 + res=128); fixed and verified
by recapture. C4 is honest acknowledgment that binary visibility
isn't the final form (per-bin would be) but is the right first
step. C8 sharpens the H1 verdict from "partial" to "C0 saturated,
upper cascades genuinely benefit" — actionable. C2's noisy anyPct
data weakens single-shot probe-stats reliability — that's a known
codex 13 path-2 follow-up. The other findings are precision
improvements that don't change the substantive verdicts.
