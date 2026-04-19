import re

# Read the file
with open('src/demo3d.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# Define old function pattern
old_pattern = r'void RadianceCascade3D::destroy\(\) \{.*?// TODO: Implement resource cleanup\n\}'

# New implementation
new_function = '''void RadianceCascade3D::destroy() {
    /**
     * @brief Release OpenGL resources
     * 
     * Implementation Steps:
     * 1. Delete probe grid texture with glDeleteTextures()
     * 2. Reset state to default
     */
    
    if (probeGridTexture != 0) {
        glDeleteTextures(1, &probeGridTexture);
        probeGridTexture = 0;
    }
    
    // Reset configuration
    resolution = 0;
    cellSize = 0.0f;
    origin = glm::vec3(0.0f);
    raysPerProbe = 0;
    intervalStart = 0.0f;
    intervalEnd = 0.0f;
    active = false;
}'''

# Replace
content_new = re.sub(old_pattern, new_function, content, flags=re.DOTALL)

# Write back
with open('src/demo3d.cpp', 'w', encoding='utf-8') as f:
    f.write(content_new)

print('Destroy function updated!')
