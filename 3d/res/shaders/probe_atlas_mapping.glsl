#version 430 core

// =============================================================================
// Phase 3A: Chart-Aware Probe-to-Atlas Mapping
// Converts probe coordinates to correct atlas UV accounting for chart layout
// =============================================================================

/**
 * @brief Convert probe coordinate to atlas UV with chart layout awareness
 * @param chartID Chart identifier (1-18)
 * @param probeCoord Probe grid coordinates [0, res-1]
 * @param cascadeLevel Cascade level (0-4)
 * @param cascadeResolutions Array of resolutions per level
 * @return Texture UV coordinates [0, 1]
 */
vec2 probeCoordToAtlasUV_Advanced(int chartID, vec3 probeCoord, int cascadeLevel, 
                                   const int cascadeResolutions[5]) {
    if (chartID < 1 || chartID > 18) return vec2(0.0);
    
    int res = cascadeResolutions[cascadeLevel];
    
    // Atlas dimensions (must match C++ constants)
    const int atlasWidth = 2560;
    const int atlasHeight = 1536;
    
    // Chart layout: 6 charts per row, 3 rows
    const int chartsPerRow = 6;
    int chartWidth = atlasWidth / chartsPerRow;     // ~427 pixels
    int chartHeight = atlasHeight / res;            // Varies by level
    
    // Calculate chart base position in atlas
    int row = (chartID - 1) / chartsPerRow;
    int col = (chartID - 1) % chartsPerRow;
    
    int chartBaseX = col * chartWidth;
    int chartBaseY = row * chartHeight;
    
    // Map probe XYZ to local chart UV
    float localU = probeCoord.x / float(res);
    float localV = probeCoord.y / float(res);
    
    // Convert to atlas UV
    float atlasU = (float(chartBaseX) + localU * float(chartWidth)) / float(atlasWidth);
    float atlasV = (float(chartBaseY) + localV * float(chartHeight)) / float(atlasHeight);
    
    return vec2(atlasU, atlasV);
}

/**
 * @brief Sample cascade atlas with chart-aware mapping
 */
vec3 sampleCascadeAtlas_Advanced(sampler2D cascadeAtlases[5], int level, int chartID, 
                                  vec3 probeCoord, const int cascadeResolutions[5]) {
    if (level < 0 || level >= 5) return vec3(0.0);
    if (chartID < 1 || chartID > 18) return vec3(0.0);
    
    vec2 uv = probeCoordToAtlasUV_Advanced(chartID, probeCoord, level, cascadeResolutions);
    vec4 sample = texture(cascadeAtlases[level], uv);
    
    return sample.rgb;
}
