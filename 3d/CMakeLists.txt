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
# Usage:
#   cd 3d
#   mkdir build && cd build
#   cmake ..
#   make
#   ./radiance_cascades_3d
#
# =============================================================================

cmake_minimum_required(VERSION 3.25)
project(RadianceCascades3D VERSION 0.1.0 LANGUAGES CXX)

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
# Find Dependencies
# =============================================================================

# OpenGL (required: 4.3+ for compute shaders)
find_package(OpenGL REQUIRED)

# GLEW for extension loading
find_package(GLEW REQUIRED)

# GLM for math
find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "GLM not found as system package, will use bundled if available")
endif()

# =============================================================================
# Source Files
# =============================================================================

set(SOURCES_3D
    src/main3d.cpp
    src/demo3d.cpp
    src/gl_helpers.cpp
    src/analytic_sdf.cpp  # Analytic SDF primitives for Phase 0
    src/rdoc_helper.cpp   # Phase 6b: RenderDoc DLL loading (isolated from raylib.h)
)

set(HEADERS_3D
    src/demo3d.h
    src/analytic_sdf.h  # Analytic SDF header
    include/gl_helpers.h
)

# =============================================================================
# Executable Definition
# =============================================================================

add_executable(${PROJECT_NAME} ${SOURCES_3D} ${HEADERS_3D})

# =============================================================================
# Include Directories
# =============================================================================

target_include_directories(${PROJECT_NAME} PRIVATE
    "${PROJECT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/../lib/rlImGui"
    "${PROJECT_SOURCE_DIR}/../lib/imgui"
    "${PROJECT_SOURCE_DIR}/../lib/raylib/src"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/lib/renderdoc"
    ${OPENGL_INCLUDE_DIR}
    ${GLEW_INCLUDE_DIRS}
)

# =============================================================================
# Link Libraries
# =============================================================================

# First build raylib and dependencies from parent
add_subdirectory(../lib lib)

target_link_libraries(${PROJECT_NAME}
    raylib
    rlImGui
    ImGui
    ${OPENGL_LIBRARIES}
    GLEW::GLEW
)

# Link GLM if found
if(glm_FOUND)
    target_link_libraries(${PROJECT_NAME} glm::glm)
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
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME}
        dl
        pthread
    )
endif()

# =============================================================================
# Compile Definitions
# =============================================================================

target_compile_definitions(${PROJECT_NAME} PRIVATE
    VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    VERSION_MINOR=${PROJECT_VERSION_MINOR}
    VERSION_PATCH=${PROJECT_VERSION_PATCH}
    BUILD_3D_VERSION
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Debug>:GL_DEBUG>
)

# =============================================================================
# Configure config.h
# =============================================================================

configure_file(src/config.h.in "${PROJECT_BINARY_DIR}/config.h" @ONLY)

# =============================================================================
# Copy Shaders and Resources
# =============================================================================

# Copy shaders to build directory
file(COPY ${PROJECT_SOURCE_DIR}/res/shaders 
     DESTINATION ${PROJECT_BINARY_DIR}/res)

# Also copy from parent res directory if needed
if(EXISTS ${PROJECT_SOURCE_DIR}/../res/shaders)
    file(COPY ${PROJECT_SOURCE_DIR}/../res/shaders 
         DESTINATION ${PROJECT_BINARY_DIR}/res)
endif()

# =============================================================================
# Build Configuration
# =============================================================================

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${PROJECT_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${PROJECT_BINARY_DIR}
)

# =============================================================================
# Installation
# =============================================================================

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/res/
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
message(STATUS "Install Prefix:       ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
