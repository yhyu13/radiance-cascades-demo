#version 450 core

in vec2 vTexCoords;
out vec4 fragColor;

// =============================================================================
// Uniforms
// =============================================================================

/** Isotropic probe grid (probeGridTexture) — modes 0, 1, 2 */
uniform sampler3D uRadianceTexture;

/** Per-direction atlas (probeAtlasTexture) — modes 3, 4, 5 */
uniform sampler3D uAtlasTexture;

/** D: atlas tile side length (D^2 bins per probe) */
uniform int uAtlasDirRes;

/** Selected direction bin for mode 5 Bin viewer */
uniform ivec2 uAtlasBin;

/** Volume dimensions (probe grid, e.g. ivec3(32)) */
uniform ivec3 uVolumeSize;

/** Slice parameters */
uniform int   uSliceAxis;       // 0=X, 1=Y, 2=Z
uniform float uSlicePosition;   // 0.0 to 1.0

/** Visualization mode:
 *  0 = Slice (isotropic grid)
 *  1 = MaxProj (isotropic grid)
 *  2 = Average (isotropic grid)
 *  3 = Atlas raw — D×D tile per probe, shows full directional layout
 *  4 = HitType heatmap — surf/sky/miss fractions from atlas alpha
 *  5 = Bin viewer — single direction bin (uAtlasBin) across all probes (nearest-bin)
 *  6 = Bilinear bin viewer — same direction, bilinear blend at bin midpoint (compare with 5)
 */
uniform int uVisualizeMode;

/** Rays per probe (D^2) — kept for API compat but no longer used for normalization */
uniform int uRaysPerProbe;

/** Display options */
uniform float uExposure;
uniform bool  uShowGrid;
uniform float uIntensityScale;

// =============================================================================
// Helpers
// =============================================================================

vec3 linearToSRGB(vec3 linear) {
    return mix(
        pow(linear, vec3(1.0 / 2.2)) * 1.055 - vec3(0.055),
        linear * 12.92,
        lessThanEqual(linear, vec3(0.0031308))
    );
}

vec3 reinhardToneMap(vec3 hdr, float exposure) {
    vec3 exposed = hdr * exposure;
    return exposed / (exposed + vec3(1.0));
}

/** Build a 3D texture coord from 2D UV + slice axis/position. */
vec3 sliceTexCoord(vec2 uv) {
    if (uSliceAxis == 0) return vec3(uSlicePosition, uv.x, uv.y);
    if (uSliceAxis == 1) return vec3(uv.x, uSlicePosition, uv.y);
    return vec3(uv.x, uv.y, uSlicePosition);
}

vec4 sampleSlice(vec2 uv) {
    return texture(uRadianceTexture, sliceTexCoord(uv));
}

vec4 maxProjection(vec2 uv) {
    vec4 maxValue = vec4(0.0);
    int samples = uVolumeSize[uSliceAxis];
    for (int i = 0; i < samples; ++i) {
        float t = (float(i) + 0.5) / float(samples);
        vec3 tc;
        if      (uSliceAxis == 0) tc = vec3(t, uv.x, uv.y);
        else if (uSliceAxis == 1) tc = vec3(uv.x, t, uv.y);
        else                      tc = vec3(uv.x, uv.y, t);
        maxValue = max(maxValue, texture(uRadianceTexture, tc));
    }
    return maxValue;
}

vec4 averageProjection(vec2 uv) {
    vec4 sum = vec4(0.0);
    int samples = min(uVolumeSize[uSliceAxis], 64);
    for (int i = 0; i < samples; ++i) {
        float t = (float(i) + 0.5) / float(samples);
        vec3 tc;
        if      (uSliceAxis == 0) tc = vec3(t, uv.x, uv.y);
        else if (uSliceAxis == 1) tc = vec3(uv.x, t, uv.y);
        else                      tc = vec3(uv.x, uv.y, t);
        sum += texture(uRadianceTexture, tc);
    }
    return sum / float(samples);
}

/** Convert UV + slice position → integer probe coordinate in [0, uVolumeSize). */
ivec3 probeFromUV(vec2 uv) {
    vec3 uvw = sliceTexCoord(uv);
    return clamp(ivec3(uvw * vec3(uVolumeSize)), ivec3(0), uVolumeSize - ivec3(1));
}

vec3 drawGrid(vec2 uv, vec3 color) {
    if (!uShowGrid) return color;
    float gridSize = 0.1;
    vec2 grid = fract(uv / gridSize);
    float lineThickness = 0.02;
    vec2 gridLine = smoothstep(vec2(0.0), vec2(lineThickness), grid) *
                    smoothstep(vec2(1.0), vec2(1.0 - lineThickness), grid);
    float gridFactor = 1.0 - (gridLine.x * gridLine.y);
    return mix(color * 0.3, color, gridFactor);
}

// =============================================================================
// Main
// =============================================================================

void main() {
    vec4 radiance;

    if (uVisualizeMode == 0) {
        // Isotropic probe grid — 2D slice
        radiance = sampleSlice(vTexCoords);

    } else if (uVisualizeMode == 1) {
        // Isotropic probe grid — max intensity projection
        radiance = maxProjection(vTexCoords);

    } else if (uVisualizeMode == 2) {
        // Isotropic probe grid — average projection
        radiance = averageProjection(vTexCoords);

    } else if (uVisualizeMode == 3) {
        // Atlas raw — shows the raw D×D tile layout of the per-direction atlas.
        // Each (1/32 × 1/32) block of the display is one probe; within each block,
        // the D×D sub-pixels are that probe's directional bins.
        // With D=4 the atlas is 128×128×32; GL_NEAREST keeps tile boundaries sharp.
        radiance = texture(uAtlasTexture, sliceTexCoord(vTexCoords));

    } else if (uVisualizeMode == 4) {
        // HitType heatmap — reads atlas alpha to classify each bin, averages over D^2.
        // Fixed for Phase 5b-1: probeGridTexture alpha is now 0.0 (written by reduction).
        // R=miss  G=surface  B=sky
        ivec3 probeCoord = probeFromUV(vTexCoords);
        int surfCount = 0, skyCount = 0;
        int D = uAtlasDirRes;
        for (int dy2 = 0; dy2 < D; ++dy2)
            for (int dx2 = 0; dx2 < D; ++dx2) {
                float a = texelFetch(uAtlasTexture,
                    ivec3(probeCoord.x * D + dx2,
                          probeCoord.y * D + dy2,
                          probeCoord.z), 0).a;
                if      (a > 0.0) ++surfCount;
                else if (a < 0.0) ++skyCount;
            }
        float N     = float(D * D);
        float surfF = float(surfCount) / N;
        float skyF  = float(skyCount)  / N;
        float missF = max(0.0, 1.0 - surfF - skyF);
        fragColor = vec4(missF, surfF, skyF, 1.0);
        return;

    } else if (uVisualizeMode == 5) {
        // Bin viewer — nearest-bin texelFetch for the selected direction bin.
        // Each pixel = one probe. Near red wall, leftward bins → red; rightward → green.
        ivec3 probeCoord = probeFromUV(vTexCoords);
        ivec3 atlasTxl = ivec3(probeCoord.x * uAtlasDirRes + uAtlasBin.x,
                               probeCoord.y * uAtlasDirRes + uAtlasBin.y,
                               probeCoord.z);
        radiance = texelFetch(uAtlasTexture, atlasTxl, 0);

    } else {
        // Mode 6: Bilinear bin viewer — same selected bin, but queries at the midpoint
        // between the selected bin center and its (+1,+1) neighbor (f=0.5,0.5 blend point).
        // Toggle mode 5 ↔ 6 to see directional bilinear smoothing at bin boundaries.
        ivec3 probeCoord = probeFromUV(vTexCoords);
        int D = uAtlasDirRes;
        // octScaled at bin-center midpoint: selected bin center is at uAtlasBin, add 0.5
        // to reach the edge between the selected bin and its (+x,+y) neighbor.
        vec2 octScaled = vec2(uAtlasBin) + 0.5;
        ivec2 b00 = clamp(ivec2(floor(octScaled)), ivec2(0), ivec2(D-1));
        ivec2 b11 = clamp(b00 + ivec2(1),          ivec2(0), ivec2(D-1));
        ivec2 b10 = ivec2(b11.x, b00.y);
        ivec2 b01 = ivec2(b00.x, b11.y);
        vec2  f   = fract(octScaled);
        int bx = probeCoord.x * D, by = probeCoord.y * D, bz = probeCoord.z;
        vec3 s00 = texelFetch(uAtlasTexture, ivec3(bx+b00.x, by+b00.y, bz), 0).rgb;
        vec3 s10 = texelFetch(uAtlasTexture, ivec3(bx+b10.x, by+b10.y, bz), 0).rgb;
        vec3 s01 = texelFetch(uAtlasTexture, ivec3(bx+b01.x, by+b01.y, bz), 0).rgb;
        vec3 s11 = texelFetch(uAtlasTexture, ivec3(bx+b11.x, by+b11.y, bz), 0).rgb;
        radiance = vec4(mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y), 1.0);
    }

    // Shared tone-map + sRGB path (modes 0, 1, 2, 3, 5)
    vec3 color = radiance.rgb * uIntensityScale;
    color = reinhardToneMap(color, uExposure);
    color = linearToSRGB(color);
    color = drawGrid(vTexCoords, color);

    if (length(radiance.rgb) < 0.01)
        color = vec3(0.05);

    fragColor = vec4(color, 1.0);
}
