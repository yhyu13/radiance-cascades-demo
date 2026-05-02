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

#version 430 core

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
uniform int uUseCascade;

/** Phase 5h: 1=cast shadow ray from surface to light, 0=unshadowed direct (Phase 1-4) */
uniform int uUseShadowRay;

/** Phase 5i: 1=SDF cone soft shadow in direct term, 0=binary shadowRay() (Phase 5h) */
uniform int   uUseSoftShadow;
/** Phase 5i: penumbra width k — lower=wider penumbra. Shared with bake shader. */
uniform float uSoftShadowK;

/** Phase 5g: C0 directional atlas (per-direction D×D tile) */
uniform sampler3D uDirectionalAtlas;

/** Phase 5g: probe grid dimensions of C0 (same as uVolumeSize of C0) */
uniform ivec3 uAtlasVolumeSize;

/** Phase 5g: C0 grid origin in world space */
uniform vec3 uAtlasGridOrigin;

/** Phase 5g: C0 grid extent in world space */
uniform vec3 uAtlasGridSize;

/** Phase 5g: directional resolution D for the C0 atlas */
uniform int uAtlasDirRes;

/** Phase 5g: 1=cosine-weighted directional atlas sampling, 0=isotropic average (default) */
uniform int uUseDirectionalGI;

// =============================================================================
// Analytic SDF — primitive SSBO (binding 0, same layout as sdf_analytic.comp)
// Only accessed when uUseAnalyticSDF == 1.
// =============================================================================

struct Primitive {
    int   type;
    float pad0, pad1, pad2;
    vec4  position;
    vec4  scale;
    vec4  color;
};
layout(std430, binding = 0) readonly buffer PrimitiveBuffer {
    Primitive primitives[];
};
uniform int uPrimitiveCount;
uniform int uUseAnalyticSDF;  // 1 = evaluate SDF analytically per-sample (no grid)

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

// Analytic SDF primitives — mirrors sdf_analytic.comp exactly
float sdfBox(vec3 p, vec3 b) {
    vec3 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}
float sdfSphere(vec3 p, float r) { return length(p) - r; }

float sampleSDFAnalytic(vec3 worldPos) {
    float minDist = INF;
    for (int i = 0; i < uPrimitiveCount; ++i) {
        vec3 localPos = worldPos - primitives[i].position.xyz;
        float d = (primitives[i].type == 0)
            ? sdfBox(localPos, primitives[i].scale.xyz)
            : sdfSphere(localPos, primitives[i].scale.x);
        minDist = min(minDist, d);
    }
    return minDist;
}

/**
 * @brief Sample SDF at world position.
 * uUseAnalyticSDF=1: evaluate primitives directly (continuous, no grid).
 * uUseAnalyticSDF=0: read from precomputed 3D texture (trilinear, grid-quantized).
 */
float sampleSDF(vec3 worldPos) {
    if (uUseAnalyticSDF != 0)
        return sampleSDFAnalytic(worldPos);

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

// =============================================================================
// Phase 5g: Directional Atlas Sampling
// =============================================================================

// Octahedral decode: unit square [0,1]^2 -> unit sphere direction
vec3 octToDir(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec3 d = vec3(uv, 1.0 - abs(uv.x) - abs(uv.y));
    if (d.z < 0.0) d.xy = (1.0 - abs(d.yx)) * sign(d.xy);
    return normalize(d);
}

// Map integer bin (dx,dy) in [0,D)^2 to the bin's representative direction
vec3 binToDir(ivec2 bin, int D) {
    return octToDir((vec2(bin) + 0.5) / float(D));
}

// Cosine-weighted irradiance integral from one probe's D×D atlas tile.
// Excludes back-facing bins (dot < 0) — they cannot illuminate the surface.
vec3 sampleProbeDir(ivec3 pc, vec3 normal, int D) {
    vec3  irrad = vec3(0.0);
    float wsum  = 0.0;
    for (int dy = 0; dy < D; ++dy) {
        for (int dx = 0; dx < D; ++dx) {
            vec3  bdir = binToDir(ivec2(dx, dy), D);
            float w    = max(0.0, dot(bdir, normal));
            irrad += texelFetch(uDirectionalAtlas,
                                ivec3(pc.x * D + dx, pc.y * D + dy, pc.z), 0).rgb * w;
            wsum  += w;
        }
    }
    return irrad / max(wsum, 1e-4);
}

// Trilinear spatial blend over the 8 surrounding C0 probes, each cosine-weighted.
// -0.5 center-aligned offset: same convention as Phase 5d trilinear and Phase 5f bilinear.
// Returns vec3(0) when pos is outside the atlas grid.
vec3 sampleDirectionalGI(vec3 pos, vec3 normal) {
    vec3 uvw = (pos - uAtlasGridOrigin) / uAtlasGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return vec3(0.0);

    // Center-aligned probe-grid coordinate: probe k's center maps to float k
    vec3  pg   = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5,
                       vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
    ivec3 p000 = ivec3(floor(pg));
    vec3  f    = fract(pg);
    ivec3 hi   = uAtlasVolumeSize - ivec3(1);
    int   D    = uAtlasDirRes;

    vec3 s000 = sampleProbeDir(p000,                                    normal, D);
    vec3 s100 = sampleProbeDir(clamp(p000 + ivec3(1,0,0), ivec3(0), hi), normal, D);
    vec3 s010 = sampleProbeDir(clamp(p000 + ivec3(0,1,0), ivec3(0), hi), normal, D);
    vec3 s110 = sampleProbeDir(clamp(p000 + ivec3(1,1,0), ivec3(0), hi), normal, D);
    vec3 s001 = sampleProbeDir(clamp(p000 + ivec3(0,0,1), ivec3(0), hi), normal, D);
    vec3 s101 = sampleProbeDir(clamp(p000 + ivec3(1,0,1), ivec3(0), hi), normal, D);
    vec3 s011 = sampleProbeDir(clamp(p000 + ivec3(0,1,1), ivec3(0), hi), normal, D);
    vec3 s111 = sampleProbeDir(clamp(p000 + ivec3(1,1,1), ivec3(0), hi), normal, D);

    vec3 sx00 = mix(s000, s100, f.x);  vec3 sx10 = mix(s010, s110, f.x);
    vec3 sx01 = mix(s001, s101, f.x);  vec3 sx11 = mix(s011, s111, f.x);
    vec3 sxy0 = mix(sx00, sx10, f.y);  vec3 sxy1 = mix(sx01, sx11, f.y);
    return mix(sxy0, sxy1, f.z);
}

/**
 * @brief Shadow ray from surface point to light (Phase 5h).
 * Normal-offset origin avoids self-intersection without a fixed bias.
 * Returns 1.0 if occluded, 0.0 if visible.
 */
float shadowRay(vec3 hitPos, vec3 normal, vec3 lightPos) {
    vec3  toLight   = lightPos - hitPos;
    float distLight = length(toLight);
    vec3  ldir      = toLight / distLight;
    // Push origin along outward normal + small ldir offset for grazing incidence
    vec3  origin    = hitPos + normal * 0.02 + ldir * 0.01;
    float t         = 0.0;
    for (int i = 0; i < 32 && t < distLight; ++i) {
        float d = sampleSDF(origin + ldir * t);
        if (d >= 1e9) return 0.0;   // exited volume — light is outside, not occluded
        if (d < 0.002) return 1.0;  // hit geometry — in shadow
        t += max(d * 0.9, 0.01);
    }
    return 0.0;
}

/**
 * @brief SDF cone soft shadow (IQ-style) — Phase 5i.
 * Same origin convention as shadowRay(). Returns shadow factor 0=lit, 1=shadow.
 * res accumulates k*h/t; smaller h/t (narrow cone clearance) → lower res → more shadow.
 * Not physically equivalent to a point light — this is an appearance approximation.
 */
float softShadow(vec3 hitPos, vec3 normal, vec3 lightPos, float k) {
    vec3  toLight   = lightPos - hitPos;
    float distLight = length(toLight);
    vec3  ldir      = toLight / distLight;
    vec3  origin    = hitPos + normal * 0.02 + ldir * 0.01;
    float t         = 0.0;
    float res       = 1.0;
    for (int i = 0; i < 32 && t < distLight; ++i) {
        float d = sampleSDF(origin + ldir * t);
        if (d >= 1e9) return 0.0;                       // exited volume — light is outside, unoccluded
        if (d < 0.002) return 1.0;                       // hit geometry — fully in shadow
        res = min(res, k * d / max(t, 0.001));           // cone narrowing accumulation
        t  += max(d * 0.9, 0.01);
    }
    return 1.0 - clamp(res, 0.0, 1.0);  // convert: res=1→shadow=0 (lit), res=0→shadow=1
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

            // Debug mode 7: ray travel distance heatmap (continuous float t, not integer stepCount).
            // Use alongside mode 5 to separate "integer step-count quantization" banding from
            // "actual SDF iso-contour" banding. If mode 7 is smooth but mode 5 is banded,
            // the cause is integer quantization, not SDF resolution.
            if (uRenderMode == 7) {
                float tNorm = clamp((t - tNear) / max(tFar - tNear, 0.001), 0.0, 1.0);
                vec3 heatColor = (tNorm < 0.5)
                    ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), tNorm * 2.0)
                    : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (tNorm - 0.5) * 2.0);
                fragColor = vec4(heatColor, 1.0);
                return;
            }

            // Debug mode 6: GI-only — albedo * indirect, linear space, no tone map.
            // Probes store source-albedo-weighted radiance; multiply by destination
            // albedo here for energy-conserving Lambertian indirect.
            if (uRenderMode == 6) {
                vec3 indirect6 = (uUseDirectionalGI != 0 && uUseCascade != 0)
                    ? sampleDirectionalGI(pos, normal)
                    : texture(uRadiance, uvw).rgb;
                fragColor = vec4(clamp(albedo * indirect6, 0.0, 1.0), 1.0);
                return;
            }

            // Debug mode 4: direct light only (bypass cascade regardless of uUseCascade)
            if (uRenderMode == 4) {
                vec3  lightDir4 = normalize(uLightPos - pos);
                float shadow4   = (uUseShadowRay != 0)
                    ? ((uUseSoftShadow != 0) ? softShadow(pos, normal, uLightPos, uSoftShadowK)
                                             : shadowRay(pos, normal, uLightPos))
                    : 0.0;
                float diff4     = max(dot(normal, lightDir4), 0.0) * (1.0 - shadow4);
                vec3  direct    = albedo * (diff4 * uLightColor + vec3(0.05));
                fragColor = vec4(toneMapACES(direct), 1.0);
                fragColor.rgb = pow(fragColor.rgb, vec3(1.0 / 2.2));
                return;
            }

            // Mode 0: final rendering
            vec3  lightDir    = normalize(uLightPos - pos);
            float shadow      = (uUseShadowRay != 0)
                ? ((uUseSoftShadow != 0) ? softShadow(pos, normal, uLightPos, uSoftShadowK)
                                         : shadowRay(pos, normal, uLightPos))
                : 0.0;
            float diff        = max(dot(normal, lightDir), 0.0) * (1.0 - shadow);
            vec3  surfaceColor = albedo * (diff * uLightColor + vec3(0.05));

            // Probes store source-albedo-weighted radiance; multiply by destination
            // albedo for energy-conserving Lambertian: L_out = albedo_dest * integral(L_in*cos)/integral(cos)
            if (uUseCascade != 0) {
                vec3 indirect = (uUseDirectionalGI != 0)
                    ? sampleDirectionalGI(pos, normal)
                    : texture(uRadiance, uvw).rgb;
                surfaceColor += albedo * indirect;
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
