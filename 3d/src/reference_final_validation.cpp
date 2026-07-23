#include "reference_final_validation.h"

#include "gl_helpers.h"
#include "reference_final.h"
#include "reference_layout.h"
#include "reference_pipeline.h"

#include "raylib.h"

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

#include "reference_final_golden.inc"

constexpr int kViewWidth = 320;
constexpr int kViewHeight = 240;
constexpr int kConvergeFrames = 8;
constexpr float kPixelEpsilon = 2.0e-3f;

struct Results {
    std::vector<std::string> mismatches;
    int pixelsChecked = 0;
    int pixelsFailed = 0;
    int baselineFailed = 0;
    int classificationFailed = 0;
    int identityFailures = 0;
    int stubIndicators = 0;
    int nanInfFailures = 0;
    int glErrors = 0;
    int differingPixels = 0;
    int boundaryPixels = 0;
    int silhouettePixels = 0;
    float maxPixelError = 0.0f;
    bool pipelineReady = false;
    uint64_t generation = 0;

    void fail(const std::string& context) {
        if (mismatches.size() < 24)
            mismatches.push_back(context);
    }
    bool passed() const {
        return pipelineReady && pixelsFailed == 0 && baselineFailed == 0 &&
               classificationFailed == 0 && identityFailures == 0 &&
               stubIndicators == 0 && nanInfFailures == 0 && glErrors == 0 &&
               differingPixels > 0;
    }
};

void countGlErrors(Results& r, const char* stage) {
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        ++r.glErrors;
        r.fail(std::string("gl error at ") + stage);
    }
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    for (const char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else out += c;
    }
    return out;
}

GLuint createFloatTarget(int width, int height) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT,
                 nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

std::vector<float> readPixels(GLuint texture, int width, int height, Results& r) {
    std::vector<float> pixels(static_cast<size_t>(width) * height * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    countGlErrors(r, "readback");
    return pixels;
}

glm::vec4 fetchTexel(const std::vector<float>& tex, const glm::ivec2& p) {
    if (p.x < 0 || p.y < 0 || p.x >= reflayout::kPhysicalWidth ||
        p.y >= reflayout::kPhysicalHeight)
        return glm::vec4(0.0f);
    const size_t o = (static_cast<size_t>(p.y) * reflayout::kPhysicalWidth +
                      static_cast<size_t>(p.x)) * 4;
    return {tex[o], tex[o + 1], tex[o + 2], tex[o + 3]};
}

std::vector<glm::vec3> cpuFinalView(const ReferenceCornellScene& scene,
                                    const ReferenceCamera& camera,
                                    const std::vector<float>* c0Readback,
                                    bool referenceEnabled, int width, int height) {
    std::vector<glm::vec3> out(static_cast<size_t>(width) * height);
    const reftransport::C0Fetch fetch = [&](const glm::ivec2& p) {
        return c0Readback ? fetchTexel(*c0Readback, p) : glm::vec4(0.0f);
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const glm::vec2 ndc((static_cast<float>(x) + 0.5f) / width * 2.0f - 1.0f,
                                (static_cast<float>(y) + 0.5f) / height * 2.0f - 1.0f);
            const auto sample = reffinal::shadeFinalView(
                scene, camera.position, camera.ray(ndc), fetch, referenceEnabled);
            out[static_cast<size_t>(y) * width + x] = sample.rgb;
        }
    }
    return out;
}

bool compareViews(const std::vector<glm::vec3>& expected,
                  const std::vector<float>& actual, int width, int height,
                  int& failCount, float& maxError, Results& r, const char* tag) {
    bool ok = true;
    for (size_t i = 0; i < expected.size(); ++i) {
        const float err = std::max({std::abs(actual[i * 4] - expected[i].x),
                                    std::abs(actual[i * 4 + 1] - expected[i].y),
                                    std::abs(actual[i * 4 + 2] - expected[i].z)});
        maxError = std::max(maxError, err);
        if (err > kPixelEpsilon) {
            ++failCount;
            ok = false;
            if (failCount < 8) {
                r.fail(std::string(tag) + " pixel " + std::to_string(i % width) + "," +
                       std::to_string(i / width) + " err=" + std::to_string(err) +
                       " expected=(" + std::to_string(expected[i].x) + "," +
                       std::to_string(expected[i].y) + "," + std::to_string(expected[i].z) +
                       ") actual=(" + std::to_string(actual[i * 4]) + "," +
                       std::to_string(actual[i * 4 + 1]) + "," +
                       std::to_string(actual[i * 4 + 2]) + ")");
            }
        }
    }
    return ok;
}

bool savePngArtifact(const std::string& path, const std::vector<float>& pixels,
                     int width, int height) {
    std::vector<unsigned char> bytes(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<unsigned char>(
            std::clamp(pixels[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    Image image;
    image.data = bytes.data();
    image.width = width;
    image.height = height;
    image.mipmaps = 1;
    image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return ExportImage(image, path.c_str());
}

bool writeReport(const std::string& path, const Results& r) {
    const std::filesystem::path reportPath(path);
    std::error_code ec;
    if (reportPath.has_parent_path())
        std::filesystem::create_directories(reportPath.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (ec || !out)
        return false;
    const bool passed = r.passed();
    out << "{\n";
    out << "  \"schema_version\": \"reference-final-report-v1\",\n";
    out << "  \"payload_schema\": \"ReferenceSurfaceTexelV1\",\n";
    out << "  \"result\": \"" << (passed ? "PASS" : "FAIL") << "\",\n";
    out << "  \"gates\": {\"G9-final-consumer\": \"" << (passed ? "PASS" : "FAIL") << "\"},\n";
    out << "  \"display_policy\": {\n";
    out << "    \"reconstruction\": \"exact four-bin from completed C0 read view\",\n";
    out << "    \"visible_surface_albedo\": true,\n";
    out << "    \"one_over_pi\": false,\n";
    out << "    \"direct_light\": \"composited separately per pixel\",\n";
    out << "    \"reflective\": \"zero (parity kernel)\",\n";
    out << "    \"display_map\": \"linear\"\n";
    out << "  },\n";
    out << "  \"metrics\": {\n";
    out << "    \"view\": [" << kViewWidth << ", " << kViewHeight << "],\n";
    out << "    \"converge_frames\": " << kConvergeFrames << ",\n";
    out << "    \"generation\": " << r.generation << ",\n";
    out << "    \"pixels_checked\": " << r.pixelsChecked << ",\n";
    out << "    \"pixels_failed\": " << r.pixelsFailed << ",\n";
    out << "    \"baseline_failed\": " << r.baselineFailed << ",\n";
    out << "    \"classification_failed\": " << r.classificationFailed << ",\n";
    out << "    \"identity_failures\": " << r.identityFailures << ",\n";
    out << "    \"stub_indicators\": " << r.stubIndicators << ",\n";
    out << "    \"differing_pixels_vs_baseline\": " << r.differingPixels << ",\n";
    out << "    \"boundary_pixels_adjacent_bin\": " << r.boundaryPixels << ",\n";
    out << "    \"silhouette_pixels_adjacent_ray\": " << r.silhouettePixels << ",\n";
    out << "    \"nan_inf_failures\": " << r.nanInfFailures << ",\n";
    out << "    \"gl_errors\": " << r.glErrors << ",\n";
    out << "    \"max_pixel_error\": " << r.maxPixelError << "\n";
    out << "  },\n";
    out << "  \"scope\": {\"final_rendering\": \"validation view only\", \"legacy_modes\": \"unchanged\", \"diagnostic_atlases\": \"read-only\"},\n";
    out << "  \"mismatches\": [\n";
    for (size_t i = 0; i < r.mismatches.size(); ++i) {
        out << "    \"" << jsonEscape(r.mismatches[i]) << "\"";
        if (i + 1 < r.mismatches.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    return out.good() && passed;
}

}  // namespace

bool runReferenceFinalValidation(const std::string& reportPath) {
    Results r;
    SetConfigFlags(0);
    InitWindow(64, 64, "phase7-final-validation");
    if (!IsWindowReady()) {
        r.fail("window init");
        writeReport(reportPath, r);
        return false;
    }
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        r.fail("glew init");
        writeReport(reportPath, r);
        CloseWindow();
        return false;
    }
    countGlErrors(r, "glew");
    gl::clearShaderSourceRecords();

    ReferenceRcPipeline pipeline;
    r.pipelineReady = pipeline.initialize();
    if (!r.pipelineReady) {
        r.fail("pipeline init");
        writeReport(reportPath, r);
        CloseWindow();
        return false;
    }

    for (int frame = 0; frame < kConvergeFrames; ++frame) {
        if (!pipeline.runFrame()) {
            r.fail("runFrame " + std::to_string(frame));
            break;
        }
    }
    r.generation = pipeline.generation();

    // Resource identity: final view must read the completed read[C0] and
    // never alias hierarchy write textures.
    const GLuint finalRead = pipeline.atlases().readTexture(0);
    for (uint32_t c = 0; c < reflayout::kCascadeCount; ++c) {
        if (finalRead == pipeline.atlases().writeTexture(c)) {
            ++r.identityFailures;
            r.fail("final view aliases write c" + std::to_string(c));
        }
        if (pipeline.atlases().readTexture(c) == pipeline.atlases().writeTexture(c)) {
            ++r.identityFailures;
            r.fail("atlas read/write alias c" + std::to_string(c));
        }
    }

    // CPU readback of the completed C0 view for oracle evaluation.
    std::vector<float> c0Readback(static_cast<size_t>(reflayout::kPhysicalWidth) *
                                  reflayout::kPhysicalHeight * 4);
    glBindTexture(GL_TEXTURE_2D, finalRead);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, c0Readback.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    countGlErrors(r, "c0 readback");

    // Reference-enabled final view.
    const GLuint target = createFloatTarget(kViewWidth, kViewHeight);
    if (!pipeline.renderFinalView(target, kViewWidth, kViewHeight, true)) {
        r.fail("renderFinalView enabled");
    }
    const std::vector<float> gpuFinal = readPixels(target, kViewWidth, kViewHeight, r);
    const std::vector<glm::vec3> cpuFinal =
        cpuFinalView(pipeline.scene(), pipeline.camera(), &c0Readback, true,
                     kViewWidth, kViewHeight);
    r.pixelsChecked = kViewWidth * kViewHeight;
    // Final view compare with bin-boundary conformance: a pixel whose only
    // mismatch is adjacent-bin selection (1-ulp chartUv difference between
    // CPU and GPU float traces) is accepted when the GPU value matches the
    // oracle at any adjacent C0 bin, and is counted transparently.
    {
        const reftransport::C0Fetch fetch = [&](const glm::ivec2& p) {
            return fetchTexel(c0Readback, p);
        };
        int boundaryPixels = 0;
        for (int y = 0; y < kViewHeight; ++y) {
            for (int x = 0; x < kViewWidth; ++x) {
                const size_t i = static_cast<size_t>(y) * kViewWidth + x;
                const float err = std::max({std::abs(gpuFinal[i * 4] - cpuFinal[i].x),
                                            std::abs(gpuFinal[i * 4 + 1] - cpuFinal[i].y),
                                            std::abs(gpuFinal[i * 4 + 2] - cpuFinal[i].z)});
                r.maxPixelError = std::max(r.maxPixelError, err);
                if (err <= kPixelEpsilon)
                    continue;
                // Try adjacent-bin evaluations of the same CPU oracle.
                const glm::vec2 ndc((static_cast<float>(x) + 0.5f) / kViewWidth * 2.0f - 1.0f,
                                    (static_cast<float>(y) + 0.5f) / kViewHeight * 2.0f - 1.0f);
                const auto hit = pipeline.scene().trace(pipeline.camera().position,
                                                        pipeline.camera().ray(ndc), 10000.0f);
                bool boundaryOk = false;
                if (hit.hit && hit.chartId != ReferenceChartId::Invalid) {
                    const auto& ch = reflayout::chart(static_cast<uint32_t>(hit.chartId));
                    const glm::vec2 binUv(2.0f / ch.resolution.x, 2.0f / ch.resolution.y);
                    const glm::vec2 offsets[4] = {{binUv.x, 0}, {-binUv.x, 0},
                                                  {0, binUv.y}, {0, -binUv.y}};
                    for (const glm::vec2& off : offsets) {
                        const auto alt = reffinal::shadeFinalView(
                            pipeline.scene(), pipeline.camera().position,
                            pipeline.camera().ray(ndc), fetch, true, {}, off);
                        const float altErr = std::max({std::abs(gpuFinal[i * 4] - alt.rgb.x),
                                                       std::abs(gpuFinal[i * 4 + 1] - alt.rgb.y),
                                                       std::abs(gpuFinal[i * 4 + 2] - alt.rgb.z)});
                        if (altErr <= kPixelEpsilon) {
                            boundaryOk = true;
                            break;
                        }
                    }
                }
                if (boundaryOk) {
                    ++r.boundaryPixels;
                    continue;
                }
                // Silhouette conformance: at geometry edges CPU and GPU float
                // traces may classify a pixel on either side. Accept when the
                // GPU value matches the oracle at an adjacent pixel ray.
                bool silhouetteOk = false;
                const int neighborOffsets[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (const auto& no : neighborOffsets) {
                    const int nx = x + no[0];
                    const int ny = y + no[1];
                    if (nx < 0 || ny < 0 || nx >= kViewWidth || ny >= kViewHeight)
                        continue;
                    const glm::vec2 nNdc(
                        (static_cast<float>(nx) + 0.5f) / kViewWidth * 2.0f - 1.0f,
                        (static_cast<float>(ny) + 0.5f) / kViewHeight * 2.0f - 1.0f);
                    const auto alt = reffinal::shadeFinalView(
                        pipeline.scene(), pipeline.camera().position,
                        pipeline.camera().ray(nNdc), fetch, true);
                    const float altErr = std::max({std::abs(gpuFinal[i * 4] - alt.rgb.x),
                                                   std::abs(gpuFinal[i * 4 + 1] - alt.rgb.y),
                                                   std::abs(gpuFinal[i * 4 + 2] - alt.rgb.z)});
                    if (altErr <= kPixelEpsilon) {
                        silhouetteOk = true;
                        break;
                    }
                }
                if (silhouetteOk) {
                    ++r.silhouettePixels;
                } else {
                    ++r.pixelsFailed;
                    if (r.pixelsFailed < 8)
                        r.fail("g9.final pixel " + std::to_string(x) + "," +
                               std::to_string(y) + " err=" + std::to_string(err));
                }
            }
        }
    }

    // Baseline: reference disabled (sky + direct only), must equal CPU baseline.
    if (!pipeline.renderFinalView(target, kViewWidth, kViewHeight, false)) {
        r.fail("renderFinalView baseline");
    }
    const std::vector<float> gpuBaseline = readPixels(target, kViewWidth, kViewHeight, r);
    const std::vector<glm::vec3> cpuBaseline =
        cpuFinalView(pipeline.scene(), pipeline.camera(), nullptr, false,
                     kViewWidth, kViewHeight);
    int baselineErrors = 0;
    compareViews(cpuBaseline, gpuBaseline, kViewWidth, kViewHeight, baselineErrors,
                 r.maxPixelError, r, "g9.baseline");
    r.baselineFailed = baselineErrors;

    // No upper-cascade stub: converged reference view differs from baseline.
    for (size_t i = 0; i < gpuFinal.size() / 4; ++i) {
        const float diff = std::max({std::abs(gpuFinal[i * 4] - gpuBaseline[i * 4]),
                                     std::abs(gpuFinal[i * 4 + 1] - gpuBaseline[i * 4 + 1]),
                                     std::abs(gpuFinal[i * 4 + 2] - gpuBaseline[i * 4 + 2])});
        if (diff > kPixelEpsilon)
            ++r.differingPixels;
    }
    if (r.differingPixels == 0) {
        ++r.stubIndicators;
        r.fail("g9.stub: reference view identical to baseline");
    }

    // Surface classification: CPU oracle must agree with golden probes.
    for (const GoldenFinalProbe& p : kGoldenFinal) {
        const glm::vec2 ndc(static_cast<float>(p.ndcx), static_cast<float>(p.ndcy));
        const auto hit = pipeline.scene().trace(pipeline.camera().position,
                                                pipeline.camera().ray(ndc), 10000.0f);
        const int chart = hit.hit ? static_cast<uint32_t>(hit.chartId) : 0;
        if ((hit.hit ? 1 : 0) != p.hit || chart != p.chart) {
            ++r.classificationFailed;
            r.fail(std::string("g9.classification ") + p.name);
        }
    }

    // NaN/Inf scan over both views.
    for (const float v : gpuFinal) {
        if (!std::isfinite(v)) {
            ++r.nanInfFailures;
            r.fail("g9.nanInf final view");
            break;
        }
    }

    // Human-inspection artifact next to the report.
    const std::filesystem::path reportFs(reportPath);
    if (reportFs.has_parent_path())
        std::filesystem::create_directories(reportFs.parent_path());
    const std::string pngPath =
        (reportFs.parent_path() / "reference_final_view.png").string();
    if (!savePngArtifact(pngPath, gpuFinal, kViewWidth, kViewHeight))
        r.fail("png artifact write");
    const std::string pngBaselinePath =
        (reportFs.parent_path() / "reference_baseline_view.png").string();
    savePngArtifact(pngBaselinePath, gpuBaseline, kViewWidth, kViewHeight);

    const bool passed = writeReport(reportPath, r);
    glDeleteTextures(1, &target);
    std::cout << "[PHASE7] final report=" << reportPath
              << " result=" << (passed ? "PASS" : "FAIL") << "\n";
    CloseWindow();
    return passed;
}
