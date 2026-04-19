with open('src/demo3d.cpp', 'a', encoding='utf-8') as f:
    f.write('''
bool Demo3D::loadOBJMesh(const std::string& filename) {
    std::cout << "\\n[Demo3D] Loading OBJ mesh: " << filename << std::endl;
    
    if (!objLoader.load(filename)) {
        std::cerr << "[ERROR] Failed to load OBJ!" << std::endl;
        return false;
    }
    
    objLoader.normalize();
    
    std::vector<uint8_t> voxelData;
    objLoader.voxelize(volumeResolution, voxelData, volumeOrigin, volumeSize);
    
    if (voxelData.empty()) {
        std::cerr << "[ERROR] Empty voxelization!" << std::endl;
        return false;
    }
    
    glBindTexture(GL_TEXTURE_3D, voxelGridTexture);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0,
                    volumeResolution, volumeResolution, volumeResolution,
                    GL_RGBA, GL_UNSIGNED_BYTE, voxelData.data());
    
    sceneDirty = true;
    useOBJMesh = true;
    
    std::cout << "[Demo3D] OBJ mesh loaded and voxelized successfully!" << std::endl;
    return true;
}
''')
print("Function appended successfully!")
