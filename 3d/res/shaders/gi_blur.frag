/**
 * @file gi_blur.frag
 * @brief Bilateral GI-only blur + composite pass.
 *
 * Reads linear direct (uDirectTex, location=0) and linear indirect/GI (uIndirectTex,
 * location=2) from the raymarch FBO, plus the GBuffer (uGBufferTex, location=1).
 *
 * Only uIndirectTex is blurred bilaterally using depth+normal weights from uGBufferTex.
 * uDirectTex is passed through unchanged. This preserves sharp direct shadows and
 * specular highlights while smoothing probe-grid GI banding on flat surfaces.
 *
 * Final composite: toneMapACES(direct + blurred_indirect) → gamma → screen.
 *
 * Sky pixels (fragGBuffer.a == 0) have indirect == 0 and are passed through unchanged.
 */

#version 430 core

in vec2 vUV;
out vec4 fragColor;

/** Linear direct lighting from raymarch pass (location=0). */
uniform sampler2D uDirectTex;

/** Linear indirect/GI from raymarch pass (location=2). Blurred bilaterally. */
uniform sampler2D uIndirectTex;

/** GBuffer: rgb = normal*0.5+0.5 (world space), a = linearDepth in (0,1]. 0 = sky. */
uniform sampler2D uGBufferTex;

/** Bilateral kernel half-radius in pixels (1 = 3x3, 4 = 9x9). */
uniform int uBlurRadius;

/** Depth difference (in linearDepth units) that reduces neighbor weight to ~0.6. */
uniform float uDepthSigma;

/** Normal difference (cosine-distance in [0,1]) that reduces neighbor weight to ~0.6. */
uniform float uNormalSigma;

/** GI luminance difference that reduces neighbor weight to ~0.6. 0.0 = disabled. */
uniform float uLumSigma;

vec3 toneMapACES(vec3 color) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);

    vec3 direct  = texelFetch(uDirectTex,   coord, 0).rgb;
    vec4 centerGB = texelFetch(uGBufferTex,  coord, 0);

    // Sky pixel or blur disabled: composite direct + unblurred indirect, tone map, done.
    vec3 indirect = texelFetch(uIndirectTex, coord, 0).rgb;
    if (centerGB.a < 1e-5 || uBlurRadius == 0) {
        vec3 composed = toneMapACES(direct + indirect);
        fragColor = vec4(pow(composed, vec3(1.0 / 2.2)), 1.0);
        return;
    }

    vec3  centerNorm  = centerGB.rgb * 2.0 - 1.0;
    float centerDepth = centerGB.a;

    // Bilateral blur on indirect only.
    vec3  accumIndirect = vec3(0.0);
    float accumW        = 0.0;

    for (int dy = -uBlurRadius; dy <= uBlurRadius; ++dy) {
        for (int dx = -uBlurRadius; dx <= uBlurRadius; ++dx) {
            ivec2 nc  = coord + ivec2(dx, dy);
            vec4  ngb = texelFetch(uGBufferTex, nc, 0);

            // Skip sky / out-of-bounds neighbors
            if (ngb.a < 1e-5) continue;

            vec3  nNorm  = ngb.rgb * 2.0 - 1.0;
            float nDepth = ngb.a;

            float dDepth  = abs(nDepth - centerDepth) / max(uDepthSigma, 1e-4);
            float wDepth  = exp(-0.5 * dDepth * dDepth);

            float cosDiff = 1.0 - clamp(dot(centerNorm, nNorm), 0.0, 1.0);
            float dNormal = cosDiff / max(uNormalSigma, 1e-4);
            float wNormal = exp(-0.5 * dNormal * dNormal);

            vec3  nGI    = texelFetch(uIndirectTex, nc, 0).rgb;
            float wLum   = 1.0;
            if (uLumSigma > 0.0) {
                float centerLum = dot(indirect, vec3(0.299, 0.587, 0.114));
                float nLum      = dot(nGI,      vec3(0.299, 0.587, 0.114));
                float dLum      = (centerLum - nLum) / max(uLumSigma, 1e-4);
                wLum = exp(-0.5 * dLum * dLum);
            }
            float w = wDepth * wNormal * wLum;
            accumIndirect += nGI * w;
            accumW        += w;
        }
    }

    vec3 blurredIndirect = accumIndirect / max(accumW, 1e-6);

    // Composite linear direct (unblurred) + linear blurred indirect, then tone map + gamma.
    vec3 composed = toneMapACES(direct + blurredIndirect);
    fragColor = vec4(pow(composed, vec3(1.0 / 2.2)), 1.0);
}
