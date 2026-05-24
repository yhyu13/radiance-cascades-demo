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
layout(location=0) out vec4 fragColor;

/** GBuffer for bilateral GI blur (location=1). Discarded when rendering to default framebuffer.
 *  rgb = world-space normal * 0.5 + 0.5, a = linearDepth in (0,1] (0 = sky/no surface). */
layout(location=1) out vec4 fragGBuffer;

/** Indirect-only (GI) term in linear light, mode 0 only (location=2).
 *  Written when uSeparateGI=1; gi_blur.frag blurs this independently and composites
 *  with fragColor (direct-only, also linear) before applying tone map + gamma.
 *  Discarded when rendering to the default framebuffer or in debug modes. */
layout(location=2) out vec4 fragGI;

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

/** Lighting controls follow-up: composite-side ambient floor strength
 *  (replaces the original hardcoded vec3(0.05) at the directColor formulas).
 *  Independent of uAmbientBakeStrength (cascade-bake-side floor) so the user
 *  can tune the visible-surface floor and the GI-bounce-source floor separately.
 *  Default 0.05 matches original literal. */
uniform float uAmbientCompositeStrength;

/** 2026-05-18 leak-suspect heatmap (render mode 14) scale.
 *  leak_potential >= uLeakHeatmapDivisor saturates to fully red.
 *  Default 0.05 picked so Phase 3 ON/OFF toggle produces visibly different color.
 *  Lower = more sensitive (tiny leaks turn red); higher = needs strong leak to turn red. */
uniform float uLeakHeatmapDivisor;

/** Phase 7 (PT reference, doc/7): half-res accumulator the PT compute shader writes.
 *  Mode 16 reads this directly; cascade-pipeline-derived data is bypassed at the
 *  output stage. The texture is GL_LINEAR / CLAMP_TO_EDGE so we just sample with
 *  normalized UVs from the fragment position. */
uniform sampler2D uPtAccum;
uniform int       uPtAccumValid;  // 0 = no PT accum bound (don't read); 1 = bound

/** 2026-05-19 Mode 18: cascade-vs-PT delta heatmap sensitivity divisor.
 *  delta_magnitude >= uDeltaHeatmapDivisor saturates to fully red/blue (signed bipolar).
 *  Default 0.2: typical Cornell radiance is ~0.3, so 0.2 captures most of the cascade-PT
 *  gap without over-saturating. Lower = more sensitive. */
uniform float uDeltaHeatmapDivisor;

/** 2026-05-19 Mode 19: PT direct-only accumulator (max-bounces=1) for GI-only delta.
 *  PT_GI = uPtAccum.rgb - uPtDirectAccum.rgb (full minus direct = pure indirect). */
uniform sampler2D uPtDirectAccum;

/** 2026-05-20 MBRC v2.0-pre (doc/7/mbrc_v20_pre_measurement_plan.md):
 *  uCascadeExclude — leave-one-out cascade attribution. Reported value is
 *  the LOGICAL exclusion index (which cascade is being attributed). The actual
 *  skip-in-merge bake-chain rewiring lives in C++ (Demo3D::updateAllCascades);
 *  here the uniform is forwarded purely for diagnostics + Mode 21 highlight,
 *  the consume path itself doesn't branch on it (cascade merge is bake-time).
 *  Range: -1 = no exclusion (baseline); 0..3 = exclude that cascade.
 *
 *  uErrorDecompMode — Mode 20 sub-mode selector for the Error Decomposition
 *  Heatmap. Values:
 *    0 = total cascade-PT delta luminance (same as mode 18 normalized magnitude)
 *    1 = direct-only delta (cascade direct - PT direct, requires PT direct accum)
 *    2 = indirect-only delta (cascade GI - PT GI)
 *    3 = relative error = |cascade-PT| / max(PT, eps)
 *
 *  uCascadeBaseInterval — C0's cellSize forwarded for camera-distance →
 *  cascade-dominance binning in Mode 21. Mirrors radiance_3d.comp's
 *  uBaseInterval; bake-side authoritative source is cascades[0].cellSize.
 *
 *  uCascadeCountConsume — number of active cascades (cascadeCount) for Mode
 *  21's dominance-bracket lookup. */
uniform int   uCascadeExclude;
uniform int   uErrorDecompMode;
uniform float uCascadeBaseInterval;
uniform int   uCascadeCountConsume;

/** 2026-05-19 Hybrid RC + Per-Pixel Correction (doc/7/hybrid_rc_pixel_correction_plan.md).
 *  Half-res RGBA32F accumulator written by hybrid_correction.comp. Stores per-pixel
 *  exact bounce-1 indirect (albedo × direct_at_random_bounce_hit) EMA-averaged over frames.
 *  Mode 0 path replaces cascade's bounce-1 contribution with this when uHybridCorrection != 0.
 *
 *  Composition (per critic-05 H1/H2):
 *      finalIndirect = mix(correction, cascadeIndirect, max(0, 1 - uHybridBlendWeight))
 *  At w=1 (default): pure correction (no double-count, but loses cascade's bounce-2+).
 *  At w<1: bias toward cascade for bounce-2+ at cost of bounce-1 double-counting. */
// v1.2 cooperative merge: read the BLURRED accumulator (.rgb = clean radiance, .a = E[L^2]).
// hybrid_blur.comp produces this from the raw accumulator + half-res GBuffer (normal+depth).
uniform sampler2D uHybridAccum;
uniform int       uHybridAccumValid;   // 0 = not bound (treat as cascade-only); 1 = bound
uniform int       uHybridCorrection;   // 0 = off; 1 = apply merge in mode 0
// Legacy mix() and max() modes retained for A/B; default is the variance merge below.
uniform float     uHybridBlendWeight;  // mix() weight: 0..1; default 1.0
uniform int       uHybridUseMax;       // legacy max(correction, cascade)
// 2026-05-19 v1.2 inverse-variance cooperative merge.
// Self-critic J4 (scale-invariance): variance scales as L^2 (bright pixels have higher
// absolute noise). Using ABSOLUTE variance biases the merge against bright regions.
// Fix: RELATIVE variance (coefficient-of-variation squared) → scale-invariant weights.
//   relVar = var / max(L^2, eps);  weight = 1 / relVar
// uHybridCascadeVariance is therefore a RELATIVE quantity (CoV^2 for cascade signal).
//
// Self-critic J7/J9 (first-frame confidence): with N=1 sample, E[L^2] == L^2 so absolute
// variance ≈ 0 → wCorr → ∞ → 1-frame flicker on every camera reset. Fix: ramp wCorr from
// 0 → full over first uHybridConfidenceSamples samples via a confidence multiplier.
uniform int       uHybridUseVarianceMerge;
uniform float     uHybridCascadeVariance;  // RELATIVE prior for cascade (CoV^2, default 0.001)
uniform int       uHybridSampleCount;      // current accumulator sample count (confidence gate)
uniform int       uHybridConfidenceSamples;// samples required for full correction trust (default 8)

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

/** Phase 9d: 1=output linear direct (location=0) and linear indirect (location=2) separately
 *  for the bilateral GI blur composite pass. 0=composite here and tone-map here (default). */
uniform int uSeparateGI;

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
//
// Phase 2 (interval atlas): the bake stores α as per-bin transparency (0 =
// opaque, 1 = transparent). The numerator α-gates bin radiance; the
// denominator uses the same cos*α weighting (mode-1/2/3/4-style "renormalize
// over visible directions"). Empirically this matched Phase 1 Mode 4 quality
// closer than the alternative (cos-only denominator) — the renormalization
// preserves the over-bright bias that the pre-Phase-2 bake encoded, which
// happens to compensate for the lost far-field multi-bounce in scenes where
// most C0 bins hit something (Sponza). See Phase 2 impl doc for the v3-vs-v4
// experiment that picked this normalization over the geometrically-purer
// cos-only divisor.
//
// Pre-Phase-2 this sampler ignored α entirely (Mode 0 had no visibility check;
// Modes 1/2 used outer probeVisibility(); Mode 3/4 had their own samplers).
// After Phase 2, bake handles visibility natively via α; this single sampler
// is the only correct path. Modes 1/2/3/4 are deprecated — see 2C cleanup.
// 2026-05-18 (critic-16 W1 refactor + mode-15 extension): unified function returning
// irradiance + diagnostic metrics in a single D² loop. Cheap extra ALU per bin;
// mode-0/6 callers ignore the diagnostics, mode-14/15 callers consume them.
//
// ProbeSample fields:
//   irrad:       vec3, normal cosine-weighted irradiance (mode 0/6 consumer)
//   leak:        float, leak-suspect luminance (mode 14 consumer)
//   oscillation: float in [0, 1], temporal-instability metric (mode 15 consumer)
//
// **leak** = sum(a.rgb * wcos * (1 - a.a)) over forward bins, luminance-reduced.
//   Quantifies "radiance the atlas stores in bins Phase 2 marks as occluded" — the
//   content the render-side α-gate hides from display.
//   Caveat (critic-16 W2): sky exit (α=0) is a FALSE POSITIVE.
//   Caveat (critic-16 W4): post EMA-α fix, soft α also reads as "partial leak."
//
// **oscillation** = sum(wcos * 4*a.a*(1-a.a)) / sum(wcos).
//   Per-bin 4*x*(1-x) peaks at 1.0 when x=0.5 (max temporal mixing: hit half the
//   time, miss the other half due to probe jitter) and is 0 at x=0 or x=1 (fully
//   converged binary α). Bright = probe is in a temporally-noisy region (sub-cell
//   geometry edges, jitter probing across walls). After EMA-α fix this is mostly
//   low; spikes indicate unconverged history or genuinely noise-prone directions.
struct ProbeSample {
    vec3  irrad;
    float leak;
    float oscillation;
};

ProbeSample sampleProbeDir(ivec3 pc, vec3 normal, int D) {
    vec3  irrad   = vec3(0.0);
    float wsum    = 0.0;        // sum(wcos * a.a) — for irrad normalization
    float wcosSum = 0.0;        // sum(wcos)        — for oscillation normalization
    vec3  leakRgb = vec3(0.0);
    float oscSum  = 0.0;        // sum(wcos * 4*a.a*(1-a.a))
    for (int dy = 0; dy < D; ++dy) {
        for (int dx = 0; dx < D; ++dx) {
            vec3  bdir = binToDir(ivec2(dx, dy), D);
            float wcos = max(0.0, dot(bdir, normal));
            vec4  a    = texelFetch(uDirectionalAtlas,
                                    ivec3(pc.x * D + dx, pc.y * D + dy, pc.z), 0);
            float w    = wcos * a.a;
            irrad   += a.rgb * w;
            wsum    += w;
            wcosSum += wcos;
            leakRgb += a.rgb * wcos * (1.0 - a.a);
            oscSum  += wcos * 4.0 * a.a * (1.0 - a.a);
        }
    }
    ProbeSample r;
    r.irrad       = irrad / max(wsum, 1e-4);
    r.leak        = dot(leakRgb, vec3(0.2126, 0.7152, 0.0722));
    r.oscillation = oscSum / max(wcosSum, 1e-4);
    return r;
}

// (sampleProbeDirPerBinOccluded and sampleProbeDirDepthAware removed in Phase 2
// 2C cleanup. The bake-side α-gate inside sampleProbeDir is now the single
// visibility path. See Phase 2 impl doc for the rationale.)

// Trilinear spatial blend over the 8 surrounding C0 probes, each cosine-weighted.
// -0.5 center-aligned offset: same convention as Phase 5d trilinear and Phase 5f bilinear.
// Returns ProbeSample (irradiance / leak / oscillation), trilinear-blended field-by-field.
ProbeSample sampleDirectionalGI(vec3 pos, vec3 normal) {
    ProbeSample zero;
    zero.irrad = vec3(0.0); zero.leak = 0.0; zero.oscillation = 0.0;
    vec3 uvw = (pos - uAtlasGridOrigin) / uAtlasGridSize;
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0))))
        return zero;

    // Center-aligned probe-grid coordinate: probe k's center maps to float k
    vec3  pg   = clamp(uvw * vec3(uAtlasVolumeSize) - 0.5,
                       vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
    ivec3 p000 = ivec3(floor(pg));
    vec3  f    = fract(pg);
    ivec3 hi   = uAtlasVolumeSize - ivec3(1);
    int   D    = uAtlasDirRes;

    ivec3 offsets[8] = ivec3[8](
        ivec3(0,0,0), ivec3(1,0,0), ivec3(0,1,0), ivec3(1,1,0),
        ivec3(0,0,1), ivec3(1,0,1), ivec3(0,1,1), ivec3(1,1,1));
    float w[8];
    w[0] = (1.0-f.x)*(1.0-f.y)*(1.0-f.z);
    w[1] =      f.x *(1.0-f.y)*(1.0-f.z);
    w[2] = (1.0-f.x)*     f.y *(1.0-f.z);
    w[3] =      f.x *     f.y *(1.0-f.z);
    w[4] = (1.0-f.x)*(1.0-f.y)*     f.z;
    w[5] =      f.x *(1.0-f.y)*     f.z;
    w[6] = (1.0-f.x)*     f.y *     f.z;
    w[7] =      f.x *     f.y *     f.z;

    ProbeSample r;
    r.irrad = vec3(0.0); r.leak = 0.0; r.oscillation = 0.0;
    for (int i = 0; i < 8; ++i) {
        ivec3 pc = p000 + offsets[i];
        bool inBounds = !(any(lessThan(pc, ivec3(0))) || any(greaterThan(pc, hi)));
        if (!inBounds) continue;
        ProbeSample s = sampleProbeDir(pc, normal, D);
        r.irrad       += s.irrad       * w[i];
        r.leak        += s.leak        * w[i];
        r.oscillation += s.oscillation * w[i];
    }
    return r;
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
    // Default GBuffer = sky (a=0 signals no surface to the blur pass)
    fragGBuffer = vec4(0.0);
    fragGI      = vec4(0.0);

    // Phase 7 (PT reference): mode 16 displays ptAccumTexture directly. Skip the
    // entire SDF raymarch + GI pipeline — pure texture display + tonemap. This is
    // intentionally short-circuited at the TOP so the expensive cascade work isn't
    // wasted on a pixel we're going to overwrite. (Cascade BAKE still runs upstream
    // — that's the cascade-overhead noted in plan §8 W7.)
    if (uRenderMode == 16) {
        vec3 ptRgb = (uPtAccumValid != 0) ? texture(uPtAccum, vUV).rgb : vec3(0.0);
        // Same tonemap as mode 0 for apples-to-apples comparison (W7 of plan: tonemap consistency).
        vec3 mapped = toneMapACES(ptRgb);
        fragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
        return;
    }

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

            // Write GBuffer for bilateral blur pass (discarded when no FBO attachment at location=1)
            {
                float linearDepth = clamp((t - tNear) / max(tFar - tNear, 0.001), 0.001, 1.0);
                fragGBuffer = vec4(normal * 0.5 + 0.5, linearDepth);
            }

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
                if (uSeparateGI != 0) {
                    fragColor = vec4(0.0);
                    fragGI    = vec4(indirect * 5.0, 1.0);
                    return;
                }
                fragColor = vec4(toneMapACES(indirect * 5.0), 1.0);
                return;
            }

            // Sample material albedo (shared by modes 0, 4)
            vec3 uvw    = (pos - uVolumeMin) / (uVolumeMax - uVolumeMin);
            vec3 albedo = texture(uAlbedo, uvw).rgb;

            // Debug mode 8: probe cell boundary visualization.
            // Shows fract(pg) as RGB where pg is the continuous probe-grid coordinate at pos.
            //   R = fract(probe-x), G = fract(probe-y), B = fract(probe-z).
            // Color transitions (fract wraps 1→0) occur at probe CENTER positions.
            // Halfway between centers (fract=0.5) is the cell boundary (trilinear blend weight=0.5).
            // Compare with mode 6 (GI-only): if banding in mode 6 aligns with mode 8 transitions,
            // the banding is probe-spatial Type A (cell-size limited). If not aligned → Type B
            // (directional quantization from finite D).
            if (uRenderMode == 8) {
                vec3 uvw5 = (pos - uAtlasGridOrigin) / uAtlasGridSize;
                vec3 pg5  = clamp(uvw5 * vec3(uAtlasVolumeSize) - 0.5,
                                  vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
                fragColor = vec4(fract(pg5), 1.0);
                return;
            }

            // Debug mode 7: ray travel distance heatmap (continuous float t, not integer stepCount).
            // Mode 7 is the continuous analogue of step-count mode 5: if mode 7 is smooth but
            // mode 5 (step count) is banded, the cause is integer quantization, not SDF resolution.
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
                    ? sampleDirectionalGI(pos, normal).irrad
                    : texture(uRadiance, uvw).rgb;
                if (uSeparateGI != 0) {
                    fragColor = vec4(0.0);
                    fragGI    = vec4(albedo * indirect6, 1.0);
                    return;
                }
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
                vec3  direct    = albedo * (diff4 * uLightColor + vec3(uAmbientCompositeStrength));
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
            float diff         = max(dot(normal, lightDir), 0.0) * (1.0 - shadow);
            vec3  directColor  = albedo * (diff * uLightColor + vec3(uAmbientCompositeStrength));
            vec3  indirectColor = vec3(0.0);
            // Step 11 (codex 07 F3): hoist `indirect` to outer scope so the
            // heatmap modes (12 = raw GI) can read the un-albedo-modulated
            // probe radiance. Stays vec3(0.0) when uUseCascade == 0 -- mode
            // 12 then degenerates to all-green (tooltip warns).
            vec3  indirect      = vec3(0.0);

            // Probes store source-albedo-weighted radiance; multiply by destination
            // albedo for energy-conserving Lambertian: L_out = albedo_dest * integral(L_in*cos)/integral(cos)
            if (uUseCascade != 0) {
                indirect = (uUseDirectionalGI != 0)
                    ? sampleDirectionalGI(pos, normal).irrad
                    : texture(uRadiance, uvw).rgb;
                indirectColor = albedo * indirect;
            }

            // 2026-05-19 Hybrid RC + Per-Pixel Correction (doc/7).
            // Replace (or blend) cascade's bounce-1 with exact MC-integrated bounce-1
            // from hybridAccumTexture. Composition per critic-05 H1/H2:
            //   finalIndirect = mix(correction, cascadeIndirect, max(0, 1 - w))
            // At w=1: pure correction (default; no double-count; loses cascade's bounce-2+).
            // At w<1: bias toward cascade (adds bounce-2+ but double-counts bounce-1).
            // Sample at the SCREEN-SPACE pixel UV — the accumulator is screen-aligned, not
            // world-aligned, so we use vUV (already in [0,1]) directly. Bilinear filter.
            if (uHybridCorrection != 0 && uHybridAccumValid != 0) {
                vec4 corrRGBA = texture(uHybridAccum, vUV);   // bilateral-filtered correction
                vec3 correction = corrRGBA.rgb;

                if (uHybridUseVarianceMerge != 0) {
                    // v1.2.2 (Phase 8 B2 redesign): sample-count cooperative merge.
                    // The previous post-blur "variance" estimate was contaminated by spatial
                    // signal variance (every edge inflated it), making cascade always win.
                    // Replacement: use sample count as confidence proxy. Both signals always
                    // contribute; correction's share grows with accumulator samples.
                    //
                    //   wCorr = uHybridSampleCount / uHybridConfidenceSamples
                    //   wCasc = 1.0   (baseline)
                    //   final = (wCorr*corr + wCasc*casc) / (wCorr + wCasc)
                    //
                    // At spp=0:           wCorr=0    → 100% cascade (smooth fallback after reset)
                    // At spp=confSamples: wCorr=1.0 → 50/50 mix
                    // At spp=10×conf:     wCorr=10  → ~91% correction, 9% cascade (still keeps
                    //                                  cascade's smooth structure visibly)
                    // This is COOPERATIVE: cascade contributes its smooth color-bleed +
                    // multi-bounce; correction adds PT-quality bounce-1 detail. Neither
                    // dominates entirely.
                    float wCorr = float(uHybridSampleCount)
                                  / max(float(uHybridConfidenceSamples), 1.0);
                    float wCasc = 1.0;
                    indirectColor = (wCorr * correction + wCasc * indirectColor)
                                  / (wCorr + wCasc);
                } else if (uHybridUseMax != 0) {
                    indirectColor = max(correction, indirectColor);
                } else {
                    float w = clamp(uHybridBlendWeight, 0.0, 1.0);
                    indirectColor = mix(correction, indirectColor, max(0.0, 1.0 - w));
                }
            }

            // Step 11 (codex 07 F3): GI heatmaps. Inserted AFTER the main-path
            // lighting computation (line 535-544) so they CONSUME directColor /
            // indirectColor / indirect -- unlike modes 4/6 which compute their
            // own. Same green/yellow/red palette as modes 5 & 7.
            if (uRenderMode == 11 || uRenderMode == 12 || uRenderMode == 13) {
                float v;
                // codex 07 F7: divisors picked from Step 10 mode-6 magnitudes
                // (Sponza body ~0.04, rim ~0.34). Retune via shader edit if saturated.
                if      (uRenderMode == 11) v = length(albedo * indirect) / 0.1;   // visible GI
                else if (uRenderMode == 12) v = length(indirect)          / 0.5;   // raw GI -- codex 08 F5 retune (was /0.05, saturated red)
                else /* 13 */ {
                    // codex 07 F8: 0.001 threshold safe for current asset albedos.
                    float total = length(directColor + indirectColor);
                    v = (total > 0.001) ? length(indirectColor) / total : 0.0;     // already 0..1
                }
                float t8 = clamp(v, 0.0, 1.0);
                vec3 heatColor = (t8 < 0.5)
                    ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t8 * 2.0)
                    : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t8 - 0.5) * 2.0);
                fragColor = vec4(heatColor, 1.0);
                return;
            }

            // 2026-05-19 Mode 18: cascade-vs-PT delta heatmap.
            // Computes the SIGNED per-pixel delta between cascade output and PT truth.
            // Bipolar colormap: red = cascade brighter than PT, blue = cascade dimmer.
            // White = exact match. Use to find WHERE cascade integration approximation
            // diverges from the truth.
            //
            // Requires: render mode 18 ALSO triggers PT dispatch (in demo3d.cpp); PT
            // should typically run in --pt-cascade-match=1 mode for apples-to-apples
            // (otherwise the ambient-floor bias contaminates the delta).
            //
            // 2026-05-19 Mode 19: cascade_GI-vs-PT_GI delta heatmap.
            // Same as mode 18 but isolates the GI/indirect component on BOTH sides.
            // Necessary because mode 18 can be deceived: when cascade over-saturates
            // direct lighting AND under-integrates GI, the total brightness can match
            // PT total while the GI signal is much weaker. Mode 19 strips direct and
            // shows the pure indirect comparison — the user's "5× weaker GI" claim
            // becomes visible here.
            //
            // PT_GI = full_PT - direct_only_PT (both accumulators populated by C++ in
            // two PT dispatches; see ptDispatchReference). cascade_GI = indirectColor
            // (computed by the standard cascade display path).
            if (uRenderMode == 19) {
                vec3 cascadeGI = indirectColor;  // standard cascade indirect-only
                vec3 ptFull   = (uPtAccumValid != 0) ? texture(uPtAccum, vUV).rgb : vec3(0.0);
                vec3 ptDirect = (uPtAccumValid != 0) ? texture(uPtDirectAccum, vUV).rgb : vec3(0.0);
                vec3 ptGI     = max(ptFull - ptDirect, vec3(0.0));  // pure indirect; clamp neg
                vec3 delta    = cascadeGI - ptGI;
                float deltaLum = dot(delta, vec3(0.2126, 0.7152, 0.0722));
                float normalized = clamp(deltaLum / max(uDeltaHeatmapDivisor, 1e-4), -1.0, 1.0);
                vec3 heatColor;
                if (normalized < 0.0) {
                    float t = -normalized;
                    heatColor = mix(vec3(1.0), vec3(0.0, 0.4, 1.0), t);
                } else {
                    heatColor = mix(vec3(1.0), vec3(1.0, 0.2, 0.0), normalized);
                }
                fragColor = vec4(heatColor, 1.0);
                return;
            }

            // 2026-05-20 Mode 20: Error Decomposition Heatmap (MBRC v2.0-pre §3).
            // Same bipolar colormap as mode 18, but uErrorDecompMode picks which
            // delta component is visualized:
            //   0 = total (cascade_full vs PT_full luminance delta) — equivalent to mode 18
            //   1 = direct-only delta
            //   2 = indirect-only delta (uses PT_GI = PT_full - PT_direct)
            //   3 = relative error |cascade - PT| / max(PT, eps), unipolar red/yellow/green
            //
            // The purpose is bin-aware diagnostic for the v2.0-pre report (see
            // doc/7/mbrc_v20_pre_measurement_plan.md §2.2). Combined with cascade-
            // dominance bins (mode 21), this lets the report attribute error to
            // specific cascades * specific components.
            if (uRenderMode == 20) {
                vec3 cascadeFull   = directColor + indirectColor;
                vec3 cascadeDirect = directColor;
                vec3 cascadeGI20   = indirectColor;
                vec3 ptFull   = (uPtAccumValid != 0) ? texture(uPtAccum, vUV).rgb : vec3(0.0);
                vec3 ptDirect = (uPtAccumValid != 0) ? texture(uPtDirectAccum, vUV).rgb : vec3(0.0);
                vec3 ptGI20   = max(ptFull - ptDirect, vec3(0.0));
                vec3 deltaCmp;
                bool unipolar = false;
                if      (uErrorDecompMode == 1) deltaCmp = cascadeDirect - ptDirect;
                else if (uErrorDecompMode == 2) deltaCmp = cascadeGI20   - ptGI20;
                else if (uErrorDecompMode == 3) {
                    // Relative error: per-channel |Δ| / max(PT_full, eps), then luminance.
                    vec3 rel = abs(cascadeFull - ptFull) / max(ptFull, vec3(1e-3));
                    float relLum = dot(rel, vec3(0.2126, 0.7152, 0.0722));
                    float t20 = clamp(relLum / max(uDeltaHeatmapDivisor, 1e-4), 0.0, 1.0);
                    vec3 hc = (t20 < 0.5)
                        ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t20 * 2.0)
                        : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t20 - 0.5) * 2.0);
                    fragColor = vec4(hc, 1.0);
                    return;
                }
                else /* 0 = total */            deltaCmp = cascadeFull - ptFull;
                float dLum = dot(deltaCmp, vec3(0.2126, 0.7152, 0.0722));
                float norm = clamp(dLum / max(uDeltaHeatmapDivisor, 1e-4), -1.0, 1.0);
                vec3 hc20;
                if (norm < 0.0) hc20 = mix(vec3(1.0), vec3(0.0, 0.4, 1.0), -norm);
                else            hc20 = mix(vec3(1.0), vec3(1.0, 0.2, 0.0),  norm);
                fragColor = vec4(hc20, 1.0);
                return;
            }

            // 2026-05-20 Mode 21: Cascade Dominance Visualization (MBRC v2.0-pre §3).
            // Color-bins each pixel by which cascade's spatial bracket contains the
            // camera→hit distance `t`. Bracket schedule mirrors radiance_3d.comp's
            // tMin/tMax formula:
            //   C0: [0,           baseInterval        ]
            //   Ci: [4^(i-1)·base, 4^i ·base          ]   for i >= 1
            // The largest cascade that doesn't contain `t` falls into "saturate" (white).
            // Colors: C0=red, C1=yellow, C2=green, C3=blue, beyond=white. The excluded
            // cascade (uCascadeExclude) overlays a dimmed/desaturated tint to make the
            // skip-in-merge experiment self-documenting on a screenshot.
            //
            // CAVEAT (self-critic §2 of impl doc): camera-distance binning is a PROXY for
            // the "which cascade's probe-ray bracket dominates this pixel's GI" question.
            // The exact dominance would require per-probe per-bin smoothstep weight readout
            // (not exposed by this architecture without a second pass). This proxy is the
            // pragmatic Mode-21 readout the v2.0-pre report uses.
            if (uRenderMode == 21) {
                int dom = 0;
                float bi = max(uCascadeBaseInterval, 1e-6);
                int N = max(uCascadeCountConsume, 1);
                for (int i = 0; i < 8; ++i) {
                    if (i >= N) break;
                    float tMaxI = (i == 0) ? bi : pow(4.0, float(i)) * bi;
                    if (t < tMaxI) { dom = i; break; }
                    dom = i; // saturate at highest cascade if t exceeds all brackets
                }
                vec3 palette[5];
                palette[0] = vec3(1.0, 0.25, 0.25);  // C0 red
                palette[1] = vec3(1.0, 1.0,  0.25);  // C1 yellow
                palette[2] = vec3(0.25, 1.0, 0.35);  // C2 green
                palette[3] = vec3(0.30, 0.55, 1.0);  // C3 blue
                palette[4] = vec3(1.0);              // saturate / beyond
                int idx = (dom < 4) ? dom : 4;
                vec3 col21 = palette[idx];
                // Dim the excluded cascade so the experiment is visually obvious.
                if (uCascadeExclude >= 0 && uCascadeExclude == dom) {
                    col21 = mix(vec3(0.0), col21, 0.35);  // 35% intensity
                }
                fragColor = vec4(col21, 1.0);
                return;
            }

            // 2026-05-24 Mode 22: Per-Pixel Dominant Direction Bin Viz (v2.0 P2 bake-side diag).
            // For each surface pixel, finds the (dx,dy) atlas direction-bin index that
            // contributes most to the luminance at this pixel under the nearest-parent
            // probe sampling path (matches the ST=0 default flipped 2026-05-24). The
            // contribution per bin = luminance(a.rgb) * wcos(normal,bdir) * a.a — i.e.
            // the same integrand sampleProbeDir averages, isolated to per-bin terms.
            //
            // Output encoding (HDR EXR-compatible for round-tripping):
            //   R = (dx_best + 0.5) / D     analyzer recovers dx = floor(R * D)
            //   G = (dy_best + 0.5) / D     analyzer recovers dy = floor(G * D)
            //   B = top_contrib / total_contrib  (dominance fraction in [0,1]; 0 if no GI)
            //
            // Purpose: converts the inferential "bake-side per-direction-bin atlas
            // content asymmetry" framing (downstream path locked-in INNOCENT by
            // h.b/h.c/h.c'/h.c'' 4-A/B chain — see
            // doc/7/v20_downstream_symmetrizer_architecture.md) into DIRECT
            // measurement. If cam0 and cam2 sample dominant bins from systematically
            // different (dx,dy) regions, the asymmetry is in atlas content.
            //
            // Uses nearest-parent probe (no spatial interpolation) so the readout
            // directly reflects atlas content at the probe the pixel sees, not a
            // neighborhood average (which would itself be a symmetrizer).
            if (uRenderMode == 22) {
                vec3 dombinRGB = vec3(0.0);
                vec3 uvw22 = (pos - uAtlasGridOrigin) / uAtlasGridSize;
                bool inVol = !(any(lessThan(uvw22, vec3(0.0))) || any(greaterThan(uvw22, vec3(1.0))));
                if (inVol) {
                    vec3  pg22 = clamp(uvw22 * vec3(uAtlasVolumeSize) - 0.5,
                                       vec3(0.0), vec3(uAtlasVolumeSize - ivec3(1)));
                    ivec3 hi22 = uAtlasVolumeSize - ivec3(1);
                    ivec3 pc22 = clamp(ivec3(floor(pg22 + 0.5)), ivec3(0), hi22);
                    int   D22  = uAtlasDirRes;
                    float topContrib = 0.0;
                    float sumContrib = 0.0;
                    ivec2 best = ivec2(0, 0);
                    for (int dy = 0; dy < D22; ++dy) {
                        for (int dx = 0; dx < D22; ++dx) {
                            vec3  bdir = binToDir(ivec2(dx, dy), D22);
                            float wcos = max(0.0, dot(bdir, normal));
                            vec4  a    = texelFetch(uDirectionalAtlas,
                                                    ivec3(pc22.x * D22 + dx, pc22.y * D22 + dy, pc22.z), 0);
                            float lum  = dot(a.rgb, vec3(0.2126, 0.7152, 0.0722));
                            float c    = lum * wcos * a.a;
                            sumContrib += c;
                            if (c > topContrib) { topContrib = c; best = ivec2(dx, dy); }
                        }
                    }
                    float frac = (sumContrib > 1e-6) ? (topContrib / sumContrib) : 0.0;
                    dombinRGB = vec3((float(best.x) + 0.5) / float(D22),
                                     (float(best.y) + 0.5) / float(D22),
                                     frac);
                }
                fragColor = vec4(dombinRGB, 1.0);
                fragGI    = vec4(dombinRGB, 1.0);  // EXR capture path (mirrors mode 17 MRT)
                return;
            }

            // On cornell-orig, we expect BLUE dominates (cascade is 42% darker per PT).
            if (uRenderMode == 18) {
                vec3 cascadeOutput = directColor + indirectColor;
                vec3 ptTruth = (uPtAccumValid != 0)
                    ? texture(uPtAccum, vUV).rgb
                    : vec3(0.0);
                vec3 delta = cascadeOutput - ptTruth;
                // Signed luminance delta — positive = cascade brighter, negative = dimmer.
                float deltaLum = dot(delta, vec3(0.2126, 0.7152, 0.0722));
                float normalized = clamp(deltaLum / max(uDeltaHeatmapDivisor, 1e-4), -1.0, 1.0);
                // Bipolar colormap:
                //   normalized = -1 → deep blue (cascade much dimmer than PT)
                //   normalized =  0 → white (exact match)
                //   normalized = +1 → deep red (cascade much brighter than PT)
                vec3 heatColor;
                if (normalized < 0.0) {
                    // Cascade dim: white → blue
                    float t = -normalized;
                    heatColor = mix(vec3(1.0), vec3(0.0, 0.4, 1.0), t);
                } else {
                    // Cascade bright: white → red
                    heatColor = mix(vec3(1.0), vec3(1.0, 0.2, 0.0), normalized);
                }
                fragColor = vec4(heatColor, 1.0);
                return;
            }

            // Mode 14: leak-suspect heatmap (2026-05-18).
            // Visualizes per-pixel "leak potential" = the radiance from atlas bins that
            // Phase 2's render-side α-gate hides from display. Bright red = high leak
            // potential at this pixel = Phase 3 has the most leverage here. The metric
            // is computed using only the existing atlas (no Phase 3 mode toggle needed).
            //
            // Reading: green = no leak, yellow = some leak in the atlas, red = the atlas
            // at this probe stored significant radiance in directions marked occluded.
            // Phase 2 prevents this leak from reaching display; Phase 3 (v3) reduces the
            // amount that gets baked into the atlas in the first place.
            if (uRenderMode == 14 || uRenderMode == 15) {
                ProbeSample ps = (uUseCascade != 0)
                    ? sampleDirectionalGI(pos, normal)
                    : ProbeSample(vec3(0.0), 0.0, 0.0);
                float v;
                if (uRenderMode == 14) {
                    // Mode 14 (LeakSuspect): sqrt-scaled leak luminance.
                    v = sqrt(ps.leak / max(uLeakHeatmapDivisor, 1e-4));
                } else {
                    // Mode 15 (TemporalOscillation): already in [0,1]; no divisor needed.
                    // Optional sqrt for perceptual sensitivity to small oscillation.
                    v = sqrt(ps.oscillation);
                }
                float t8 = clamp(v, 0.0, 1.0);
                vec3 heatColor = (t8 < 0.5)
                    ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t8 * 2.0)
                    : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t8 - 0.5) * 2.0);
                fragColor = vec4(heatColor, 1.0);
                return;
            }

            // uSeparateGI=1: output linear direct + linear indirect separately for the
            // bilateral GI blur composite pass. Tone mapping moves to gi_blur.frag.
            // Step 10 (codex 06 F3): gate on uRenderMode == 0 so the new diagnostic
            // modes (9, 10) always reach the composite below; the GI-blur split path
            // is only meaningful for the default render mode.
            // 2026-05-22 HDR-EXR honest metric: mode 17 (GI-only) joins the split
            // path when --screenshot-exr is on (C++ widens uSeparateGI accordingly)
            // so fragGI receives HDR-linear cascade indirect for the EXR dump.
            // fragColor still uses indirectColor in mode 17 to preserve the
            // mode-17 PNG visual cross-check.
            if (uSeparateGI != 0 && (uRenderMode == 0 || uRenderMode == 17)) {
                vec3 outColor = (uRenderMode == 17) ? indirectColor : directColor;
                fragColor = vec4(outColor,      1.0);
                fragGI    = vec4(indirectColor, 1.0);
                return;
            }

            // Step 10 (codex 06 F2 + F8): GI diagnostic modes. Mode 9 strips the
            // hidden vec3(0.05) ambient floor; mode 10 shows ONLY that floor.
            // Mode 4 (existing) = Mode 9 + Mode 10. Comparing 6 vs 10 reveals
            // whether the ambient floor is washing out cascade GI bounce.
            vec3 modeColor;
            if      (uRenderMode == 9)  modeColor = albedo * diff * uLightColor;
            else if (uRenderMode == 10) modeColor = albedo * vec3(uAmbientCompositeStrength);
            // 2026-05-19 Mode 17: GI-only isolated. Pure indirect bounce from cascade
            // atlas; NO direct light, NO ambient floor, NO shadow. Toggle "Temporal
            // multi-bounce (Phase MB)" in Hierarchy & Merge tab — image visibly
            // brightens with MB ON because multi-bounce adds to the atlas. Differs
            // from mode 6 which still composites direct via uSeparateGI path; mode 17
            // is the pure-GI viewer for MB A/B comparisons.
            else if (uRenderMode == 17) modeColor = indirectColor;
            else                        modeColor = directColor + indirectColor;

            // Normal path: composite here, tone map after the loop.
            float alpha = 1.0;
            accumulatedColor += modeColor * alpha * (1.0 - accumulatedAlpha);
            accumulatedAlpha += alpha * (1.0 - accumulatedAlpha);

            break;
        }
        
        // Advance by SDF distance (with safety factor)
        float stepSize = max(dist * 0.7, 0.01);
        t += stepSize;
        
        if (t > tFar)
            break;
    }
    
    // Debug mode 5: SDF step count heatmap (green=few, yellow=moderate, red=many/miss).
    // Normalize against 32 — Cornell Box rays typically hit in <32 steps.
    // Non-surface pixels show red (stepCount → max). Post-loop, non-surface-hit mode.
    // Pair with mode 7 (ray-travel-distance): if mode 7 is smooth but mode 5 is banded,
    // the cause is integer step-count quantization, not actual SDF iso-contour structure.
    if (uRenderMode == 5) {
        float t8 = clamp(float(stepCount) / 32.0, 0.0, 1.0);
        vec3 heatColor = (t8 < 0.5)
            ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), t8 * 2.0)
            : mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t8 - 0.5) * 2.0);
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
