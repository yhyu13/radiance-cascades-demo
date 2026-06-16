#version 430 core

// =============================================================================
// Phase 3A: Chart Classification Helper Functions
// Shared between raymarch.frag and surface_radiance_debug.comp
// =============================================================================

/**
 * @brief Check if point is on box face (within epsilon)
 */
bool isOnBoxFace(vec3 pos, vec3 boxMin, vec3 boxMax, float eps) {
    bool insideX = pos.x >= boxMin.x - eps && pos.x <= boxMax.x + eps;
    bool insideY = pos.y >= boxMin.y - eps && pos.y <= boxMax.y + eps;
    bool insideZ = pos.z >= boxMin.z - eps && pos.z <= boxMax.z + eps;
    
    if (!insideX || !insideY || !insideZ) return false;
    
    float distToSurface = min(
        min(abs(pos.x - boxMin.x), abs(pos.x - boxMax.x)),
        min(abs(pos.y - boxMin.y), abs(pos.y - boxMax.y)),
        min(abs(pos.z - boxMin.z), abs(pos.z - boxMax.z))
    );
    
    return distToSurface < eps;
}

/**
 * @brief Get chart ID for short box face (charts 7-12)
 */
int getShortBoxChartID(vec3 pos, vec3 boxMin, vec3 boxMax) {
    float dx_min = abs(pos.x - boxMin.x);
    float dx_max = abs(pos.x - boxMax.x);
    float dy_min = abs(pos.y - boxMin.y);
    float dy_max = abs(pos.y - boxMax.y);
    float dz_min = abs(pos.z - boxMin.z);
    float dz_max = abs(pos.z - boxMax.z);
    
    float minDist = min(min(dx_min, dx_max), min(min(dy_min, dy_max), min(dz_min, dz_max)));
    
    if (dx_min == minDist) return 7;
    if (dx_max == minDist) return 8;
    if (dy_min == minDist) return 9;
    if (dy_max == minDist) return 10;
    if (dz_min == minDist) return 11;
    if (dz_max == minDist) return 12;
    
    return -1;
}

/**
 * @brief Get chart ID for tall box face (charts 13-18)
 */
int getTallBoxChartID(vec3 pos, vec3 boxMin, vec3 boxMax) {
    float dx_min = abs(pos.x - boxMin.x);
    float dx_max = abs(pos.x - boxMax.x);
    float dy_min = abs(pos.y - boxMin.y);
    float dy_max = abs(pos.y - boxMax.y);
    float dz_min = abs(pos.z - boxMin.z);
    float dz_max = abs(pos.z - boxMax.z);
    
    float minDist = min(min(dx_min, dx_max), min(min(dy_min, dy_max), min(dz_min, dz_max)));
    
    if (dx_min == minDist) return 13;
    if (dx_max == minDist) return 14;
    if (dy_min == minDist) return 15;
    if (dy_max == minDist) return 16;
    if (dz_min == minDist) return 17;
    if (dz_max == minDist) return 18;
    
    return -1;
}

/**
 * @brief Classify hit surface into chart ID with normal alignment check
 */
int classifyHitSurface_Advanced(vec3 hitPos, vec3 normal, 
                                 vec3 roomMin, vec3 roomMax,
                                 vec3 shortBoxMin, vec3 shortBoxMax,
                                 vec3 tallBoxMin, vec3 tallBoxMax,
                                 float planeEps) {
    // Check distance to each room plane
    float distFloor   = abs(hitPos.y - roomMin.y);
    float distCeiling = abs(hitPos.y - roomMax.y);
    float distLeft    = abs(hitPos.x - roomMin.x);
    float distRight   = abs(hitPos.x - roomMax.x);
    float distBack    = abs(hitPos.z - roomMax.z);
    
    // Find nearest plane within epsilon
    float minDist = 1e10;
    int chartID = -1;
    
    if (distFloor < planeEps && distFloor < minDist) {
        minDist = distFloor;
        chartID = 1;
    }
    if (distCeiling < planeEps && distCeiling < minDist) {
        minDist = distCeiling;
        chartID = 2;
    }
    if (distLeft < planeEps && distLeft < minDist) {
        minDist = distLeft;
        chartID = 3;
    }
    if (distRight < planeEps && distRight < minDist) {
        minDist = distRight;
        chartID = 4;
    }
    if (distBack < planeEps && distBack < minDist) {
        minDist = distBack;
        chartID = 5;
    }
    
    // Normal alignment check for grazing angles
    if (chartID > 0) {
        vec3 chartNormal;
        if (chartID == 1) chartNormal = vec3(0.0, 1.0, 0.0);
        else if (chartID == 2) chartNormal = vec3(0.0, -1.0, 0.0);
        else if (chartID == 3) chartNormal = vec3(1.0, 0.0, 0.0);
        else if (chartID == 4) chartNormal = vec3(-1.0, 0.0, 0.0);
        else if (chartID == 5) chartNormal = vec3(0.0, 0.0, -1.0);
        
        if (abs(dot(normal, chartNormal)) < 0.5) {
            chartID = -1;  // Grazing angle, mark as uncertain
        }
    }
    
    // Check box geometry if not classified as room plane
    if (chartID == -1) {
        if (isOnBoxFace(hitPos, shortBoxMin, shortBoxMax, planeEps)) {
            chartID = getShortBoxChartID(hitPos, shortBoxMin, shortBoxMax);
        } else if (isOnBoxFace(hitPos, tallBoxMin, tallBoxMax, planeEps)) {
            chartID = getTallBoxChartID(hitPos, tallBoxMin, tallBoxMax);
        }
    }
    
    return chartID;
}
