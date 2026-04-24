#version 450 core

/**
 * @file radiance_debug.frag
 * @brief Fragment shader for radiance cascade debug visualization (Phase 1)
 * 
 * Visualizes 3D radiance cascade textures with multiple display modes:
 * - Slice view: Cross-section through the volume
 * - Max projection: Maximum intensity projection
 * - Average: Average radiance along axis
 * - Direct lighting: Shows only direct illumination component
 * 
 * Phase 1 Focus: Validate multi-light injection and color bleeding
 */

in vec2 vTexCoords;
out vec4 fragColor;

// =============================================================================
// Uniforms
// =============================================================================

/** Radiance cascade texture to visualize */
uniform sampler3D uRadianceTexture;

/** Volume dimensions */
uniform ivec3 uVolumeSize;

/** Slice parameters */
uniform int uSliceAxis;          // 0=X, 1=Y, 2=Z
uniform float uSlicePosition;    // 0.0 to 1.0

/** Visualization mode */
uniform int uVisualizeMode;      // 0=Slice, 1=MaxProj, 2=Average, 3=DirectLighting, 4=HitType

/** Rays per probe for this cascade (used by mode 4 to normalize hit fractions) */
uniform int uRaysPerProbe;

/** Display options */
uniform float uExposure;         // HDR exposure adjustment
uniform bool uShowGrid;          // Show voxel grid overlay
uniform float uIntensityScale;   // Manual intensity scaling

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Convert linear RGB to sRGB for proper display
 */
vec3 linearToSRGB(vec3 linear) {
    return mix(
        pow(linear, vec3(1.0 / 2.2)) * 1.055 - vec3(0.055),
        linear * 12.92,
        lessThanEqual(linear, vec3(0.0031308))
    );
}

/**
 * @brief Tone mapping (Reinhard operator)
 */
vec3 reinhardToneMap(vec3 hdr, float exposure) {
    vec3 exposed = hdr * exposure;
    return exposed / (exposed + vec3(1.0));
}

/**
 * @brief Sample radiance at specific slice position
 */
vec4 sampleSlice(vec2 uv) {
    vec3 texCoord;
    
    if (uSliceAxis == 0) {
        // X-axis slice (YZ plane)
        texCoord = vec3(uSlicePosition, uv.x, uv.y);
    } else if (uSliceAxis == 1) {
        // Y-axis slice (XZ plane)
        texCoord = vec3(uv.x, uSlicePosition, uv.y);
    } else {
        // Z-axis slice (XY plane)
        texCoord = vec3(uv.x, uv.y, uSlicePosition);
    }
    
    return texture(uRadianceTexture, texCoord);
}

/**
 * @brief Maximum intensity projection along axis
 */
vec4 maxProjection(vec2 uv) {
    vec4 maxValue = vec4(0.0);
    int samples = uVolumeSize[uSliceAxis];
    
    for (int i = 0; i < samples; ++i) {
        float t = (float(i) + 0.5) / float(samples);
        vec3 texCoord;
        
        if (uSliceAxis == 0) {
            texCoord = vec3(t, uv.x, uv.y);
        } else if (uSliceAxis == 1) {
            texCoord = vec3(uv.x, t, uv.y);
        } else {
            texCoord = vec3(uv.x, uv.y, t);
        }
        
        vec4 radianceSample = texture(uRadianceTexture, texCoord);
        maxValue = max(maxValue, radianceSample);
    }
    
    return maxValue;
}

/**
 * @brief Average projection along axis
 */
vec4 averageProjection(vec2 uv) {
    vec4 sum = vec4(0.0);
    int samples = min(uVolumeSize[uSliceAxis], 64); // Limit for performance
    
    for (int i = 0; i < samples; ++i) {
        float t = (float(i) + 0.5) / float(samples);
        vec3 texCoord;
        
        if (uSliceAxis == 0) {
            texCoord = vec3(t, uv.x, uv.y);
        } else if (uSliceAxis == 1) {
            texCoord = vec3(uv.x, t, uv.y);
        } else {
            texCoord = vec3(uv.x, uv.y, t);
        }
        
        sum += texture(uRadianceTexture, texCoord);
    }
    
    return sum / float(samples);
}

/**
 * @brief Draw grid overlay
 */
vec3 drawGrid(vec2 uv, vec3 color) {
    if (!uShowGrid)
        return color;
    
    float gridSize = 0.1; // Grid cell size in UV space
    vec2 grid = fract(uv / gridSize);
    
    // Draw grid lines
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
    
    // Sample based on visualization mode
    if (uVisualizeMode == 0) {
        // Slice view
        radiance = sampleSlice(vTexCoords);
    } else if (uVisualizeMode == 1) {
        // Maximum intensity projection
        radiance = maxProjection(vTexCoords);
    } else if (uVisualizeMode == 2) {
        // Average projection
        radiance = averageProjection(vTexCoords);
    } else if (uVisualizeMode == 3) {
        // Direct lighting only (use RGB, ignore alpha)
        radiance = sampleSlice(vTexCoords);
    } else {
        // Mode 4: Hit-type heatmap — decode packed alpha into surf/sky/miss fractions
        // Probe alpha encodes: packed = surfHits + skyHits * 255.0
        // R = surf fraction (green), G = sky fraction (blue), B = miss fraction (dark red)
        radiance = sampleSlice(vTexCoords);
        float packed = radiance.a;
        float N = float(max(uRaysPerProbe, 1));
        float skyF  = floor(packed / 255.0 + 0.5) / N;
        float surfF = mod(packed + 0.5, 255.0) / N;
        float missF = max(0.0, 1.0 - surfF - skyF);
        // Color: surface=green, sky=blue, miss=red
        fragColor = vec4(missF, surfF, skyF, 1.0);
        return;
    }

    // Apply intensity scaling
    vec3 color = radiance.rgb * uIntensityScale;

    // Tone mapping
    color = reinhardToneMap(color, uExposure);

    // Convert to sRGB for display
    color = linearToSRGB(color);

    // Draw grid overlay
    color = drawGrid(vTexCoords, color);

    // Background for empty regions
    float alpha = length(radiance.rgb);
    if (alpha < 0.01) {
        color = vec3(0.05); // Dark background
    }

    fragColor = vec4(color, 1.0);
}
