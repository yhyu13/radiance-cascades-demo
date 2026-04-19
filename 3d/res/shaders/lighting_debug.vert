#version 430 core

/**
 * @file lighting_debug.vert
 * @brief Simple pass-through vertex shader for per-light debug visualization
 * 
 * Renders a full-screen quad with UV coordinates for sampling lighting data.
 */

// Output to fragment shader
out vec2 vTexCoords;

void main() {
    // Generate full-screen quad vertices using triangle strip
    vec2 positions[4] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0,  1.0)
    );
    
    // Calculate texture coordinates (0 to 1)
    vec2 pos = positions[gl_VertexID];
    vTexCoords = pos * 0.5 + 0.5;
    
    // Set clip space position
    gl_Position = vec4(pos, 0.0, 1.0);
}
