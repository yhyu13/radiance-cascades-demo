#version 430 core

/**
 * @file sdf_debug.vert
 * @brief Simple vertex shader for full-screen quad
 */

// Vertex position (clip space)
layout(location = 0) in vec2 aPos;

// Output UV coordinates
out vec2 vUV;

void main() {
    // Pass through position
    gl_Position = vec4(aPos, 0.0, 1.0);
    
    // Convert from [-1, 1] to [0, 1] for UV
    vUV = aPos * 0.5 + 0.5;
}
