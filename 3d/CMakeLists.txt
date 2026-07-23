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
    src/app3d.cpp
    src/reference_cornell_scene.cpp
    src/reference_layout.cpp
    src/reference_layout_validation.cpp
    src/reference_rc_atlases.cpp
    src/reference_merge_validation.cpp
    src/reference_feedback_validation.cpp
    src/reference_transport.cpp
    src/reference_transport_validation.cpp
    src/demo3d.cpp
    src/surface_rc.cpp
    src/validation_helpers.cpp
    src/gl_helpers.cpp
    src/analytic_sdf.cpp  # Analytic SDF primitives for Phase 0
    src/rdoc_helper.cpp   # Phase 6b: RenderDoc DLL loading (isolated from raylib.h)
    src/exr_writer.cpp    # HDR EXR sidecar writer, tinyexr isolated from raylib.h
    lib/tinyexr/miniz.c   # tinyexr compression dependency
)

set(HEADERS_3D
    src/app3d.h
    src/legacy_demo3d_runtime.h
    src/reference_cornell_scene.h
    src/reference_layout.h
    src/reference_layout_validation.h
    src/reference_rc_atlases.h
    src/reference_merge_validation.h
    src/reference_feedback_validation.h
    src/reference_transport.h
    src/reference_transport_validation.h
    src/demo3d.h
    src/surface_rc.h
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
    "${PROJECT_SOURCE_DIR}/lib/tinyexr"
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
    RC3D_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
    RC3D_SHADER_ROOT="${PROJECT_SOURCE_DIR}/res/shaders"
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

# miniz.c is third-party amalgamated code; suppress its warnings so project
# warnings remain useful.
if(MSVC)
    set_source_files_properties(lib/tinyexr/miniz.c PROPERTIES COMPILE_FLAGS "/W0")
else()
    set_source_files_properties(lib/tinyexr/miniz.c PROPERTIES COMPILE_FLAGS "-w")
endif()

# Developer and validation runs load shaders directly from RC3D_SHADER_ROOT.
# Keep a synchronized build-tree copy for packaging and manual inspection, but
# never mix the parent 2D shaders into the 3D runtime namespace.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${PROJECT_BINARY_DIR}/res/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/res"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/res/shaders"
            "${PROJECT_BINARY_DIR}/res/shaders"
    COMMENT "Synchronizing 3D shader resources"
)

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
