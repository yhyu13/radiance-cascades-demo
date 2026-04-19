#version 450 core

/**
 * @file lighting_debug.frag
 * @brief Fragment shader for per-light debug visualization (Phase 1)
 * 
 * Visualizes individual light contributions separately:
 * - Light 0, 1, 2: Show each point light independently
 * - Combined: All lights summed
 * - Normals: Surface normals as RGB
 * - Albedo: Material colors
 * 
 * Helps validate multi-light setup and color bleeding
 */

in vec2 vTexCoords;
out vec4 fragColor;

// =============================================================================
// Uniforms
// =============================================================================

uniform sampler3D uRadianceTexture;
uniform sampler3D uSDFTexture;

uniform ivec3 uVolumeSize;
uniform int uSliceAxis;
uniform float uSlicePosition;

uniform int uDebugMode;  // 0=Light0, 1=Light1, 2=Light2, 3=Combined, 4=Normals, 5=Albedo

uniform float uExposure;
uniform float uIntensityScale;

// Light positions for reference visualization
uniform vec3 uLightPositions[3];

// =============================================================================
// Helper Functions
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

vec3 computeNormal(ivec3 coord) {
    float dx = texelFetch(uSDFTexture, coord + ivec3(1,0,0), 0).r -
               texelFetch(uSDFTexture, coord - ivec3(1,0,0), 0).r;
    float dy = texelFetch(uSDFTexture, coord + ivec3(0,1,0), 0).r -
               texelFetch(uSDFTexture, coord - ivec3(0,1,0), 0).r;
    float dz = texelFetch(uSDFTexture, coord + ivec3(0,0,1), 0).r -
               texelFetch(uSDFTexture, coord - ivec3(0,0,1), 0).r;
    
    vec3 grad = vec3(dx, dy, dz);
    float len = length(grad);
    
    if (len < 1e-6)
        return vec3(0.0, 1.0, 0.0);
    
    return normalize(grad) * 0.5 + vec3(0.5); // Convert to 0-1 range for display
}

vec3 sampleAlbedo(vec3 worldPos) {
    // Cornell Box colors
    if (worldPos.x < -1.9 && worldPos.x > -2.1)
        return vec3(0.65, 0.05, 0.05);
    if (worldPos.x > 1.9 && worldPos.x < 2.1)
        return vec3(0.12, 0.45, 0.15);
    return vec3(0.75, 0.75, 0.75);
}

vec3 probeToWorld(ivec3 probePos) {
    vec3 gridOrigin = vec3(-2.0, 0.0, -2.0);
    vec3 gridSize = vec3(4.0, 2.0, 4.0);
    return gridOrigin + (vec3(probePos) + 0.5f) * (gridSize / vec3(uVolumeSize));
}

// =============================================================================
// Main
// =============================================================================

void main() {
    vec3 texCoord;
    
    if (uSliceAxis == 0) {
        texCoord = vec3(uSlicePosition, vTexCoords.x, vTexCoords.y);
    } else if (uSliceAxis == 1) {
        texCoord = vec3(vTexCoords.x, uSlicePosition, vTexCoords.y);
    } else {
        texCoord = vec3(vTexCoords.x, vTexCoords.y, uSlicePosition);
    }
    
    ivec3 voxelCoord = ivec3(texCoord * vec3(uVolumeSize));
    
    // Bounds check
    if (any(greaterThanEqual(voxelCoord, uVolumeSize)) || any(lessThan(voxelCoord, ivec3(0)))) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    
    vec4 radiance = texelFetch(uRadianceTexture, voxelCoord, 0);
    vec3 color;
    
    if (uDebugMode == 0 || uDebugMode == 1 || uDebugMode == 2) {
        // Individual light visualization
        // Phase 1: We don't have separate buffers per light, so show combined
        // with indicator of which light we're "simulating"
        color = radiance.rgb;
        
        // Add light position marker
        vec3 worldPos = probeToWorld(voxelCoord);
        float distToLight = distance(worldPos, uLightPositions[uDebugMode]);
        
        if (distToLight < 0.2) {
            // Highlight light position
            color = vec3(1.0, 1.0, 0.0); // Yellow marker
        }
        
    } else if (uDebugMode == 3) {
        // Combined lighting
        color = radiance.rgb;
        
    } else if (uDebugMode == 4) {
        // Surface normals
        color = computeNormal(voxelCoord);
        
    } else if (uDebugMode == 5) {
        // Material albedo
        vec3 worldPos = probeToWorld(voxelCoord);
        color = sampleAlbedo(worldPos);
    }
    
    // Apply tone mapping
    color = reinhardToneMap(color * uIntensityScale, uExposure);
    color = linearToSRGB(color);
    
    fragColor = vec4(color, 1.0);
}
