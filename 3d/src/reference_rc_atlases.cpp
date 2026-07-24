#include "reference_rc_atlases.h"

#include "gl_helpers.h"
#include "reference_layout.h"

#include <iostream>
#include <vector>

ReferenceRcAtlases::~ReferenceRcAtlases() {
    reset();
}

void ReferenceRcAtlases::reset() noexcept {
    for (auto& pair : pairs_) {
        if (pair.read != 0)
            glDeleteTextures(1, &pair.read);
        if (pair.write != 0)
            glDeleteTextures(1, &pair.write);
        pair.read = 0;
        pair.write = 0;
    }
    valid_ = false;
    historyValid_ = false;
    historyGeneration_ = 0;
}

namespace {
void clearTextureToSentinel(GLuint texture, int width, int height) {
    if (texture == 0)
        return;
    if (GLEW_ARB_clear_texture && glClearTexImage) {
        const float clearValue[4] = {0.0f, 0.0f, 0.0f, -1.0f};
        glClearTexImage(texture, 0, GL_RGBA, GL_FLOAT, clearValue);
    } else {
        std::vector<float> clearData(static_cast<size_t>(width) * height * 4);
        for (size_t i = 3; i < clearData.size(); i += 4)
            clearData[i] = -1.0f;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        width, height,
                        GL_RGBA, GL_FLOAT, clearData.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
}  // namespace

bool ReferenceRcAtlases::allocate() {
    reset();
    for (uint32_t c = 0; c < reflayout::kCascadeCount; ++c) {
        Pair& pair = pairs_[c];
        GLuint textures[2] = {0, 0};
        glGenTextures(2, textures);
        pair.read = textures[0];
        pair.write = textures[1];

        for (int side = 0; side < 2; ++side) {
            const GLuint texture = textures[side];
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F,
                         width_, height_,
                         0, GL_RGBA, GL_FLOAT, nullptr);

            // Cleared-state contract: RGB zero, alpha -1 (negative miss sentinel).
            clearTextureToSentinel(texture, width_, height_);

            const std::string label = "ReferenceRC.C" + std::to_string(c) +
                                      (side == 0 ? ".read" : ".write");
            gl::labelObject(GL_TEXTURE, texture, label);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (gl::checkGLError("ReferenceRcAtlases::allocate", 0)) {
        std::cerr << "[ReferenceRC] GL error during atlas allocation.\n";
        reset();
        return false;
    }
    valid_ = true;
    return true;
}

void ReferenceRcAtlases::swap() {
    for (auto& pair : pairs_)
        std::swap(pair.read, pair.write);
    historyValid_ = true;
    ++historyGeneration_;
}

void ReferenceRcAtlases::invalidateHistory() {
    for (const auto& pair : pairs_)
        clearTextureToSentinel(pair.read, width_, height_);
    historyValid_ = false;
    // Generation is preserved: it advances only through a completed swap.
}

bool ReferenceRcAtlases::verifyClearedState(uint32_t cascade, bool readSide) const {
    const GLuint texture = readSide ? pairs_[cascade].read : pairs_[cascade].write;
    if (texture == 0)
        return false;
    glBindTexture(GL_TEXTURE_2D, texture);
    std::vector<float> data(static_cast<size_t>(width_) * height_ * 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, data.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    for (size_t i = 0; i < data.size(); i += 4) {
        if (data[i] != 0.0f || data[i + 1] != 0.0f || data[i + 2] != 0.0f ||
            data[i + 3] != -1.0f)
            return false;
    }
    return true;
}
