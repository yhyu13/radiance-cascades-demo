#version 430 core

/**
 * @file sdf_debug.vert
 * @brief Simple pass-through vertex shader for SDF debug visualization
 * 
 * Renders a full-screen quad with UV coordinates for sampling 3D SDF volume.
 */

// Output to fragment shader
out vec2 vUV;

void main() {
    // Generate full-screen quad vertices
    // Triangle strip covering [-1, -1] to [1, 1]
    vec2 positions[4] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0,  1.0)
    );
    
    // Calculate UV coordinates (0 to 1)
    vec2 pos = positions[gl_VertexID];
    vUV = pos * 0.5 + 0.5;
    
    // Set clip space position
    gl_Position = vec4(pos, 0.0, 1.0);
}
