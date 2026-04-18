/**
 * @file gl_helpers.cpp
 * @brief OpenGL helper utilities implementation for 3D Radiance Cascades
 * 
 * This file implements wrapper functions for OpenGL 3D texture management,
 * framebuffer objects, compute shaders, and query operations.
 */

#include "gl_helpers.h"
#include <iostream>
#include <fstream>
#include <sstream>

namespace gl {

// =============================================================================
// 3D Texture Management
// =============================================================================

GLuint createTexture3D(
    GLsizei width,
    GLsizei height,
    GLsizei depth,
    GLint internalFormat,
    GLenum format,
    GLenum type,
    const void* data
) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_3D, texture);
    
    // Allocate storage
    glTexImage3D(
        GL_TEXTURE_3D,
        0,                  // Mipmap level
        internalFormat,
        width, height, depth,
        0,                  // Border (must be 0)
        format,
        type,
        data
    );
    
    // Set default parameters
    setTexture3DParameters(texture);
    
    glBindTexture(GL_TEXTURE_3D, 0);
    return texture;
}

void updateTexture3DSubregion(
    GLuint texture,
    GLint xOffset, GLint yOffset, GLint zOffset,
    GLsizei width, GLsizei height, GLsizei depth,
    GLenum format,
    GLenum type,
    const void* data
) {
    glBindTexture(GL_TEXTURE_3D, texture);
    
    glTexSubImage3D(
        GL_TEXTURE_3D,
        0,                  // Mipmap level
        xOffset, yOffset, zOffset,
        width, height, depth,
        format,
        type,
        data
    );
    
    glBindTexture(GL_TEXTURE_3D, 0);
}

void setTexture3DParameters(
    GLuint texture,
    GLenum minFilter,
    GLenum magFilter,
    GLenum wrapMode
) {
    glBindTexture(GL_TEXTURE_3D, texture);
    
    // Filtering
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, magFilter);
    
    // Wrap modes (all three axes)
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, wrapMode);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, wrapMode);
    
    glBindTexture(GL_TEXTURE_3D, 0);
}

// =============================================================================
// Framebuffer Objects
// =============================================================================

GLuint createFramebuffer3D(GLuint texture, GLint mipLevel, GLint zSlice) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    // Attach 3D texture slice
    glFramebufferTexture3D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_3D,
        texture,
        mipLevel,
        zSlice
    );
    
    // Check completeness
    if (!checkFramebufferComplete(fbo)) {
        std::cerr << "[GL Error] Framebuffer incomplete!" << std::endl;
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

GLuint createFramebufferWithAttachments(
    const std::vector<GLuint>& textures,
    size_t count
) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    // Attach multiple textures for MRT
    std::vector<GLenum> attachments;
    for (size_t i = 0; i < count; ++i) {
        glFramebufferTexture3D(
            GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0 + i,
            GL_TEXTURE_3D,
            textures[i],
            0,
            0
        );
        attachments.push_back(GL_COLOR_ATTACHMENT0 + i);
    }
    
    // Set draw buffers
    glDrawBuffers(count, attachments.data());
    
    if (!checkFramebufferComplete(fbo)) {
        std::cerr << "[GL Error] Framebuffer with MRT incomplete!" << std::endl;
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return fbo;
}

bool checkFramebufferComplete(GLuint framebuffer) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[GL Error] Framebuffer status: ";
        switch (status) {
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                std::cerr << "Incomplete attachment" << std::endl;
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                std::cerr << "Missing attachment" << std::endl;
                break;
            case GL_FRAMEBUFFER_UNSUPPORTED:
                std::cerr << "Unsupported format" << std::endl;
                break;
            default:
                std::cerr << "Unknown error (" << status << ")" << std::endl;
        }
        return false;
    }
    
    return true;
}

// =============================================================================
// Compute Shaders
// =============================================================================

GLuint loadComputeShader(const std::string& filepath) {
    // Read shader source
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[GL Error] Cannot open compute shader: " << filepath << std::endl;
        return 0;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    return createComputeProgram(source);
}

GLuint createComputeProgram(const std::string& shaderSource) {
    // Create shader object
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* src = shaderSource.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    
    // Compile
    glCompileShader(shader);
    
    // Check compilation status
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "[GL Error] Compute shader compilation failed:\n" << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    
    // Create program
    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    
    // Check link status
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "[GL Error] Compute shader linking failed:\n" << log << std::endl;
        glDeleteShader(shader);
        glDeleteProgram(program);
        return 0;
    }
    
    // Clean up shader object (it's linked into program now)
    glDetachShader(program, shader);
    glDeleteShader(shader);
    
    return program;
}

GLuint compileShader(GLenum type, const std::string& filepath) {
    /**
     * @brief Compile a single shader stage from file
     */
    
    // Read shader source
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[GL Error] Cannot open shader file: " << filepath << std::endl;
        return 0;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    // Create shader object
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    
    // Compile
    glCompileShader(shader);
    
    // Check compilation status
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[GL Error] Shader compilation failed (" << filepath << "):\n" << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

void dispatchComputeShader(
    GLuint program,
    GLuint workGroupsX,
    GLuint workGroupsY,
    GLuint workGroupsZ
) {
    glUseProgram(program);
    glDispatchCompute(workGroupsX, workGroupsY, workGroupsZ);
    glUseProgram(0);
}

void dispatchComputeAuto(
    GLuint program,
    GLuint globalSizeX,
    GLuint globalSizeY,
    GLuint globalSizeZ
) {
    // Query local_size from shader
    GLint localSizeX, localSizeY, localSizeZ;
    
    glGetProgramiv(program, GL_COMPUTE_WORK_GROUP_SIZE, &localSizeX);
    glGetProgramiv(program, GL_COMPUTE_WORK_GROUP_SIZE, &localSizeY);
    glGetProgramiv(program, GL_COMPUTE_WORK_GROUP_SIZE, &localSizeZ);
    
    // Calculate work group count
    GLuint workGroupsX = (globalSizeX + localSizeX - 1) / localSizeX;
    GLuint workGroupsY = (globalSizeY + localSizeY - 1) / localSizeY;
    GLuint workGroupsZ = (globalSizeZ + localSizeZ - 1) / localSizeZ;
    
    dispatchComputeShader(program, workGroupsX, workGroupsY, workGroupsZ);
}

// =============================================================================
// Image Load/Store
// =============================================================================

void bindImageTexture(
    GLuint unit,
    GLuint texture,
    GLint level,
    GLboolean layered,
    GLint layer,
    GLenum access,
    GLenum format
) {
    glBindImageTexture(unit, texture, level, layered, layer, access, format);
}

void memoryBarrier(GLbitfield barriers) {
    glMemoryBarrier(barriers);
}

// =============================================================================
// Buffer Objects
// =============================================================================

GLuint createShaderStorageBuffer(GLsizeiptr size, const void* data, GLenum usage) {
    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, usage);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return buffer;
}

void bindShaderStorageBuffer(GLuint bindingIndex, GLuint buffer) {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingIndex, buffer);
}

// =============================================================================
// Query Objects
// =============================================================================

GLuint createTimeQuery() {
    GLuint query;
    glGenQueries(1, &query);
    return query;
}

void beginTimeQuery(GLuint query) {
    glBeginQuery(GL_TIME_ELAPSED, query);
}

uint64_t endTimeQuery(GLuint query) {
    glEndQuery(GL_TIME_ELAPSED);
    
    uint64_t time;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &time);
    return time;
}

// =============================================================================
// Debug & Utilities
// =============================================================================

void enableDebugOutput() {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    
    glDebugMessageCallback([](
        GLenum source,
        GLenum type,
        GLuint id,
        GLenum severity,
        GLsizei length,
        const GLchar* message,
        const void* userParam
    ) {
        std::cerr << "[GL Debug] ";
        
        switch (severity) {
            case GL_DEBUG_SEVERITY_HIGH:
                std::cerr << "[HIGH] ";
                break;
            case GL_DEBUG_SEVERITY_MEDIUM:
                std::cerr << "[MEDIUM] ";
                break;
            case GL_DEBUG_SEVERITY_LOW:
                std::cerr << "[LOW] ";
                break;
            case GL_DEBUG_SEVERITY_NOTIFICATION:
                std::cerr << "[NOTIFY] ";
                break;
        }
        
        std::cerr << message << std::endl;
    }, nullptr);
}

std::string getEnumString(GLenum enumValue) {
    // Simplified - full implementation would have complete mapping
    switch (enumValue) {
        case GL_TEXTURE_3D: return "GL_TEXTURE_3D";
        case GL_COMPUTE_SHADER: return "GL_COMPUTE_SHADER";
        case GL_SHADER_IMAGE_ACCESS_BARRIER_BIT: return "GL_SHADER_IMAGE_ACCESS_BARRIER_BIT";
        default: return "Unknown";
    }
}

bool checkGLError(const char* file, int line) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::cerr << "[GL Error] " << file << ":" << line 
                  << " - Code: " << error << std::endl;
        return true;
    }
    return false;
}

} // namespace gl
