/**
 * @file raymarch.frag
 * @brief Fragment shader for volume raymarching visualization
 * 
 * This shader renders the final image by raymarching through the 3D radiance
 * volume. It's executed as a fullscreen fragment shader, with one ray cast
 * per pixel.
 * 
 * Algorithm:
 * 1. Generate primary ray from camera through pixel
 * 2. March through volume in steps:
 *    a. Sample SDF for distance to surface
 *    b. Step by safe distance (SDF-guided)
 *    c. Sample radiance from cascade hierarchy
 *    d. Accumulate color with front-to-back blending
 *    e. Early termination when opaque
 * 3. Apply tone mapping and gamma correction
 * 4. Write final color
 * 
 * Quality Settings:
 * - Fixed step count or adaptive (SDF-based)
 * - Early ray termination threshold
 * - Temporal reprojection for stability
 */

#version 330 core

// =============================================================================
// Input/Output
// =============================================================================

/** Vertex position (fullscreen quad) */
in vec2 vUV;

/** Final color output */
out vec4 fragColor;

// =============================================================================
// Uniforms
// =============================================================================

/** View matrix */
uniform mat4 uViewMatrix;

/** Projection matrix */
uniform mat4 uProjMatrix;

/** Inverse view-projection matrix */
uniform mat4 uInvVPMatrix;

/** Camera position */
uniform vec3 uCameraPos;

/** Volume dimensions */
uniform ivec3 uVolumeSize;

/** Volume world-space bounds */
uniform vec3 uVolumeMin;
uniform vec3 uVolumeMax;

/** Raymarching parameters */
uniform int uSteps;
uniform float uTerminationThreshold;
uniform float uTime;

/** Rendering mode */
uniform int uRenderMode; // 0 = final, 1 = sdf viz, 2 = normals, etc.

/** Direct light position in world space */
uniform vec3 uLightPos;

/** Direct light color */
uniform vec3 uLightColor;

// =============================================================================
// Texture Bindings
// =============================================================================

/** Signed distance field */
uniform sampler3D uSDF;

/** Radiance volume (from cascades) */
uniform sampler3D uRadiance;

/** Albedo/material color volume */
uniform sampler3D uAlbedo;

/** Whether to blend in cascade indirect lighting */
uniform bool uUseCascade;

// =============================================================================
// Constants
// =============================================================================

const float PI = 3.14159265359;
const float EPSILON = 1e-6;
const float INF = 1e10;

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Calculate ray direction from UV and camera matrices
 */
vec3 calculateRayDirection(vec2 uv) {
    // Convert UV to clip space [-1, 1]
    vec2 ndc = uv * 2.0 - 1.0;
    
    // Create ray in clip space
    vec4 clipPos = vec4(ndc, -1.0, 1.0);
    
    // Transform to world space
    vec4 worldPos = uInvVPMatrix * clipPos;
    worldPos /= worldPos.w;
    
    vec3 rayDir = normalize(worldPos.xyz - uCameraPos);
    return rayDir;
}

/**
 * @brief Calculate intersection of ray with axis-aligned box
 * 
 * @param rayOrigin Ray origin
 * @param rayDir Ray direction
 * @param boxMin Box minimum corner
 * @param boxMax Box maximum corner
 * @param tNear Output: entry distance
 * @param tFar Output: exit distance
 * @return true if ray intersects box
 */
bool intersectBox(
    vec3 rayOrigin, vec3 rayDir,
    vec3 boxMin, vec3 boxMax,
    out float tNear, out float tFar
) {
    vec3 invDir = 1.0 / rayDir;
    
    vec3 t0 = (boxMin - rayOrigin) * invDir;
    vec3 t1 = (boxMax - rayOrigin) * invDir;
    
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    
    tNear = max(max(tmin.x, tmin.y), tmin.z);
    tFar = min(min(tmax.x, tmax.y), tmax.z);
    
    return tFar > tNear && tFar > 0.0;
}

/**
 * @brief Sample SDF at world position
 */
float sampleSDF(vec3 worldPos) {
    // Convert world position to texture coordinates
    vec3 texCoord = (worldPos - uVolumeMin) / (uVolumeMax - uVolumeMin);
    
    if (any(lessThan(texCoord, vec3(0.0))) || any(greaterThan(texCoord, vec3(1.0))))
        return INF;
    
    return texture(uSDF, texCoord).r;
}

/**
 * @brief Sample radiance at world position
 */
vec3 sampleRadiance(vec3 worldPos) {
    vec3 texCoord = (worldPos - uVolumeMin) / (uVolumeMax - uVolumeMin);
    
    if (any(lessThan(texCoord, vec3(0.0))) || any(greaterThan(texCoord, vec3(1.0))))
        return vec3(0.0);
    
    return texture(uRadiance, texCoord).rgb;
}

/**
 * @brief Estimate surface normal from SDF gradient
 */
vec3 estimateNormal(vec3 worldPos) {
    const float eps = 0.06;  // ~1 voxel at 64^3 in 4-unit volume (4/64=0.0625)
    
    vec3 dx = vec3(sampleSDF(worldPos + vec3(eps, 0, 0)) - sampleSDF(worldPos - vec3(eps, 0, 0)), 0, 0);
    vec3 dy = vec3(0, sampleSDF(worldPos + vec3(0, eps, 0)) - sampleSDF(worldPos - vec3(0, eps, 0)), 0);
    vec3 dz = vec3(0, 0, sampleSDF(worldPos + vec3(0, 0, eps)) - sampleSDF(worldPos - vec3(0, 0, eps)));
    
    return normalize(vec3(dx.x, dy.y, dz.z));
}

/**
 * @brief Tone mapping (ACES approximation)
 */
vec3 toneMapACES(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

// =============================================================================
// Main Fragment Shader
// =============================================================================

void main() {
    // Generate ray from camera
    vec3 rayDir = calculateRayDirection(vUV);
    
    // Find entry and exit points with volume bounding box
    float tNear, tFar;
    if (!intersectBox(uCameraPos, rayDir, uVolumeMin, uVolumeMax, tNear, tFar)) {
        // Ray misses volume - clear to sky color
        fragColor = vec4(0.1, 0.1, 0.15, 1.0);
        return;
    }
    
    // Start raymarching
    float t = max(tNear, 0.0);
    vec3 accumulatedColor = vec3(0.0);
    float accumulatedAlpha = 0.0;
    
    // March through volume
    int stepCount = 0;
    for (int i = 0; i < uSteps; ++i) {
        ++stepCount;
        if (accumulatedAlpha >= uTerminationThreshold)
            break; // Early termination

        vec3 pos = uCameraPos + rayDir * t;
        
        // Sample SDF for adaptive stepping
        float dist = sampleSDF(pos);
        
        if (dist < EPSILON) {
            // Hit surface!
            vec3 normal = estimateNormal(pos);

            // Debug mode 1: normals as RGB
            if (uRenderMode == 1) {
                fragColor = vec4(normal * 0.5 + 0.5, 1.0);
                return;
            }

            // Debug mode 2: depth map (distance ray travelled to reach surface)
            if (uRenderMode == 2) {
                float depth = (t - tNear) / max(tFar - tNear, 0.001);
                fragColor = vec4(vec3(1.0 - depth), 1.0); // near=white, far=dark
                return;
            }

            // Debug mode 3: indirect radiance * 5 (magnified for visibility)
            if (uRenderMode == 3) {
                vec3 uvw3 = (pos - uVolumeMin) / (uVolumeMax - uVolumeMin);
                vec3 indirect = texture(uRadiance, uvw3).rgb;
                fragColor = vec4(toneMapACES(indirect * 5.0), 1.0);
                return;
            }

            // Sample material albedo (shared by modes 0, 4)
            vec3 uvw    = (pos - uVolumeMin) / (uVolumeMax - uVolumeMin);
            vec3 albedo = texture(uAlbedo, uvw).rgb;

            // Debug mode 4: direct light only (bypass cascade regardless of uUseCascade)
            if (uRenderMode == 4) {
                vec3 lightDir4 = normalize(uLightPos - pos);
                float diff4    = max(dot(normal, lightDir4), 0.0);
                vec3 direct    = albedo * (diff4 * uLightColor + vec3(0.05));
                fragColor = vec4(toneMapACES(direct), 1.0);
                fragColor.rgb = pow(fragColor.rgb, vec3(1.0 / 2.2));
                return;
            }

            // Mode 0: final rendering
            vec3 lightDir     = normalize(uLightPos - pos);
            float diff        = max(dot(normal, lightDir), 0.0);
            vec3 surfaceColor = albedo * (diff * uLightColor + vec3(0.05));

            // Indirect lighting from cascade (probes already store albedo-weighted radiance)
            if (uUseCascade) {
                vec3 indirect = texture(uRadiance, uvw).rgb;
                surfaceColor += indirect * 1.0;
            }

            // Front-to-back blending
            float alpha = 1.0;
            accumulatedColor += surfaceColor * alpha * (1.0 - accumulatedAlpha);
            accumulatedAlpha += alpha * (1.0 - accumulatedAlpha);

            break;
        }
        
        // Advance by SDF distance (with safety factor)
        float stepSize = max(dist * 0.7, 0.01);
        t += stepSize;
        
        if (t > tFar)
            break;
    }
    
    // Debug mode 5: step count heatmap (green=few, yellow=moderate, red=many/miss)
    // Normalize against 32 not uSteps — Cornell Box rays typically hit in <32 steps,
    // dividing by uSteps(256) would compress everything into pure green.
    if (uRenderMode == 5) {
        float t5 = clamp(float(stepCount) / 32.0, 0.0, 1.0);
        // green -> yellow -> red ramp
        vec3 heatColor = (t5 < 0.5)
            ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t5 * 2.0)
            : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t5 - 0.5) * 2.0);
        fragColor = vec4(heatColor, 1.0);
        return;
    }

    // Apply tone mapping
    accumulatedColor = toneMapACES(accumulatedColor);
    
    // Gamma correction
    accumulatedColor = pow(accumulatedColor, vec3(1.0 / 2.2));
    
    // Output final color
    fragColor = vec4(accumulatedColor, 1.0);
}
