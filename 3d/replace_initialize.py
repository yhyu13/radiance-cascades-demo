import re

# Read the file
with open('src/demo3d.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Define old function pattern (matches from line 53 to 74)
old_pattern = r'void RadianceCascade3D::initialize\(int res, float cellSz, const glm::vec3& org, int rays\) \{.*?// TODO: Implement cascade initialization.*?// - Set up framebuffer attachment\n\}'

# New implementation
new_function = '''void RadianceCascade3D::initialize(int res, float cellSz, const glm::vec3& org, int rays) {
    /**
     * @brief Initialize cascade parameters and create OpenGL texture
     * 
     * Implementation Steps:
     * 1. Store configuration parameters
     * 2. Calculate interval start/end based on cascade index
     * 3. Create 3D texture with gl::createTexture3D()
     * 4. Set texture parameters (filtering, wrap mode)
     * 5. Attach to framebuffer if needed
     * 
     * @param res Resolution (e.g., 32, 64, 128)
     * @param cellSz World space size per voxel
     * @param org World space origin
     * @param rays Rays per probe
     */
    
    // Step 1: Store configuration
    resolution = res;
    cellSize = cellSz;
    origin = org;
    raysPerProbe = rays;
    
    // Step 2: Calculate distance intervals (exponential cascade spacing)
    // Each cascade covers a range of distances for hierarchical lighting
    intervalStart = cellSz * static_cast<float>(res) * 0.5f;
    intervalEnd = intervalStart * 4.0f;  // Each cascade covers 4x the previous range
    
    // Step 3: Create 3D texture for radiance storage
    // Use RGBA16F for HDR radiance data (R,G,B + alpha for emission/opacity)
    probeGridTexture = gl::createTexture3D(
        resolution,
        resolution,
        resolution,
        GL_RGBA16F,      // Internal format: 16-bit float per channel (HDR)
        GL_RGBA,         // Pixel format
        GL_HALF_FLOAT,   // Data type: half-precision float
        nullptr          // Empty texture, will be filled by compute shader
    );
    
    // Step 4: Configure texture parameters for volume sampling
    gl::setTexture3DParameters(
        probeGridTexture,
        GL_LINEAR,       // Minification filter: linear interpolation
        GL_LINEAR,       // Magnification filter: linear interpolation
        GL_CLAMP_TO_EDGE // Wrap mode: clamp to edge (no repeating)
    );
    
    // Step 5: Mark as active
    active = true;
    
    std::cout << "  [OK] Cascade initialized: " << resolution << "^3, cell=" << cellSz 
              << ", texID=" << probeGridTexture << std::endl;
}'''

# Replace
content_new = re.sub(old_pattern, new_function, content, flags=re.DOTALL)

# Write back
with open('src/demo3d.cpp', 'w', encoding='utf-8') as f:
    f.write(content_new)

print('Replacement successful!')
