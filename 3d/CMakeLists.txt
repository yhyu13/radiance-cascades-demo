# =============================================================================
# CMakeLists.txt - 3D Radiance Cascades Implementation
# =============================================================================
# 
# Purpose: Build configuration for 3D Radiance Cascades demo
# 
# This CMakeLists.txt builds a separate 3D version alongside the existing 2D
# implementation. The 3D version uses compute shaders and volume textures
# for true volumetric global illumination.
#
# Key Differences from 2D Version:
# - Uses OpenGL 4.3+ features (compute shaders, 3D textures)
# - Requires GL_ARB_compute_shader and GL_ARB_shader_image_load_store
# - Volume rendering with raymarching
# - Sparse voxel octree data structure
#
# Usage:
#   mkdir build && cd build
#   cmake .. -DBUILD_3D_VERSION=ON
#   make
#   ./radiance_cascades_3d
#
# =============================================================================

cmake_minimum_required(VERSION 3.25)
project(RadianceCascades3D VERSION 1.0 LANGUAGES CXX)

# =============================================================================
# Build Options
# =============================================================================

option(BUILD_3D_VERSION "Build 3D version instead of 2D" OFF)
option(ENABLE_COMPUTE_SHADERS "Use compute shaders for 3D processing" ON)
option(USE_SPARSE_VOXELS "Enable sparse voxel octree support" ON)
option(TEMPORAL_REPROJECTION "Enable temporal accumulation for performance" ON)
option(DEBUG_VISUALIZATION "Enable debug visualization tools" ON)

# =============================================================================
# C++ Standard Configuration
# =============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# =============================================================================
# Source Files - 3D Version
# =============================================================================

set(SOURCES_3D
    # Main application
    src/main3d.cpp
    
    # Demo class
    src/demo3d.cpp
    src/demo3d.h
    
    # OpenGL helpers for 3D textures
    include/gl_helpers.h
)

# =============================================================================
# Find Dependencies
# =============================================================================

# OpenGL (required: 4.3+ for compute shaders)
find_package(OpenGL REQUIRED)

# Check for minimum OpenGL version
if(NOT OPENGL_VERSION_STRING VERSION_GREATER_EQUAL "4.3")
    message(WARNING "OpenGL 4.3+ recommended for compute shader support")
endif()

# GLFW (window management, already in raylib)
# We'll use raylib's window management like the 2D version

# =============================================================================
# Include Directories
# =============================================================================

include_directories(
    ${CMAKE_SOURCE_DIR}/../src
    ${CMAKE_SOURCE_DIR}/../lib/imgui
    ${CMAKE_SOURCE_DIR}/../lib/raylib/src
    ${CMAKE_SOURCE_DIR}/../lib/rlImGui
    ${CMAKE_SOURCE_DIR}/include
    ${OPENGL_INCLUDE_DIR}
)

# =============================================================================
# Executable Definition
# =============================================================================

add_executable(${PROJECT_NAME}_3D ${SOURCES_3D})

# Link libraries
target_link_libraries(${PROJECT_NAME}_3D
    ${OPENGL_LIBRARIES}
    raylib
    imgui
    rlImGui
)

# Platform-specific libraries
if(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(OPENGL_FRAMEWORK OpenGL)
    target_link_libraries(${PROJECT_NAME}_3D
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${OPENGL_FRAMEWORK}
    )
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME}_3D
        dl
        pthread
    )
endif()

# =============================================================================
# Compile Definitions
# =============================================================================

target_compile_definitions(${PROJECT_NAME}_3D PRIVATE
    VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    VERSION_MINOR=${PROJECT_VERSION_MINOR}
    VERSION_PATCH=${PROJECT_VERSION_PATCH}
    BUILD_3D_VERSION
    $<$<BOOL:${ENABLE_COMPUTE_SHADERS}>:ENABLE_COMPUTE_SHADERS>
    $<$<BOOL:${USE_SPARSE_VOXELS}>:USE_SPARSE_VOXELS>
    $<$<BOOL:${TEMPORAL_REPROJECTION}>:TEMPORAL_REPROJECTION>
    $<$<BOOL:${DEBUG_VISUALIZATION}>:DEBUG_VISUALIZATION>
)

# =============================================================================
# Shader Compilation
# =============================================================================

# Copy shaders to build directory (no compilation needed for GLSL)
file(GLOB SHADER_FILES "${CMAKE_SOURCE_DIR}/res/shaders/*.comp" 
                        "${CMAKE_SOURCE_DIR}/res/shaders/*.frag"
                        "${CMAKE_SOURCE_DIR}/res/shaders/*.vert")

foreach(SHADER ${SHADER_FILES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    configure_file(${SHADER} ${CMAKE_BINARY_DIR}/res/shaders/${SHADER_NAME} COPYONLY)
endforeach()

# =============================================================================
# Resource Files
# =============================================================================

# Copy resource files to build directory
#file(COPY ${CMAKE_SOURCE_DIR}/res/textures DESTINATION ${CMAKE_BINARY_DIR}/res)

# =============================================================================
# Build Configuration
# =============================================================================

# Set output directory
set_target_properties(${PROJECT_NAME}_3D PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
)

# Enable hot reloading of shaders
if(MSVC)
    add_custom_command(TARGET ${PROJECT_NAME}_3D POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/res/shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}_3D>/res/shaders"
    )
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS ${PROJECT_NAME}_3D
    RUNTIME DESTINATION bin
)

install(DIRECTORY res/
    DESTINATION bin/res
    FILES_MATCHING PATTERN "*"
)

# =============================================================================
# Configuration Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== Radiance Cascades 3D Configuration ===")
message(STATUS "Version:              ${PROJECT_VERSION}")
message(STATUS "Build Type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:         ${CMAKE_CXX_STANDARD}")
message(STATUS "OpenGL Version:       ${OPENGL_VERSION_STRING}")
message(STATUS "Compute Shaders:      ${ENABLE_COMPUTE_SHADERS}")
message(STATUS "Sparse Voxels:        ${USE_SPARSE_VOXELS}")
message(STATUS "Temporal Reprojection: ${TEMPORAL_REPROJECTION}")
message(STATUS "Debug Visualization:  ${DEBUG_VISUALIZATION}")
message(STATUS "Install Prefix:       ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")

# CMakeLists.txt for 3D Radiance Cascades
# This builds the experimental 3D volumetric global illumination demo

cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED NO)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Enable OpenGL and compute shaders
project(radiance_cascades_3d VERSION 0.1.0)

configure_file(src/config.h.in config.h)

# Find required packages
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

# Source files for 3D version
set(SOURCES_3D
    src/main3d.cpp
    src/demo3d.cpp
    src/gl_helpers.cpp
)

# Header files
set(HEADERS_3D
    src/demo3d.h
    include/gl_helpers.h
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES_3D} ${HEADERS_3D})

# Add subdirectories for dependencies
add_subdirectory(../lib lib)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    "${PROJECT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/../lib/rlImGui"
    "${PROJECT_SOURCE_DIR}/../lib/imgui"
    "${PROJECT_SOURCE_DIR}/include"
    "${OPENGL_INCLUDE_DIR}"
    "${GLEW_INCLUDE_DIRS}"
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    raylib
    rlImGui
    ImGui
    ${OPENGL_LIBRARIES}
    GLEW::GLEW
)

# Link GLM (header-only, but may need include path)
find_package(glm QUIET)
if(glm_FOUND)
    target_link_libraries(${PROJECT_NAME} glm::glm)
else()
    # If GLM not installed as package, use local copy or download
    message(STATUS "GLM not found as system package, using bundled version")
    # You may need to add GLM to your project or install it
endif()

# Platform-specific linking
if(APPLE)
    target_link_libraries(${PROJECT_NAME} 
        "-framework IOKit"
        "-framework Cocoa"
        "-framework OpenGL"
    )
elseif(WIN32)
    target_link_libraries(${PROJECT_NAME}
        opengl32
        glu32
    )
endif()

# Copy shaders to build directory
# This ensures shaders are available when running from build directory
# Shaders are now in 3d/res/shaders/ instead of res/shaders/
file(COPY ${PROJECT_SOURCE_DIR}/../res/shaders DESTINATION ${PROJECT_BINARY_DIR}/res)
file(COPY ${PROJECT_SOURCE_DIR}/res/shaders DESTINATION ${PROJECT_BINARY_DIR}/3d/res)

# Set output directory
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
)

# Compiler warnings
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Debug definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Debug>:GL_DEBUG>
)

# Installation rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/../res/shaders
    DESTINATION ${CMAKE_INSTALL_PREFIX}/res
)
