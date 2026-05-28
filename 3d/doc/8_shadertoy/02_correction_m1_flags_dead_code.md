# Correction: M1 Delta Flags Are Dead Code (No UI/CLI Access)

**Date:** 2026-05-28  
**Correction to:** `01_audit_v3_status_and_gaps.md` Finding F1  
**Severity:** Lowered from HIGH to MEDIUM (flags are harmless dead code, not untested active toggles)

---

## 1. Revised Finding: M1 Flags Exist But Cannot Be Toggled

The initial audit stated the M1 delta flags were "implemented as flag-gated toggles" that had "never been A/B tested." Closer inspection reveals:

**The M1 delta flags are COMPLETELY INACCESSIBLE from the running application.**

| Flag | Defined in | Default | GUI checkbox? | CLI argument? | Runtime toggle? |
|------|-----------|---------|---------------|---------------|-----------------|
| `m1Delta3GatedTrilinear` | `demo3d.h:1429` | `false` | **NO** | **NO** | **NO** |
| `m1Delta6GeometricCone` | `demo3d.h:1430` | `false` | **NO** | **NO** | **NO** |

They have setters (`setM1Delta3GatedTrilinear`, `setM1Delta6GeometricCone`) that trigger cascade rebuilds, but nothing calls those setters from ImGui or CLI parsing. The shader uniforms are wired (lines 2475-2476), but since the C++ values never change from `false`, the uniforms are always `0`.

**This is dead code** — stubs wired end-to-end but with no activation path. To test them, you must:
1. Edit `demo3d.h:1429-1430` to change default values to `true`
2. Recompile
3. Run the application

This is actually **better** than the initial audit suggested — the flags can't accidentally leak into baselines or user workflows. They're implementations waiting for the M1 gated A/B process to give them a UI/CLI activation path.

---

## 2. What the Code Actually Does (When Active)

When `m1Delta3GatedTrilinear = true` (requires source edit):
```glsl
// radiance_3d.comp:673-675
upperDir = ws;  // full WeightedSample result (per-corner renormalized .rgb)
aFactor  = 1.0; // no scalar attenuation
```

When `m1Delta6GeometricCone = true` (requires source edit):
```cpp
// demo3d.cpp:2480-2484
sinT = std::sin(0.75f * 0.5f * kPi);  // = 0.924 for ALL cascades
```

Current default (both flags OFF):
```glsl
upperDir = vec4(upperDirTrilinear.rgb, ws.a);  // Phase 3 v3: trilinear .rgb + WS fraction
aFactor  = upperDir.a;  // scalar (uniform) attenuation
```

---

## 3. Sponza Valid Mask Analysis (confirmed)

The `analyze_baselines.py:55` mask is:
```python
mask = (pt_lum > 0.05) & (casc_lum > 0.001)
```

**This threshold is too high.** At N=2048, the PT indirect mean is 0.0589 — barely above the 0.05 threshold. Only the brightest 693 pixels (0.08% of the image) survive. The valid count drops from 6665→693 as N increases, confirming the mask is noise-inflated at low N.

**The metric computed on this mask is not representative of whole-scene quality.** The `ratio_self = 4.71` means those 693 specific pixels (the ones where PT registers non-trivial indirect) have cascade 4.7× too bright. The other 99.92% of pixels are not measured.

**Recommended fix:**
```python
mask = (pt_lum > 0.001) & (casc_lum > 0.001)  # lower threshold = wider coverage
```
Or use an adaptive threshold that targets a fixed valid pixel percentage (e.g., top 10%).

---

## 4. Cascade#1 Cone Hardcode (confirmed)

The ShaderToy WeightedSample cone:
```glsl
// CubeA.glsl:22
float theta = (lProbeSize*0.5 - 0.5)/(lProbeSize*0.5)*3.141592653*0.5;
```

Per-cascade values for sin(theta):
| Cascade transition | lProbeSize | sin(theta) |
|--------------------|------------|------------|
| C0 → C1 | 4 | sin(3π/8) ≈ 0.924 |
| C1 → C2 | 8 | sin(7π/16) ≈ 0.981 |
| C2 → C3 | 16 | sin(15π/32) ≈ 0.995 |

Current hardcode: `sin(3π/8) ≈ 0.924` for ALL cascades.

**Impact:** C1→C2 cone is ~5.8% too narrow; C2→C3 is ~7.1% too narrow. This makes the visibility test MORE rejective (too NARROW cone rejects more corners, which is conservative but inaccurate). This is a second-order problem compared to the fundamental 4.7× Sponza gap.

---

## 5. Tools/Scripts Verification Summary

| Tool | Status | Issues |
|------|--------|--------|
| `build.ps1` | ✓ PASS | Clean CMake configure + build |
| `CMakeLists.txt` | ✓ PASS | C++23, GLEW, GLM, raylib, all correct |
| `sponza_capture.ps1` | ✓ PASS | Correctly avoids DM/ST/WS forcing; -DryRun works |
| `build_baseline_lock.ps1` | ✓ PASS | SHA256 hashing, conservative "missing" marking |
| `analyze_baselines.py` | ⚠ CONCERN | Valid mask threshold (0.05) too high for Sponza |
| `baseline_lock.json` | ✓ PASS | All captures hashed, metrics attached, verdict recorded |

---

## 6. Recommended Immediate Actions (revised)

1. **DO NOT edit demo3d.h to enable M1 flags until M1 per-delta impl docs exist.** The flags compile but the M1 gated process (A/B vs baselines, STRONG/MARGINAL/DEAD verdict) hasn't run.

2. **Fix the analyzer threshold** — lower from 0.05 to 0.001 (or computed from PT noise floor) and re-run Sponza metrics to get a larger valid mask.

3. **If re-analyzed Sponza still shows ratio > 2.0** on a wider mask, stop Path A and escalate to user. The M1 deltas cannot close a 2×+ gap.

4. **Before M1 A/B**, add CLI arguments + ImGui checkboxes for the M1 flags so they can be toggled without source edits. This is ~5 lines of code:
   ```cpp
   // CLI parsing
   else if (arg == "--m1-delta-3-gated-trilinear") setM1Delta3GatedTrilinear(true);
   else if (arg == "--m1-delta-6-geometric-cone") setM1Delta6GeometricCone(true);
   
   // ImGui
   if (ImGui::Checkbox("M1 Delta #3: Gated Trilinear", &m1Delta3GatedTrilinear))
       /* setter called */;
   if (ImGui::Checkbox("M1 Delta #6: Geometric Cone", &m1Delta6GeometricCone))
       /* setter called */;
   ```

5. **Write the missing M1 per-delta impl docs** before any A/B code executes. Per the v3 scope doc §5 rule #8: "No 'land the whole port in one commit.' Each delta gets its own A/B + impl doc + gate."

---

## 7. Cross-References

- Main audit: `doc/8_shadertoy/01_audit_v3_status_and_gaps.md`
- v3 scope: `doc/7/v3_shadertoy_adoption_scope.md`
- v20 diff: `doc/7/v20_shadertoy_diff_impl.md`
- Baselines: `tools/v3_baseline/baseline_lock.json`
- Analyzer: `tools/v3_baseline/analyze_baselines.py`