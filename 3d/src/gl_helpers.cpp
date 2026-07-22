/**
 * @file gl_helpers.cpp
 * @brief OpenGL helper utilities implementation for 3D Radiance Cascades
 * 
 * This file implements wrapper functions for OpenGL 3D texture management,
 * framebuffer objects, compute shaders, and query operations.
 */

#include "gl_helpers.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace gl {

namespace {

std::string shaderRoot;
std::vector<ShaderSourceRecord> shaderSourceRecords;
uint64_t debugErrorCount = 0;

constexpr std::array<uint32_t, 64> kSha256K = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

uint32_t rotateRight(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32u - bits));
}

std::string sha256Bytes(std::vector<uint8_t> bytes) {
    const uint64_t bitLength = static_cast<uint64_t>(bytes.size()) * 8u;
    bytes.push_back(0x80u);
    while ((bytes.size() % 64u) != 56u)
        bytes.push_back(0u);
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<uint8_t>((bitLength >> shift) & 0xffu));

    std::array<uint32_t, 8> hash = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };

    for (size_t offset = 0; offset < bytes.size(); offset += 64u) {
        std::array<uint32_t, 64> words{};
        for (size_t i = 0; i < 16u; ++i) {
            const size_t p = offset + i * 4u;
            words[i] = (uint32_t(bytes[p]) << 24u) | (uint32_t(bytes[p + 1]) << 16u) |
                       (uint32_t(bytes[p + 2]) << 8u) | uint32_t(bytes[p + 3]);
        }
        for (size_t i = 16u; i < words.size(); ++i) {
            const uint32_t s0 = rotateRight(words[i - 15], 7u) ^ rotateRight(words[i - 15], 18u) ^ (words[i - 15] >> 3u);
            const uint32_t s1 = rotateRight(words[i - 2], 17u) ^ rotateRight(words[i - 2], 19u) ^ (words[i - 2] >> 10u);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
        uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];
        for (size_t i = 0; i < words.size(); ++i) {
            const uint32_t s1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
            const uint32_t choose = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + choose + kSha256K[i] + words[i];
            const uint32_t s0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
        hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (uint32_t value : hash)
        out << std::setw(8) << value;
    return out.str();
}

std::string shaderInfoLog(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::vector<GLchar> log(static_cast<size_t>(std::max(length, 1)));
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    return std::string(log.data());
}

std::string programInfoLog(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::vector<GLchar> log(static_cast<size_t>(std::max(length, 1)));
    glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
    return std::string(log.data());
}

void recordShaderSource(const std::string& logicalName, const std::string& path,
                        const std::string& hash, bool compiled) {
    shaderSourceRecords.push_back({logicalName, path, hash, compiled});
}

}  // namespace

void setShaderRoot(const std::string& root) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(root, ec);
    shaderRoot = (ec ? std::filesystem::path(root) : canonical).lexically_normal().string();
}

const std::string& getShaderRoot() {
    return shaderRoot;
}

std::string resolveShaderPath(const std::string& shaderName) {
    return (std::filesystem::path(shaderRoot) / shaderName).lexically_normal().string();
}

std::string sha256File(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
        return {};
    return sha256Bytes(std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                            std::istreambuf_iterator<char>()));
}

void clearShaderSourceRecords() {
    shaderSourceRecords.clear();
}

const std::vector<ShaderSourceRecord>& getShaderSourceRecords() {
    return shaderSourceRecords;
}

void labelObject(GLenum identifier, GLuint object, const std::string& label) {
    if (object != 0 && GLEW_KHR_debug && glObjectLabel)
        glObjectLabel(identifier, object, -1, label.c_str());
}

void pushDebugGroup(const char* label) {
    if (GLEW_KHR_debug && glPushDebugGroup)
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, label);
}

void popDebugGroup() {
    if (GLEW_KHR_debug && glPopDebugGroup)
        glPopDebugGroup();
}

bool validateProgram(GLuint program, const std::string& label) {
    glValidateProgram(program);
    GLint valid = GL_FALSE;
    glGetProgramiv(program, GL_VALIDATE_STATUS, &valid);
    if (valid == GL_TRUE)
        return true;
    std::cerr << "[GL Error] Program validation failed (" << label
              << "):\n" << programInfoLog(program) << std::endl;
    return false;
}

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

GLuint loadComputeShader(const std::string& filepath, const std::string& logicalName) {
    // Read shader source
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[GL Error] Cannot open compute shader: " << filepath << std::endl;
        recordShaderSource(logicalName.empty() ? filepath : logicalName, filepath, {}, false);
        return 0;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    const std::string hash = sha256Bytes(std::vector<uint8_t>(source.begin(), source.end()));
    const GLuint program = createComputeProgram(source, filepath);
    recordShaderSource(logicalName.empty() ? filepath : logicalName, filepath, hash, program != 0);
    return program;
}

GLuint createComputeProgram(const std::string& shaderSource, const std::string& sourceLabel) {
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
        std::cerr << "[GL Error] Compute shader compilation failed (" << sourceLabel
                  << "):\n" << shaderInfoLog(shader) << std::endl;
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
        std::cerr << "[GL Error] Compute shader linking failed (" << sourceLabel
                  << "):\n" << programInfoLog(program) << std::endl;
        glDeleteShader(shader);
        glDeleteProgram(program);
        return 0;
    }
    
    // Clean up shader object (it's linked into program now)
    glDetachShader(program, shader);
    glDeleteShader(shader);
    
    return program;
}

GLuint compileShader(GLenum type, const std::string& filepath, const std::string& logicalName) {
    /**
     * @brief Compile a single shader stage from file
     */
    
    // Read shader source
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[GL Error] Cannot open shader file: " << filepath << std::endl;
        recordShaderSource(logicalName.empty() ? filepath : logicalName, filepath, {}, false);
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
        std::cerr << "[GL Error] Shader compilation failed (" << filepath
                  << "):\n" << shaderInfoLog(shader) << std::endl;
        glDeleteShader(shader);
        recordShaderSource(logicalName.empty() ? filepath : logicalName, filepath,
                           sha256Bytes(std::vector<uint8_t>(source.begin(), source.end())), false);
        return 0;
    }

    recordShaderSource(logicalName.empty() ? filepath : logicalName, filepath,
                       sha256Bytes(std::vector<uint8_t>(source.begin(), source.end())), true);
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

void noteDebugError() {
    ++debugErrorCount;
}

uint64_t getDebugErrorCount() {
    return debugErrorCount;
}

bool checkGLError(const char* file, int line) {
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        noteDebugError();
        std::cerr << "[GL Error] " << file << ":" << line 
                  << " - Code: " << error << std::endl;
        return true;
    }
    return false;
}

} // namespace gl
