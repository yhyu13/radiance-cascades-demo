#include "reference_pipeline.h"

#include "config.h"
#include "gl_helpers.h"
#include "reference_layout.h"

#include <filesystem>
#include <fstream>
#include <iostream>

ReferenceRcPipeline::~ReferenceRcPipeline() {
    shutdown();
}

bool ReferenceRcPipeline::initialize() {
    shutdown();

    gl::setShaderRoot(RC3D_SHADER_ROOT);
    transportShader_ = gl::loadComputeShader(
        gl::resolveShaderPath("reference_transport.comp"),
        "reference_transport.comp");
    if (transportShader_ == 0) {
        std::cerr << "[ReferencePipeline] transport shader load failed\n";
        return false;
    }

    glGenBuffers(1, &sceneBuffer_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sceneBuffer_);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(ReferenceSceneGpuData),
                 &scene_.gpuData(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, sceneBuffer_);

    if (!atlases_.allocate()) {
        std::cerr << "[ReferencePipeline] atlas allocation failed\n";
        return false;
    }
    if (gl::checkGLError("ReferenceRcPipeline::initialize", 0)) {
        std::cerr << "[ReferencePipeline] GL error during initialization\n";
        return false;
    }
    initialized_ = true;
    return true;
}

void ReferenceRcPipeline::shutdown() noexcept {
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

bool ReferenceRcPipeline::runFrame() {
    if (!initialized_ || transportShader_ == 0 || !atlases_.valid())
        return false;
    for (int cascade = 5; cascade >= 0; --cascade) {
        const std::string group = "reference_transport.C" + std::to_string(cascade);
        gl::pushDebugGroup(group.c_str());
        glUseProgram(transportShader_);
        glUniform1i(glGetUniformLocation(transportShader_, "uMode"), 1);
        glUniform1i(glGetUniformLocation(transportShader_, "uCascade"), cascade);
        glUniform1i(glGetUniformLocation(transportShader_, "uEnableUpperMerge"),
                    cascade < 5 ? 1 : 0);
        glUniform1i(glGetUniformLocation(transportShader_, "uHistoryValid"),
                    atlases_.historyValid() ? 1 : 0);
        glUniform1i(glGetUniformLocation(transportShader_, "uPhysicalWidth"),
                    reflayout::kPhysicalWidth);
        glUniform1i(glGetUniformLocation(transportShader_, "uPhysicalHeight"),
                    reflayout::kPhysicalHeight);
        glUniform1f(glGetUniformLocation(transportShader_, "uC0Log2Offset"),
                    c0Log2Offset());
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
        // Skip unused interior padding (x>=256, y>=256): 37.5% of 1024x512.
        // Primary page y in [0,256); interior used region x in [0,256), y in [256,512).
        const GLint originLoc =
            glGetUniformLocation(transportShader_, "uDispatchOrigin");
        glUniform2i(originLoc, 0, 0);
        glDispatchCompute(reflayout::kPhysicalWidth / 8, reflayout::kBandHeight / 8, 1);
        glUniform2i(originLoc, 0, reflayout::kBandHeight);
        glDispatchCompute(256 / 8, reflayout::kBandHeight / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        gl::popDebugGroup();
    }
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);
    atlases_.swap();
    return !gl::checkGLError("ReferenceRcPipeline::runFrame", 0);
}

bool ReferenceRcPipeline::writeOccupancyJson(const std::string& path) const {
    std::array<RcAtlasOccupancy, 6> occupancy{};
    for (uint32_t c = 0; c < 6; ++c)
        occupancy[c] = atlases_.occupancy(c, true);
    const std::filesystem::path p(path);
    std::error_code ec;
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (ec || !out)
        return false;
    out << "{\n";
    out << "  \"schema_version\": \"new-rc-occupancy-v1\",\n";
    out << "  \"algorithm\": \"surface-rc\",\n";
    out << "  \"quality_profile\": \"" << rcQualityProfileName(quality_) << "\",\n";
    out << "  \"atlas_filter\": \"" << rcAtlasFilterName(atlases_.filter()) << "\",\n";
    out << "  \"physical_width\": " << atlases_.width() << ",\n";
    out << "  \"physical_height\": " << atlases_.height() << ",\n";
    out << "  \"texels_per_cascade\": " << (atlases_.width() * atlases_.height()) << ",\n";
    out << "  \"storage_bytes\": "
        << (static_cast<long long>(6) * 2 * atlases_.width() * atlases_.height() * 16)
        << ",\n";
    out << "  \"cascades\": [\n";
    for (int c = 0; c < 6; ++c) {
        const RcAtlasOccupancy& o = occupancy[static_cast<size_t>(c)];
        const double inactiveRatio =
            o.total > 0 ? static_cast<double>(o.inactive) / o.total : 0.0;
        out << "    {\"cascade\": " << c
            << ", \"total\": " << o.total
            << ", \"active\": " << o.active
            << ", \"inactive\": " << o.inactive
            << ", \"inactive_ratio\": " << inactiveRatio << "}";
        out << (c < 5 ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    return out.good();
}

bool ReferenceRcPipeline::renderFinalView(GLuint target, int width, int height,
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
    // Validated path stays linear (1/1); interactive callers override.
    glUniform1f(glGetUniformLocation(transportShader_, "uExposure"), exposure_);
    glUniform1f(glGetUniformLocation(transportShader_, "uInvGamma"), invGamma_);
    glUniform1f(glGetUniformLocation(transportShader_, "uC0Log2Offset"),
                c0Log2Offset());
    gl::pushDebugGroup("reference_final");
    glBindImageTexture(2, target, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, atlases_.readTexture(0));
    glUniform1i(glGetUniformLocation(transportShader_, "uFeedbackC0"), 5);
    glActiveTexture(GL_TEXTURE0);
    const GLuint groupsX = static_cast<GLuint>((width + 7) / 8);
    const GLuint groupsY = static_cast<GLuint>((height + 7) / 8);
    glDispatchCompute(groupsX, groupsY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);
    glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    gl::popDebugGroup();
    return !gl::checkGLError("ReferenceRcPipeline::renderFinalView", 0);
}
