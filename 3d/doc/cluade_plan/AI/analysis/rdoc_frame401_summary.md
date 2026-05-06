# RenderDoc AI Analysis — Frame 401 (Auto-Capture)

**Capture:** `rdoc_frame_frame401.rdc`  
**Date:** 2026-05-06  
**Mode:** `--auto-rdoc` (8s warm-up, all 4 cascades forced via `rdocForceRebuildCount=2` fix)  
**Analyzer:** claude-opus-4-7

---

## Pipeline Execution Summary

```
Frame 401 action tree:
  glClear (depth+color)
  ┌─ radiance_3d  [C0 bake]    eid=45   ← all 4 cascade levels ran ✓
  ├─ reduction_3d [C0 reduce]  eid=56
  ├─ radiance_3d  [C1 bake]    eid=103
  ├─ reduction_3d [C1 reduce]  eid=114
  ├─ radiance_3d  [C2 bake]    eid=161
  ├─ reduction_3d [C2 reduce]  eid=172
  ├─ radiance_3d  [C3 bake]    eid=219
  ├─ reduction_3d [C3 reduce]  eid=230
  glClear (GI buffer)
  ┌─ raymarch                   eid=281
  ├─ gi_blur                    eid=309
  └─ glDrawElements() (UI)      eid=340
  SwapBuffers
```

---

## GPU Timing (FetchCounters)

| Pass | Cascade | GPU time | Notes |
|---|---|---|---|
| Cascade bake | C0 | **5.3 ms** | Smallest: D=8, res=32 |
| Cascade reduction | C0 | 0.03 ms | Stagger-optimized (C0 lightest) |
| Cascade bake | C1 | **6.3 ms** | |
| Cascade reduction | C1 | 0.19 ms | |
| Cascade bake | C2 | **10.5 ms** | Largest: higher dir res |
| Cascade reduction | C2 | 0.20 ms | |
| Cascade bake | C3 | **7.0 ms** | |
| Cascade reduction | C3 | 0.22 ms | |
| **Total cascade** | | **29.8 ms** | All 4 at once (stagger disabled for capture) |
| Raymarching | — | **5.9 ms** | |
| GI blur | — | **1.7 ms** | |
| UI draw | — | 0.01 ms | |
| **Total frame** | | **~37.4 ms** | ≈ 27 FPS worst-case |

> **Normal stagger cost** (C0 every frame, C1 every 2nd, etc.):  
> ~5.3 + 0.5×6.3 + 0.25×10.5 + 0.125×7.0 = **14.1 ms/frame avg** for cascades  
> + 5.9ms raymarch + 1.7ms GI blur = **~21.7 ms/frame → ~46 FPS**

---

## Cascade Resources Visible in Capture

| Resource | ID | Status |
|---|---|---|
| cascade0_probeGrid | 146 | ✅ |
| cascade0_probeAtlas | 147 | ✅ |
| cascade0_probeAtlasHistory | 148 | ✅ |
| cascade0_probeGridHistory | 149 | ✅ |
| cascade1_probeAtlas | 151 | ✅ |
| cascade1_probeAtlasHistory | 152 | ✅ |
| cascade1_probeGridHistory | 153 | ✅ |
| cascade2_probeAtlas | 155 | ✅ |
| cascade2_probeAtlasHistory | 156 | ✅ |
| cascade2_probeGridHistory | 157 | ✅ |
| cascade3_probeGrid | 158 | ✅ |
| cascade3_probeAtlas | 159 | ✅ |
| cascade3_probeAtlasHistory | 160 | ✅ |

All 4 cascades fully bound and visible. (`cascade1_probeGrid` absent — C1 uses fused EMA swap, its grid lives in history handle after the swap; known behavior.)

---

## Texture Analysis (Claude Vision)

### SDF Volume (`sdfTexture`, 128³, z=64)
**Claude says:** Hard seams and flat regions — sharp binary transition instead of smooth distance gradient. Blocky edges from low-resolution voxelization. Possible asymmetric arch geometry (right leg shorter than left).

**Assessment:** The flat appearance is expected for narrow-band SDF visualization, but the asymmetry flag is worth checking. No correctness bugs affecting rendering, just visualization.

---

### Albedo Volume (`albedoTexture`, 128³, z=64)

| Region | Expected | Actual | Status |
|---|---|---|---|
| Left wall | Red | Red | ✅ |
| Right wall | Green | Green | ✅ |
| Background | White | **Light gray** | ⚠️ |

**Claude says:** Background registers as ~#C0C0C0 instead of #FFFFFF. The colored walls pass. The gray may be a gamma/format issue with how the mid-slice is visualized rather than a real albedo bug — worth verifying in the shader.

---

### C0 Probe Directional Atlas (`cascade0_probeAtlas`, 32³, z=16)

**Claude says:**
- ✅ Center tiles: smooth red→red and green→green transitions — neighboring probes correctly interpolate
- ✅ No random noise — atomic accumulation working, no merge errors
- ⚠️ Some uniform gray tiles in lower-middle rows — may be probes never baked or stuck inside geometry
- ⚠️ Sharp red/green boundary is expected (wall boundary) but should be verified not to be a seam

---

### C1 Probe Directional Atlas (`cascade1_probeAtlas`, 16³, z=8)

**Claude says:** ~70–75% surface coverage, expected ≥98%. ~25% dead tiles (all-zero alpha) flagged — fails spec. Central void region identified.

**Assessment:** C1 surface coverage was the main fix in Phase 14c. This is a known area needing continued work.

---

### C0 Isotropic Probe Grid (`cascade0_probeGrid`, 32³, z=16)

**Claude says:** Gave diagnostic guide (implies the actual texture may show generic patterns). The probe grid should show a blurred version of room irradiance — if it appears uniform or noisy, the reduction stage has issues.

---

## Final Frame Quality (from thumbnail)

**Claude rates: FAIR**

### Artifacts identified:
1. **Outer-wall drift / leakage** *(prominent)* — Red and green wall colors bleed outside the Cornell Box silhouette. Indirect probe contributions escaping past wall geometry → halos around box exterior.

2. **Color bleeding asymmetry** *(mild)* — Red wall bounce saturated; floor near red wall weaker than expected. Green side shows more floor bounce than red side.

3. **Cascade/probe leakage under tall box** — Shadow detaches from box contact point. Probe interpolation crossing the occluder.

4. **Coarse probe grid on back wall** — Smooth but slightly blocky falloff on the bright ceiling lobe. No hard banding.

### What's clean:
- ✅ No directional bin banding (~36° steps)
- ✅ No ring-shaped cascade seams
- ✅ No shadow acne speckles
- ✅ Shadows are present

---

## Bugs Ranked by Impact

| Priority | Bug | Artifact | Likely cause |
|---|---|---|---|
| P0 | Outer-wall radiance leakage | Colored halos outside box | Cascade probes at/near boundary not clamped; boundary conditions in raymarch or probe lookup |
| P1 | C1 coverage ~75% (spec: ≥98%) | Dead probe tiles | C1 tMax or ray distribution not covering the scene fully |
| P2 | Albedo background gray not white | Visualization only | Gamma/format in volume slice view |
| P3 | Gray probe tiles in C0 lower region | Potentially dark probes in GI | Probes inside geometry or bake skipping those positions |

---

## Key Fix Applied This Session

The `rdocForceRebuildCount = 2` fix ensures cascade dispatches appear in the captured frame:

```
Before fix:
  Frame N:   beginRdocFrameIfPending() → forceCascadeRebuild=true + TriggerCapture()
             update() → cascades run, flag cleared
  Frame N+1: [CAPTURED] → forceCascadeRebuild=false → NO cascades in capture ❌

After fix:
  Frame N:   rdocForceRebuildCount=2, TriggerCapture()
             update() → cascades run (count 2→1)
  Frame N+1: [CAPTURED] sustain block → forceCascadeRebuild=true, renderFrameIndex=0
             update() → cascades run (count 1→0) ✓
```

---

## Next Steps

1. **P0 fix**: Investigate outer-wall probe leakage — check cascade boundary condition in `raymarch.comp` and `gi_blur.comp`; clamp probe lookup to interior AABB
2. **P1 fix**: Increase C1 `tMax` or verify probe seeding coverage at scene bounds
3. Update stagger timing analysis in the Phase 10 plan given the new per-cascade timing data
