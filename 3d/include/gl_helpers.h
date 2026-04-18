/**
 * @file gl_helpers.h
 * @brief OpenGL helper utilities for 3D Radiance Cascades
 * 
 * This header provides wrapper functions for OpenGL 3D texture management,
 * framebuffer objects, and compute shader dispatch. These helpers abstract
 * the low-level OpenGL calls needed for volume rendering.
 * 
 * Requirements:
 * - OpenGL 4.3+ (for compute shaders)
 * - GL_ARB_compute_shader
 * - GL_ARB_shader_image_load_store
 * - GL_ARB_texture_view
 */

#ifndef GL_HELPERS_H
#define GL_HELPERS_H

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace gl {

// =============================================================================
// 3D Texture Management
// =============================================================================

/**
 * @brief Create a 3D volume texture with specified format
 * 
 * Creates an empty 3D texture suitable for storing voxel data, distance fields,
 * or radiance information. Uses GL_TEXTURE_3D target with appropriate filtering.
 * 
 * @param width Volume width in voxels
 * @param height Volume height in voxels
 * @param depth Volume depth in voxels
 * @param internalFormat Internal storage format (e.g., GL_RGBA16F, GL_R32F)
 * @param format Pixel format (e.g., GL_RGBA, GL_RED)
 * @param type Data type (e.g., GL_FLOAT, GL_UNSIGNED_BYTE)
 * @param data Initial texture data (nullptr for empty texture)
 * @return GLuint Texture object ID
 * 
 * @note Common format combinations:
 *   - Voxel grid: GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE
 *   - SDF: GL_R32F, GL_RED, GL_FLOAT
 *   - Radiance: GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT
 */
GLuint createTexture3D(
    GLsizei width,
    GLsizei height,
    GLsizei depth,
    GLint internalFormat,
    GLenum format,
    GLenum type,
    const void* data = nullptr
);

/**
 * @brief Update a subregion of a 3D texture
 * 
 * Uploads data to a specific region of an existing 3D texture. Useful for
 * dynamic voxel updates or streaming data.
 * 
 * @param texture Texture object ID
 * @param xOffset X offset in voxels
 * @param yOffset Y offset in voxels
 * @param zOffset Z offset in voxels
 * @param width Region width
 * @param height Region height
 * @param depth Region depth
 * @param format Pixel format
 * @param type Data type
 * @param data Pointer to source data
 */
void updateTexture3DSubregion(
    GLuint texture,
    GLint xOffset, GLint yOffset, GLint zOffset,
    GLsizei width, GLsizei height, GLsizei depth,
    GLenum format,
    GLenum type,
    const void* data
);

/**
 * @brief Set texture parameters for 3D volume texture
 * 
 * Configures filtering and wrap modes optimized for volume rendering.
 * 
 * @param texture Texture object ID
 * @param minFilter Minification filter (default: GL_LINEAR)
 * @param magFilter Magnification filter (default: GL_LINEAR)
 * @param wrapMode Wrap mode for all axes (default: GL_CLAMP_TO_EDGE)
 */
void setTexture3DParameters(
    GLuint texture,
    GLenum minFilter = GL_LINEAR,
    GLenum magFilter = GL_LINEAR,
    GLenum wrapMode = GL_CLAMP_TO_EDGE
);

// =============================================================================
// Framebuffer Objects (FBO)
// =============================================================================

/**
 * @brief Create a framebuffer object with 3D texture attachment
 * 
 * Creates an FBO with a 3D texture attached as color buffer. Used for
 * off-screen rendering to volume textures.
 * 
 * @param texture 3D texture to attach
 * @param mipLevel Mipmap level (default: 0)
 * @param zSlice Z slice to attach (for layer rendering, default: 0)
 * @return GLuint Framebuffer object ID
 * 
 * @note For full 3D rendering, use compute shaders instead. FBOs are limited
 *       to 2D rendering and can only attach single Z slices.
 */
GLuint createFramebuffer3D(GLuint texture, GLint mipLevel = 0, GLint zSlice = 0);

/**
 * @brief Create framebuffer with multiple 3D texture attachments
 * 
 * Supports MRT (Multiple Render Targets) for deferred rendering pipelines.
 * 
 * @param textures Array of 3D texture IDs
 * @param count Number of textures
 * @return GLuint Framebuffer object ID
 */
GLuint createFramebufferWithAttachments(const std::vector<GLuint>& textures, size_t count);

/**
 * @brief Check framebuffer completeness
 * 
 * Validates that framebuffer is properly configured and ready for rendering.
 * 
 * @param framebuffer Framebuffer object ID
 * @return true if complete, false otherwise
 */
bool checkFramebufferComplete(GLuint framebuffer);

// =============================================================================
// Compute Shader Helpers
// =============================================================================

/**
 * @brief Load and compile a compute shader from file
 * 
 * Reads GLSL compute shader source, compiles it, and returns shader object.
 * Includes error checking and compilation log output.
 * 
 * @param filepath Path to .comp shader file
 * @return GLuint Shader object ID (0 on failure)
 * 
 * @example
 *   GLuint shader = loadComputeShader("res/shaders/voxelize.comp");
 *   if (shader == 0) { /* handle error *\/ }
 */
GLuint loadComputeShader(const std::string& filepath);

/**
 * @brief Create compute shader program from source
 * 
 * Links compute shader into a complete program ready for dispatch.
 * 
 * @param shaderSource GLSL source code
 * @return GLuint Program object ID (0 on failure)
 */
GLuint createComputeProgram(const std::string& shaderSource);

/**
 * @brief Compile a single shader stage (vertex, fragment, etc.)
 * 
 * Reads GLSL source from file, compiles it, and returns shader object.
 * Used for building multi-stage programs (e.g., vertex + fragment).
 * 
 * @param type Shader type (GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, etc.)
 * @param filepath Path to shader file (.vert, .frag, etc.)
 * @return GLuint Shader object ID (0 on failure)
 */
GLuint compileShader(GLenum type, const std::string& filepath);

/**
 * @brief Dispatch compute shader with work group dimensions
 * 
 * Executes compute shader on GPU with specified number of work groups.
 * Automatically calculates work groups based on local_size from shader.
 * 
 * @param program Compute shader program ID
 * @param workGroupsX Number of work groups in X dimension
 * @param workGroupsY Number of work groups in Y dimension
 * @param workGroupsZ Number of work groups in Z dimension
 * 
 * @note Call glMemoryBarrier() after dispatch if results will be read by
 *       other shader stages.
 */
void dispatchComputeShader(
    GLuint program,
    GLuint workGroupsX,
    GLuint workGroupsY,
    GLuint workGroupsZ
);

/**
 * @brief Dispatch compute shader with automatic work group calculation
 * 
 * Calculates optimal work group count based on problem size and shader's
 * local_size declaration.
 * 
 * @param program Compute shader program ID
 * @param globalSizeX Total work items in X
 * @param globalSizeY Total work items in Y
 * @param globalSizeZ Total work items in Z
 */
void dispatchComputeAuto(
    GLuint program,
    GLuint globalSizeX,
    GLuint globalSizeY,
    GLuint globalSizeZ
);

// =============================================================================
// Image Load/Store (for compute shaders)
// =============================================================================

/**
 * @brief Bind image texture for compute shader access
 * 
 * Binds 3D texture as image for read/write access from compute shaders.
 * 
 * @param unit Image unit (0-7)
 * @param texture Texture object ID
 * @param level Mipmap level
 * @param layered Whether texture is layered (false for 3D)
 * @param layer Layer number (ignored if !layered)
 * @param access Access mode (GL_READ_ONLY, GL_WRITE_ONLY, GL_READ_WRITE)
 * @param format Format qualifier matching shader declaration
 * 
 * @example
 *   // In compute shader: layout(rgba16f, binding = 0) uniform image3D img;
 *   bindImageTexture(0, texture, 0, false, 0, GL_READ_WRITE, GL_RGBA16F);
 */
void bindImageTexture(
    GLuint unit,
    GLuint texture,
    GLint level,
    GLboolean layered,
    GLint layer,
    GLenum access,
    GLenum format
);

/**
 * @brief Issue memory barrier for shader synchronization
 * 
 * Ensures compute shader writes are visible to subsequent shader stages.
 * Must be called after compute shader dispatch before using results.
 * 
 * @param barriers Bitfield specifying which operations to synchronize
 * 
 * @note Common barriers:
 *   - GL_SHADER_IMAGE_ACCESS_BARRIER_BIT (image load/store)
 *   - GL_TEXTURE_FETCH_BARRIER_BIT (texture sampling)
 *   - GL_FRAMEBUFFER_BARRIER_BIT (framebuffer fetch)
 */
void memoryBarrier(GLbitfield barriers);

// =============================================================================
// Buffer Objects
// =============================================================================

/**
 * @brief Create shader storage buffer object (SSBO)
 * 
 * Creates buffer for large data storage accessible by compute shaders.
 * More flexible than UBOs with no size limit.
 * 
 * @param size Buffer size in bytes
 * @param data Initial data (nullptr for unitialized)
 * @param usage Usage pattern (GL_DYNAMIC_DRAW, GL_STATIC_DRAW, etc.)
 * @return GLuint Buffer object ID
 */
GLuint createShaderStorageBuffer(GLsizeiptr size, const void* data, GLenum usage);

/**
 * @brief Bind SSBO to binding point
 * 
 * Makes SSBO accessible to shaders at specified binding point.
 * 
 * @param bindingIndex Binding point index
 * @param buffer Buffer object ID
 */
void bindShaderStorageBuffer(GLuint bindingIndex, GLuint buffer);

// =============================================================================
// Query Objects
// =============================================================================

/**
 * @brief Create timer query object for performance measurement
 * 
 * Measures GPU execution time of rendering commands.
 * 
 * @return GLuint Query object ID
 */
GLuint createTimeQuery();

/**
 * @brief Begin timer query
 * 
 * Starts measuring GPU time.
 * 
 * @param query Query object ID
 */
void beginTimeQuery(GLuint query);

/**
 * @brief End timer query and retrieve result
 * 
 * Stops timing and returns elapsed GPU time in nanoseconds.
 * 
 * @param query Query object ID
 * @return uint64_t Elapsed time in nanoseconds
 */
uint64_t endTimeQuery(GLuint query);

// =============================================================================
// Debug & Utilities
// =============================================================================

/**
 * @brief Enable OpenGL debug output
 * 
 * Sets up debug context with callback for error reporting.
 */
void enableDebugOutput();

/**
 * @brief Get string representation of OpenGL enum
 * 
 * Utility for debugging and logging.
 * 
 * @param enumValue OpenGL enum (e.g., GL_TEXTURE_3D)
 * @return std::string Human-readable name
 */
std::string getEnumString(GLenum enumValue);

/**
 * @brief Check for OpenGL errors
 * 
 * Reports any pending OpenGL errors to console.
 * 
 * @param file Source file name (use __FILE__)
 * @param line Source line number (use __LINE__)
 * @return true if error detected
 */
bool checkGLError(const char* file, int line);

/**
 * @brief Macro for convenient error checking
 * 
 * Usage: GL_CHECK(glSomeFunction(...));
 */
#define GL_CHECK(call) do { \
    call; \
    gl::checkGLError(__FILE__, __LINE__); \
} while(0)

} // namespace gl

#endif // GL_HELPERS_H
