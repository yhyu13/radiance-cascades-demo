# RenderDoc GPU Analysis

**Capture:** `D:\GitRepo-My\radiance-cascades-demo\3d\doc\cluade_plan\AI\captures/rdoc_frame_capture.rdc`  
**Analyzed:** 2026-05-03T16:26:24  
**Model:** claude-opus-4-7  

---

## Note
*`renderdoc` Python module not available (requires qrenderdoc Python env). Texture/timing analysis skipped. Final frame analyzed from thumbnail.*

## Final Frame (from capture thumbnail)

# Artifact Analysis

## Observed Artifacts

1. **Outer-wall drift / radiance leak** — The exterior walls of the Cornell Box (visible behind/around the inner room) show colored bleeding: faint red on the left exterior, green on the right, and gray top/bottom bars. Light/radiance is escaping through the box walls, indicating the cascade probes outside the box are sampling interior radiance through thin geometry.

2. **Color bleeding deficiency** — The white floor and ceiling near the colored walls show very weak red/green bleed compared to what's expected in a Cornell Box. The indirect color transfer from the red and green walls onto neutral surfaces is underrepresented.

3. **Soft / missing contact shadows** — The base of both white blocks lacks firm contact shadows against the floor. The shadows fade out too softly, suggesting under-resolved near-field occlusion (probably cascade-0 resolution limit).

4. **Mild probe-grid softness** — The lighting on the back wall and ceiling has a smooth but slightly blotchy falloff, hinting at low-resolution probe interpolation, though no hard banding is visible.

No visible: directional bin banding, cascade ring seams, or shadow acne.

## Quality Rating: **Fair**

The interior render is largely plausible and noise-free, but the **outer-wall radiance leak** is a significant structural artifact, and the weak color bleeding plus soft contact shadows reduce physical accuracy.
