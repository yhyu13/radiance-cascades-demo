# Phase 5 Debug Tooling Implementation Learnings

**Date:** 2026-04-28  
**Branch:** 3d  
**Status:** Implemented, compiled (0 errors).  
**Context:** Debugging and visualization work done in parallel with Phase 5b/5c to validate the directional merge. Also documents bug fixes found during debug-mode testing.

---

## What Was Implemented

### 1. GL_TRIANGLE_STRIP vs GL_TRIANGLES draw-call mismatch (bug fix)

**Root cause:** The three debug shaders (`sdf_debug.vert`, `radiance_debug.vert`, `lighting_debug.vert`) generate screen-space geometry procedurally from `gl_VertexID`:
```glsl
vec2 positions[4] = vec2[4](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
```
This is a 4-vertex `GL_TRIANGLE_STRIP` (two triangles, no index 4 or 5). The draw calls incorrectly said `glDrawArrays(GL_TRIANGLES, 0, 6)` — `gl_VertexID` 4 and 5 index out of the array, producing degenerate geometry. Only the lower-left triangle rendered. Result: all three debug vis modes showed only the bottom-left half of the screen.

**Fix:** Changed `renderSDFDebug()` and `renderRadianceDebug()` to `glDrawArrays(GL_TRIANGLE_STRIP, 0, 4)`.

**Key distinction to preserve:** The main scene `raymarchPass()` uses a different pattern: `raymarch.vert` reads `layout(location=0) in vec2 aPos` from a VBO with 6 vertices (2 triangles). Its draw call remains `glDrawArrays(GL_TRIANGLES, 0, 6)`. Do not change this to TRIANGLE_STRIP — the VBO is not set up for it.

**Symptom:** When only the bottom-left half of a debug view is visible, the first thing to check is the draw call vs vertex shader convention. There are exactly two patterns in this codebase: procedural `gl_VertexID` + TRIANGLE_STRIP (debug shaders), and VBO attribute 0 + TRIANGLES (raymarch.frag).

### 2. Radiance debug: 6 visualization modes

Updated `radiance_debug.frag` from 3 modes (Slice/MaxProj/Avg) to 6:

| Mode | Name | What it shows |
|---|---|---|
| 0 | Slice | 2D slice of `probeGridTexture` along selected axis |
| 1 | MaxProj | Max-intensity projection through `probeGridTexture` |
| 2 | Avg | Direction-averaged projection through `probeGridTexture` |
| 3 | Atlas | Raw `probeAtlasTexture` tile layout — each D×D block = one probe |
| 4 | HitType | Surf/sky/miss fractions per probe from atlas alpha |
| 5 | Bin | Single direction bin (uAtlasBin.x, uAtlasBin.y) across all probes in slice |

Modes 0–2 read `uRadianceTexture` (probeGridTexture). Modes 3–5 read `uAtlasTexture` (probeAtlasTexture).

**Mode 3 (Atlas raw):** Uses `sliceTexCoord(vTexCoords)` to sample `uAtlasTexture`. Since the atlas is `(res*D) × (res*D) × res`, sampling with the same normalized UV as the probe grid shows the full D×D tiled layout. Most useful at D=4: each 4×4 pixel block on screen = one probe's directional bin layout.

**Mode 4 (HitType fix):** Old mode 4 read `probeGridTexture` alpha as packed hit count — broken by Phase 5b-1 zeroing that alpha. New mode 4 iterates all D² bins per probe via `texelFetch(uAtlasTexture, ...)` and classifies: `a > 0` → surface, `a < 0` → sky, `a == 0` → miss. Outputs `vec4(missF, surfF, skyF, 1.0)` (R=miss, G=surf, B=sky).

**Mode 5 (Bin viewer):** Uses `probeFromUV()` helper to convert 2D screen UV + slice axis/position → integer probe coordinate. Then fetches `texelFetch(uAtlasTexture, ivec3(probeCoord.x*D+bin.x, probeCoord.y*D+bin.y, probeCoord.z), 0)`. Each pixel on screen = one probe; the value is that probe's stored radiance for the selected directional bin. Validation: near the red wall, bins pointing toward the red wall should show red; opposite bins should show the color of the far wall.

**`probeFromUV()` helper:**
```glsl
ivec3 probeFromUV(vec2 uv) {
    vec3 uvw = sliceTexCoord(uv);
    return clamp(ivec3(uvw * vec3(uVolumeSize)), ivec3(0), uVolumeSize - ivec3(1));
}
```
Converts the 2D screen UV + slice axis/position → 3D normalized UVW → integer probe index. Used by both mode 4 and mode 5. The `sliceTexCoord` helper already existed for mode 0/1/2 and was reused here.

### 3. New uniforms in `radiance_debug.frag`

```glsl
uniform sampler3D uAtlasTexture;   // per-direction atlas (modes 3, 4, 5)
uniform int       uAtlasDirRes;    // D — atlas tile side length
uniform ivec2     uAtlasBin;       // (dx, dy) selected bin for mode 5
```

Bound in `renderRadianceDebug()`:
```cpp
glActiveTexture(GL_TEXTURE1);
glBindTexture(GL_TEXTURE_3D, cascades[selC].probeAtlasTexture);
glUniform1i(..., "uAtlasTexture", 1);
glUniform1i(..., "uAtlasDirRes", dirRes);
glUniform2i(..., "uAtlasBin", atlasBinDx, atlasBinDy);
```

### 4. Directional merge toggle in UI

Added "Directional merge (Phase 5c)" checkbox in the Cascade panel. When toggled, `cascadeReady = false` triggers a full rebake of all cascades so the A/B comparison is done at equivalent cascade state (not mixed bakes).

Tracking pattern:
```cpp
static bool lastDirectionalMerge = true;
if (useDirectionalMerge != lastDirectionalMerge) {
    lastDirectionalMerge = useDirectionalMerge;
    cascadeReady = false;
}
```

### 5. Phase 5 block in main GUI menu

Added Phase 5 status after the Phase 4 block. Shows live feature status: 5a/5b/5b-1/5c as active, 5e as pending, with the directional merge ON/OFF state shown inline.

### 6. Probe fill rate readback fix

**Problem:** Phase 5b-1 writes `probeGridTexture` with `alpha=0.0`. The old readback decoded `buf[i*4+3]` (the alpha) as a packed hit count → always 0 → fill bars showed 0%.

**Fix — dual-read pattern:**
```cpp
// 1. Read probeGridTexture for RGB luminance statistics only
glBindTexture(GL_TEXTURE_3D, cascades[ci].probeGridTexture);
glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, buf.data());
// → compute nonZero, maxLum, sumLum from buf[i*4+0..2], ignore alpha

// 2. Read probeAtlasTexture for surf/sky classification
if (cascades[ci].probeAtlasTexture != 0) {
    glBindTexture(GL_TEXTURE_3D, cascades[ci].probeAtlasTexture);
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, atlasBuf.data());
    // Walk D² bins per probe, check alpha: >0 surface, <0 sky
    for (int pz...) for (int py...) for (int px...) {
        bool hasSurf=false, hasSky=false;
        for (int dy...) for (int dx...) {
            int ax = px*D+dx, ay = py*D+dy;
            float a = atlasBuf[((pz*atlasWH + ay)*atlasWH + ax)*4 + 3];
            if (a > 0.0f) hasSurf = true;
            if (a < 0.0f) hasSky  = true;
        }
        if (hasSurf) ++surfHit;
        if (hasSky)  ++skyHit;
    }
}
```

Atlas buffer size: `atlasWH * atlasWH * res * 4` floats, where `atlasWH = res * D`.

---

## Key Learnings

### Debug draw-call pattern must match vertex shader convention

The two patterns in this codebase are mutually incompatible and produce no compile or link error when mixed:
- **Procedural:** `gl_VertexID` into `positions[4]`, requires `GL_TRIANGLE_STRIP, 0, 4`
- **VBO attribute:** `layout(location=0) in vec2 aPos`, requires `GL_TRIANGLES, 0, 6`

The symptom of a mismatch (only bottom-left half of screen) is distinctive and repeatable. Future debug shaders should be written following the existing procedural pattern.

### Changing one draw call can silently break another

When the `GL_TRIANGLES→GL_TRIANGLE_STRIP` fix was applied to debug passes, the raymarch pass was also changed — breaking the normal scene. The lesson: draw call fixes must be targeted. Read which vertex shader is active before changing the primitive mode. The fix for one shader type actively breaks the other.

### Atlas readback index arithmetic

The atlas texture layout is `atlasWH × atlasWH × res` where `atlasWH = res * D`. For probe `(px, py, pz)` and bin `(dx, dy)`, the atlas texel is at absolute coordinates `(px*D+dx, py*D+dy, pz)`. The flat buffer index is:
```
((pz * atlasWH + (py*D+dy)) * atlasWH + (px*D+dx)) * 4 + channel
```
This is just a standard 3D row-major index with `atlasWH` as the stride, not `res`. Getting the stride wrong (using `res` instead of `atlasWH`) produces silently wrong results — values are read from wrong probes.

### Mode 4 HitType vs Mode 5 Bin are both useful but for different things

Mode 4 shows hit type fraction per probe (a single color per probe-pixel). It's useful for diagnosing whether probes near geometry are classified correctly. Mode 5 shows color per probe for a specific direction bin — it's useful for validating that the directional atlas contains the expected light from that direction. Both were needed to validate Phase 5c:
- Mode 4 confirms the atlas alpha convention is correct (not all-miss)  
- Mode 5 confirms that near the red wall, bins pointing toward it show red

### Probe readback is expensive — run at reduced frequency

`glGetTexImage` is a GPU sync point. The atlas at D=4, res=32 is 128×128×32×4 floats = 8 MB per cascade. With 4 cascades that's 32 MB of GPU→CPU transfer per stats update. The existing `statsFrameInterval` throttle (one readback every N frames) is critical — do not remove it or the frame rate will tank.

---

## Files Changed

| File | Change |
|---|---|
| `res/shaders/radiance_debug.frag` | Added `uAtlasTexture`, `uAtlasDirRes`, `uAtlasBin` uniforms; added `probeFromUV()` helper; added `sliceTexCoord()` helper; replaced dead mode 3 with Atlas raw; fixed mode 4 to read atlas alpha; added mode 5 Bin viewer; updated mode docstring |
| `src/demo3d.cpp` | `renderSDFDebug()` + `renderRadianceDebug()`: `GL_TRIANGLES,0,6 → GL_TRIANGLE_STRIP,0,4`; `renderRadianceDebug()`: atlas bind on unit 1, push `uAtlasDirRes`/`uAtlasBin`; probe readback: dual-read pattern; Phase 5 main menu block; Cascade panel: directional merge checkbox |
| `src/demo3d.h` | `int atlasBinDx`, `int atlasBinDy`; `bool useDirectionalMerge` |
