#version 430 core

/**
 * @file sdf_debug.frag
 * @brief Debug visualization for 3D SDF volume
 * 
 * Displays a 2D cross-section of the 3D signed distance field
 * as a grayscale image. Useful for verifying SDF generation correctness.
 */

// Input from vertex shader
in vec2 vUV;

// Output color
out vec4 fragColor;

// Uniforms
uniform sampler3D sdfVolume;        // R32F SDF texture
uniform int sliceAxis;              // 0=X, 1=Y, 2=Z
uniform float slicePosition;        // 0.0 to 1.0 (normalized position along axis)
uniform vec3 volumeOrigin;          // World space origin
uniform vec3 volumeSize;            // World space size
uniform float visualizeMode;        // 0=grayscale, 1=surface (zero-crossing), 2=gradient magnitude

void main() {
    // Calculate 3D UV coordinates based on slice
    vec3 uvw;
    
    if (sliceAxis == 0) {
        // X-axis slice (YZ plane)
        uvw = vec3(slicePosition, vUV.x, vUV.y);
    } else if (sliceAxis == 1) {
        // Y-axis slice (XZ plane)
        uvw = vec3(vUV.x, slicePosition, vUV.y);
    } else {
        // Z-axis slice (XY plane) - default
        uvw = vec3(vUV.x, vUV.y, slicePosition);
    }
    
    // Sample SDF value
    float sdf = texture(sdfVolume, uvw).r;
    
    // Visualization modes
    if (visualizeMode < 0.5) {
        // Mode 0: Grayscale (raw SDF values)
        // Map SDF range [-2.0, 2.0] to [0.0, 1.0]
        float normalized = clamp((sdf + 2.0) / 4.0, 0.0, 1.0);
        fragColor = vec4(vec3(normalized), 1.0);
        
    } else if (visualizeMode < 1.5) {
        // Mode 1: Surface detection (highlight zero-crossings)
        // Surfaces have SDF ≈ 0
        float surfaceWeight = exp(-abs(sdf) * 10.0); // Sharp peak at 0
        vec3 color = mix(vec3(0.1), vec3(1.0, 0.8, 0.2), surfaceWeight);
        fragColor = vec4(color, 1.0);
        
    } else {
        // Mode 2: Gradient magnitude (edge detection)
        // Approximate gradient using finite differences
        float epsilon = 1.0 / 64.0; // Assuming 64³ resolution
        
        vec3 duvw = vec3(epsilon);
        float sdf_x = texture(sdfVolume, uvw + vec3(duvw.x, 0.0, 0.0)).r;
        float sdf_y = texture(sdfVolume, uvw + vec3(0.0, duvw.y, 0.0)).r;
        float sdf_z = texture(sdfVolume, uvw + vec3(0.0, 0.0, duvw.z)).r;
        
        vec3 gradient = vec3(sdf_x - sdf, sdf_y - sdf, sdf_z - sdf) / epsilon;
        float gradMag = length(gradient);
        
        // Visualize gradient magnitude
        float intensity = clamp(gradMag / 5.0, 0.0, 1.0);
        fragColor = vec4(vec3(intensity), 1.0);
    }
}
