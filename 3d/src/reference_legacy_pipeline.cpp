#include "reference_legacy_pipeline.h"

#include "config.h"
#include "gl_helpers.h"

#include <iostream>

ReferenceLegacyPipeline::~ReferenceLegacyPipeline() {
    shutdown();
}

bool ReferenceLegacyPipeline::initialize() {
    shutdown();
    gl::setShaderRoot(RC3D_SHADER_ROOT);
    transportShader_ = gl::loadComputeShader(
        gl::resolveShaderPath("reference_transport_legacy.comp"),
        "reference_transport_legacy.comp");
    if (transportShader_ == 0) {
        std::cerr << "[LegacyPipeline] transport shader load failed\n";
        return false;
    }
    glGenBuffers(1, &sceneBuffer_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sceneBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(ReferenceSceneGpuData),
                 &scene_.gpuData(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneBuffer_);
    if (!atlases_.allocate()) {
        std::cerr << "[LegacyPipeline] atlas allocation failed\n";
        return false;
    }
    if (gl::checkGLError("ReferenceLegacyPipeline::initialize", 0)) {
        std::cerr << "[LegacyPipeline] GL error during initialization\n";
        return false;
    }
    initialized_ = true;
    return true;
}

void ReferenceLegacyPipeline::shutdown() noexcept {
    if (sceneBuffer_ != 0) {
        glDeleteBuffers(1, &sceneBuffer_);
        sceneBuffer_ = 0;
    }
    if (transportShader_ != 0) {
        glDeleteProgram(transportShader_);
        transportShader_ = 0;
    }
    atlases_.reset();
    initialized_ = false;
}

bool ReferenceLegacyPipeline::runFrame() {
    if (!initialized_ || transportShader_ == 0 || !atlases_.valid())
        return false;
    for (int cascade = 5; cascade >= 0; --cascade) {
        glUseProgram(transportShader_);
        glUniform1i(glGetUniformLocation(transportShader_, "uMode"), 1);
        glUniform1i(glGetUniformLocation(transportShader_, "uCascade"), cascade);
        glUniform1i(glGetUniformLocation(transportShader_, "uEnableUpperMerge"),
                    cascade < 5 ? 1 : 0);
        glUniform1i(glGetUniformLocation(transportShader_, "uHistoryValid"),
                    atlases_.historyValid() ? 1 : 0);
        glBindImageTexture(2, atlases_.writeTexture(static_cast<uint32_t>(cascade)),
                           0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glActiveTexture(GL_TEXTURE4);
        if (cascade < 5)
            glBindTexture(GL_TEXTURE_2D,
                          atlases_.writeTexture(static_cast<uint32_t>(cascade + 1)));
        else
            glBindTexture(GL_TEXTURE_2D, 0);
        glUniform1i(glGetUniformLocation(transportShader_, "uUpperCascade"), 4);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, atlases_.readTexture(0));
        glUniform1i(glGetUniformLocation(transportShader_, "uFeedbackC0"), 5);
        glActiveTexture(GL_TEXTURE0);
        glDispatchCompute(reflegacy::kPhysicalWidth / 8, reflegacy::kPhysicalHeight / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);
    atlases_.swap();
    return !gl::checkGLError("ReferenceLegacyPipeline::runFrame", 0);
}

bool ReferenceLegacyPipeline::renderFinalView(GLuint target, int width, int height,
                                               bool referenceEnabled) {
    if (!initialized_ || transportShader_ == 0 || target == 0 || !atlases_.valid())
        return false;
    glUseProgram(transportShader_);
    glUniform1i(glGetUniformLocation(transportShader_, "uMode"), 2);
    glUniform1i(glGetUniformLocation(transportShader_, "uRefEnabled"),
                referenceEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(transportShader_, "uWidth"), width);
    glUniform1i(glGetUniformLocation(transportShader_, "uHeight"), height);
    glUniform3fv(glGetUniformLocation(transportShader_, "uCamPos"), 1,
                 &camera_.position.x);
    glUniform3fv(glGetUniformLocation(transportShader_, "uCamFwd"), 1,
                 &camera_.forward.x);
    glUniform3fv(glGetUniformLocation(transportShader_, "uCamRight"), 1,
                 &camera_.right.x);
    glUniform3fv(glGetUniformLocation(transportShader_, "uCamUp"), 1,
                 &camera_.up.x);
    glUniform1f(glGetUniformLocation(transportShader_, "uCamTan"),
                camera_.tanHalfFov());
    glUniform1f(glGetUniformLocation(transportShader_, "uAspect"), camera_.aspect);
    glUniform1f(glGetUniformLocation(transportShader_, "uExposure"), exposure_);
    glUniform1f(glGetUniformLocation(transportShader_, "uInvGamma"), invGamma_);
    glBindImageTexture(2, target, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, atlases_.readTexture(0));
    glUniform1i(glGetUniformLocation(transportShader_, "uFeedbackC0"), 5);
    glActiveTexture(GL_TEXTURE0);
    glDispatchCompute(static_cast<GLuint>((width + 7) / 8),
                      static_cast<GLuint>((height + 7) / 8), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);
    glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    return !gl::checkGLError("ReferenceLegacyPipeline::renderFinalView", 0);
}
