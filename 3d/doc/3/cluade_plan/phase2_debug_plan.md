# Phase 2 Debug Visualization Plan

**Date:** 2026-04-22  
**Agreed context:** codex_critic/06_updated_recommendation.md — visual smoke test before Phase 3 or Phase 2.5 additions  
**Goal:** Answer four specific questions with on-screen or console evidence before declaring Phase 2 done

---

## The Four Questions to Answer

| Q | Question | Tool |
|---|----------|------|
| Q1 | Is the SDF correct — do walls produce non-zero distance gradients? | SDF debug (existing) + normal debug mode |
| Q2 | Does the probe dispatch actually write non-zero radiance to the texture? | CPU readback (one-time print) |
| Q3 | Does the cascade GI reach the final fragment shader? | Indirect-only render mode |
| Q4 | Does toggling "Cascade GI" change the image visibly? | A/B in same frame |

Answer all four before touching material color, shadow rays, or Phase 3.

---

## Tool 1 — CPU Readback of Probe Texture (no shader change)

**What it tests:** Q2 — did `imageStore` in `radiance_3d.comp` actually write anything?

**Why first:** If the probe texture is all-zero, everything else is irrelevant. This is the single cheapest gate.

**Implementation — add to `demo3d.cpp` after the cascade dispatch in `render()`:**

```cpp
// ONE-TIME diagnostic: read probe texture and print stats
static bool probeDumped = false;
if (!probeDumped && cascadeReady) {
    probeDumped = true;
    int res = cascades[0].resolution;  // 32
    int totalPixels = res * res * res;
    std::vector<float> buf(totalPixels * 4);  // RGBA as float32

    glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, buf.data());
    glBindTexture(GL_TEXTURE_3D, 0);

    float maxLum = 0.0f, sumLum = 0.0f;
    int nonZero = 0;
    for (int i = 0; i < totalPixels; ++i) {
        float r = buf[i*4+0], g = buf[i*4+1], b = buf[i*4+2];
        float lum = (r + g + b) / 3.0f;
        if (lum > 1e-4f) ++nonZero;
        sumLum += lum;
        maxLum = std::max(maxLum, lum);
    }
    float meanLum = sumLum / totalPixels;

    std::cout << "[Probe Readback] " << nonZero << "/" << totalPixels
              << " non-zero probes, maxLum=" << maxLum
              << ", meanLum=" << meanLum << std::endl;

    // Print center probe and a wall-adjacent probe
    auto idx = [res](int x, int y, int z){ return (z*res*res + y*res + x)*4; };
    int cx = res/2, cy = res/2, cz = res/2;
    std::cout << "[Probe center  (" << cx << "," << cy << "," << cz << ")] "
              << "R=" << buf[idx(cx,cy,cz)] << " G=" << buf[idx(cx,cy,cz)+1]
              << " B=" << buf[idx(cx,cy,cz)+2] << std::endl;
    // Near back wall (z=1 → world z≈-1.8)
    std::cout << "[Probe backwall (16,16,1)] "
              << "R=" << buf[idx(16,16,1)] << " G=" << buf[idx(16,16,1)+1]
              << " B=" << buf[idx(16,16,1)+2] << std::endl;
}
```

**What to read:**
- `nonZero > 0` → dispatch wrote something → proceed to Q3  
- `nonZero == 0` → dispatch wrote nothing → diagnose binding (see failure table below)  
- `maxLum > 0.3` → probes near the light are correctly bright  
- `center probe ≈ 0.1–0.4` → center of room sees some wall radiance  
- `backwall probe ≈ 0.5–0.8` → probe adjacent to back wall has high direct lighting stored  

**Failure diagnosis table:**

| Symptom | Most likely cause | Fix |
|---------|-------------------|-----|
| All zero | `imageStore` binding wrong (image unit ≠ 0) | Add `glUniform1i(prog, "oRadiance", 0)` explicitly |
| All zero | SDF has no surfaces (cornell box SDF wrong) | Run SDF debug view, check walls visible |
| All zero | `uVolumeSize` wrong → bounds check kills all invocations | Print `c.resolution` before dispatch |
| Near-zero only | `intervalEnd` too small (probes can't reach walls) | Already fixed to `length(uGridSize)≈6.93` |
| Uniform value everywhere | `sampleSDF` always returns INF (UV wrong) | Check `uGridOrigin` and `uGridSize` uniforms |

---

## Tool 2 — Normal Debug Mode (extend `uRenderMode` in `raymarch.frag`)

**What it tests:** Q1 — are Phase 1 normals pointing the right direction for each surface?

**Why this:** Without correct normals, both direct shading and the shadow ray (Phase 2.5) are wrong. This is cheap to add and reveals normal issues immediately as color.

**Shader change — add inside the hit block in `raymarch.frag`:**

```glsl
if (dist < EPSILON) {
    vec3 normal = estimateNormal(pos);

    // Debug modes
    if (uRenderMode == 1) {
        // Normals as RGB: map [-1,1] to [0,1]
        fragColor = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }
    if (uRenderMode == 2) {
        // SDF distance at hit (should be near 0, shows gradient magnitude)
        float d = abs(sampleSDF(pos));
        fragColor = vec4(vec3(d * 20.0), 1.0);
        return;
    }

    // Direct lighting
    vec3 lightDir = normalize(uLightPos - pos);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 surfaceColor = diff * uLightColor + vec3(0.05);

    if (uRenderMode == 3) {
        // Indirect only (magnified × 5 for visibility)
        if (uUseCascade) {
            vec3 uvw = (pos - uVolumeMin) / (uVolumeMax - uVolumeMin);
            vec3 indirect = texture(uRadiance, uvw).rgb;
            fragColor = vec4(toneMapACES(indirect * 5.0), 1.0);
        } else {
            fragColor = vec4(0.05, 0.05, 0.05, 1.0);  // dark gray = no cascade bound
        }
        return;
    }

    // Normal rendering (mode 0)
    if (uUseCascade) {
        vec3 uvw = (pos - uVolumeMin) / (uVolumeMax - uVolumeMin);
        vec3 indirect = texture(uRadiance, uvw).rgb;
        surfaceColor += indirect * 0.3;
    }
    // ... rest of shading unchanged
```

**C++ change — add `int raymarchRenderMode` to `Demo3D` private, send to shader:**

```cpp
glUniform1i(glGetUniformLocation(prog, "uRenderMode"), raymarchRenderMode);
```

**ImGui change — add render mode selector to Settings panel:**

```cpp
ImGui::Separator();
ImGui::Text("Debug Render Mode:");
ImGui::RadioButton("Final (0)",    &raymarchRenderMode, 0); ImGui::SameLine();
ImGui::RadioButton("Normals (1)",  &raymarchRenderMode, 1); ImGui::SameLine();
ImGui::RadioButton("SDF dist (2)", &raymarchRenderMode, 2); ImGui::SameLine();
ImGui::RadioButton("Indirect×5 (3)", &raymarchRenderMode, 3);
```

**What to see:**

| Mode | Expected image | Problem if not seen |
|------|----------------|---------------------|
| 1 — Normals | Back wall: blue-green (normal points +Z). Floor: green (normal points +Y). Left wall: red-tinted (normal points +X). Ceiling: dark green. | Wrong: all normals the same color → eps still too small or SDF flat |
| 2 — SDF dist | Near-black at every hit (distance ≈ 0 at surface). Very bright step in open air. | Wrong: surfaces show distance > 0.1 → raymarcher stopping too early |
| 3 — Indirect×5 | Low-frequency colored blobs: brighter near ceiling/walls, darker near floor center. If cascade is empty → flat dark gray everywhere. | All dark gray with `uUseCascade=true` → probe texture is zero or UV mapping wrong |

---

## Tool 3 — Probe Slice View (new small shader, reuse SDF debug pattern)

**What it tests:** Q2 more visually — spatial distribution of probe radiance across the volume.

**When to implement:** Only if CPU readback (Tool 1) confirms non-zero values but Tool 2 indirect-only mode still looks wrong. The slice view shows WHERE in the volume the probes are bright.

**New file `res/shaders/probe_debug.frag`:**

```glsl
#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler3D uProbeGrid;
uniform int   uSliceAxis;       // 0=X, 1=Y, 2=Z
uniform float uSlicePosition;   // 0..1
uniform float uBrightnessScale; // 1.0 default, raise if dim

void main() {
    vec3 uvw;
    if (uSliceAxis == 0)      uvw = vec3(uSlicePosition, vUV.x, vUV.y);
    else if (uSliceAxis == 1) uvw = vec3(vUV.x, uSlicePosition, vUV.y);
    else                      uvw = vec3(vUV.x, vUV.y, uSlicePosition);

    vec3 radiance = texture(uProbeGrid, uvw).rgb * uBrightnessScale;
    fragColor = vec4(radiance, 1.0);
}
```

**C++ addition — `renderProbeDebug()` method (reuse SDF debug pattern):**

```cpp
void Demo3D::renderProbeDebug() {
    if (!showProbeDebug || !cascades[0].active) return;
    auto it = shaders.find("probe_debug.frag");
    if (it == shaders.end()) return;

    GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
    int sz = 300;
    glViewport(viewport[2] - sz - 10, viewport[3] - sz - 10, sz, sz);

    glUseProgram(it->second);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, cascades[0].probeGridTexture);
    glUniform1i(glGetUniformLocation(it->second, "uProbeGrid"), 0);
    glUniform1i(glGetUniformLocation(it->second, "uSliceAxis"),     probeSliceAxis);
    glUniform1f(glGetUniformLocation(it->second, "uSlicePosition"),  probeSlicePosition);
    glUniform1f(glGetUniformLocation(it->second, "uBrightnessScale"), probeBrightnessScale);

    glBindVertexArray(debugQuadVAO);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);

    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}
```

**What to see in the probe slice (Y=0.5, horizontal mid-slice):**

```
Expected pattern (rough):
  Left column: slightly warm (near red wall, but no albedo yet → just brightness)
  Right column: slightly warm (near green wall)
  Center: dimmer (further from walls)
  Top row: bright (near ceiling, direct light overhead)
  Bottom row: dimmer (further from light)
```

If the slice is all black → probe data is zero → Tool 1 failure.  
If the slice is uniform gray → SDF sampling returns constant → UV mapping wrong.  
If the slice is noisy bright-dark → ray directions non-uniform → check Fibonacci sphere.

---

## Tool 4 — A/B Comparison (no new code, use existing toggle)

**What it tests:** Q4 — does the toggle actually change the final image?

**Procedure:**
1. Set `raymarchRenderMode = 0` (final output)
2. Uncheck "Cascade GI" → **Screenshot A** (direct only)
3. Check "Cascade GI" → **Screenshot B** (direct + indirect)
4. Diff the images or compare by eye

**Minimum success:** Any visible difference anywhere in the image.  
**Good success:** Surfaces facing walls are slightly brighter with cascade ON.  
**Better success:** A hint of the warm ceiling light reflected in the floor.

**What each failure tells you:**

| Failure | Cause | Fix |
|---------|-------|-----|
| Images identical | `uUseCascade` not being read by GLSL (bool uniform issue) | Change `uniform bool` to `uniform int`, check `glGetUniformLocation` ≠ -1 |
| Images identical | Probe texture binds to wrong unit | Print `glGetUniformLocation(prog, "uRadiance")`, confirm it gets 1 |
| Tiny diff, invisible by eye | Blend factor 0.3 too low | Raise to 1.0 temporarily for confirmation |
| B darker than A | Indirect term is subtracting (negative values in probe) | Print probe values from Tool 1 |

---

## Execution Order

```
Step 1: Add CPU readback (Tool 1) — 20 lines of C++, no shader change
        → Run, read console: is maxLum > 0?
        → YES → continue to Step 2
        → NO  → fix dispatch (binding or uniform), re-run

Step 2: Add render modes 1,2,3 to raymarch.frag + ImGui buttons (Tool 2)
        → Mode 1 (normals): each wall should have a distinct color
        → Mode 2 (SDF dist): should be near-black at hit surfaces
        → Mode 3 (indirect×5): should show non-uniform glow if cascade non-zero

Step 3: A/B toggle (Tool 4) — no code change, use existing toggle
        → Rebuild with mode 3 changes, then switch back to mode 0
        → Compare with/without cascade GI checkbox

Step 4: Probe slice view (Tool 3) — only if Steps 1-3 pass but something looks wrong spatially
```

---

## Expected Healthy Outcome (all tools pass)

```
Console:
  [Probe Readback] 12800/32768 non-zero probes, maxLum=0.82, meanLum=0.09
  [Probe center (16,16,16)] R=0.04 G=0.04 B=0.04
  [Probe backwall (16,16,1)] R=0.61 G=0.58 B=0.52

Screen mode 1 (normals):
  Back wall = blue-green, Floor = green, Left wall = red-orange, Right = cyan

Screen mode 3 (indirect×5):
  Low-frequency warm blobs near ceiling, darker at floor corners

Screen mode 0 + toggle:
  Cascade ON visibly brighter overall, especially floor and lower walls
```

If this outcome is achieved, Phase 2 is **visually confirmed**.  
Then and only then: proceed to Phase 2.5 (material albedo + shadow ray).

---

## Files to Touch

| File | Change |
|------|--------|
| `src/demo3d.cpp` | Add CPU readback after cascade dispatch; add `renderProbeDebug()`; add `raymarchRenderMode`, `showProbeDebug`, `probeSliceAxis`, `probeSlicePosition`, `probeBrightnessScale` members; add ImGui render mode radio buttons |
| `src/demo3d.h` | Add the 5 new member variables above |
| `res/shaders/raymarch.frag` | Add mode 1 (normals), mode 2 (SDF dist), mode 3 (indirect×5) branches inside hit block |
| `res/shaders/probe_debug.frag` | New file — probe slice viewer |

**Estimated effort:** ~2 hours for Steps 1–3. Step 4 (probe slice) adds ~1 hour if needed.

---

## Definition of Done for This Debug Plan

Phase 2 is visually confirmed when:
1. CPU readback shows `nonZero > 1000` and `maxLum > 0.3`
2. Normal debug shows distinct colors per wall face
3. Indirect-only debug shows non-uniform spatial distribution (not all-black, not all-same)
4. A/B toggle shows a visible image change at `mode 0`

Only after all four: start Phase 2.5.
