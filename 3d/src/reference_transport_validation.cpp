#include "reference_transport_validation.h"

#include "config.h"
#include "gl_helpers.h"
#include "reference_cornell_scene.h"
#include "reference_layout.h"
#include "reference_rc_atlases.h"
#include "reference_transport.h"

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

#include "reference_layout_golden.inc"
#include "reference_transport_golden.inc"

constexpr float kGoldenAlphaEpsilon = 1.0e-4f;
constexpr float kGoldenRgbEpsilon = 5.0e-4f;
constexpr float kGpuEpsilon = 1.0e-3f;
constexpr size_t kMaxRecordedMismatches = 32;

struct Mismatch {
    std::string context;
    std::string field;
    double expected = 0.0;
    double actual = 0.0;
};

struct Results {
    std::vector<Mismatch> mismatches;
    int fixturesChecked = 0;
    int fixturesFailed = 0;
    int gpuFixtureFailures = 0;
    int bandSamplesChecked = 0;
    int bandSamplesFailed = 0;
    int payloadFailures = 0;
    int inactiveFailures = 0;
    int classificationFailures = 0;
    int glErrors = 0;
    bool shaderLoaded = false;
    bool atlasesAllocated = false;
    float maxRgbError = 0.0f;
    float maxAlphaError = 0.0f;
    int skySamples = 0;
    int hitSamples = 0;
    int distinctHitDistances = 0;

    void fail(const std::string& context, const std::string& field,
              double expected, double actual) {
        if (mismatches.size() < kMaxRecordedMismatches)
            mismatches.push_back({context, field, expected, actual});
    }

    bool g5() const {
        return payloadFailures == 0 && inactiveFailures == 0 &&
               classificationFailures == 0 && bandSamplesFailed == 0 &&
               glErrors == 0 && atlasesAllocated && shaderLoaded;
    }
    bool g8() const {
        return fixturesFailed == 0 && gpuFixtureFailures == 0;
    }
    bool passed() const { return g5() && g8(); }
};

bool close(double a, double b, double epsilon) {
    return std::abs(a - b) <= epsilon;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    for (const char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string glString(GLenum name) {
    const GLubyte* value = glGetString(name);
    return value ? reinterpret_cast<const char*>(value) : "unavailable";
}

// CPU expectation for one transport fixture.
reftransport::LocalSample cpuExpect(const ReferenceCornellScene& scene,
                                    const GoldenTransportFixture& f,
                                    ReferenceMaterialKind* kindOut,
                                    bool* hitOut) {
    const glm::vec3 dir(static_cast<float>(f.dx), static_cast<float>(f.dy),
                        static_cast<float>(f.dz));
    if (f.type == 0) {
        const auto hit = scene.trace(
            {static_cast<float>(f.ox), static_cast<float>(f.oy), static_cast<float>(f.oz)},
            dir, static_cast<float>(f.maxT));
        if (kindOut) *kindOut = hit.materialKind;
        if (hitOut) *hitOut = hit.hit;
        return reftransport::shadeHit(scene, hit, glm::normalize(dir),
                                      static_cast<float>(f.thetaIndex),
                                      static_cast<uint32_t>(f.probeSize + 0.5));
    }
    ReferenceTraceHit hit;
    hit.hit = true;
    hit.distance = static_cast<float>(f.hdist);
    hit.position = {0.0f, 0.0f, 0.0f};
    hit.normal = {static_cast<float>(f.hx), static_cast<float>(f.hy),
                  static_cast<float>(f.hz)};
    hit.materialKind = static_cast<ReferenceMaterialKind>(f.materialKind);
    hit.reflectanceOrEmission = {static_cast<float>(f.ex), static_cast<float>(f.ey),
                                 static_cast<float>(f.ez)};
    if (kindOut) *kindOut = hit.materialKind;
    if (hitOut) *hitOut = true;
    return reftransport::shadeHit(scene, hit, glm::normalize(dir),
                                  static_cast<float>(f.thetaIndex),
                                  static_cast<uint32_t>(f.probeSize + 0.5));
}

int categoryOf(const ReferenceTraceHit& hit, const glm::vec3& probeDir) {
    if (!hit.hit)
        return 4;
    switch (hit.materialKind) {
        case ReferenceMaterialKind::Diffuse:
            return glm::dot(hit.normal, probeDir) < 0.0f ? 0 : 5;
        case ReferenceMaterialKind::BlackUncharted: return 1;
        case ReferenceMaterialKind::Reflective: return 2;
        case ReferenceMaterialKind::Emissive: return 3;
        case ReferenceMaterialKind::Sky: return 4;
    }
    return 4;
}

void checkCpuFixtures(const ReferenceCornellScene& scene, Results& r) {
    for (const GoldenTransportFixture& f : kGoldenTransport) {
        ++r.fixturesChecked;
        const std::string ctx = std::string("fixture.") + f.name;
        ReferenceMaterialKind kind = ReferenceMaterialKind::Sky;
        bool isHit = false;
        const auto sample = cpuExpect(scene, f, &kind, &isHit);
        bool ok = true;
        const float alphaError = std::abs(sample.alpha - static_cast<float>(f.ealpha));
        r.maxAlphaError = std::max(r.maxAlphaError, alphaError);
        if (alphaError > kGoldenAlphaEpsilon) {
            r.fail(ctx, "alpha", f.ealpha, sample.alpha);
            ok = false;
        }
        const float rgbError = std::max({
            std::abs(sample.rgb.r - static_cast<float>(f.er)),
            std::abs(sample.rgb.g - static_cast<float>(f.eg)),
            std::abs(sample.rgb.b - static_cast<float>(f.eb))});
        r.maxRgbError = std::max(r.maxRgbError, rgbError);
        if (rgbError > kGoldenRgbEpsilon) {
            r.fail(ctx, "rgb.r", f.er, sample.rgb.r);
            ok = false;
        }
        // Category agreement (independent derivation of hit classification).
        const glm::vec3 dir = glm::normalize(
            glm::vec3(static_cast<float>(f.dx), static_cast<float>(f.dy),
                      static_cast<float>(f.dz)));
        int cpuCategory;
        if (f.type == 1) {
            cpuCategory = (kind == ReferenceMaterialKind::Emissive) ? 3 :
                          (kind == ReferenceMaterialKind::Reflective) ? 2 :
                          (kind == ReferenceMaterialKind::BlackUncharted) ? 1 :
                          glm::dot(glm::vec3(static_cast<float>(f.hx),
                                             static_cast<float>(f.hy),
                                             static_cast<float>(f.hz)),
                                   dir) < 0.0f ? 0 : 5;
        } else {
            const auto hit = scene.trace(
                {static_cast<float>(f.ox), static_cast<float>(f.oy),
                 static_cast<float>(f.oz)},
                dir, static_cast<float>(f.maxT));
            cpuCategory = categoryOf(hit, dir);
            if (hit.hit && f.expectedChart != 0 &&
                static_cast<uint32_t>(f.expectedChart) !=
                    static_cast<uint32_t>(hit.chartId)) {
                r.fail(ctx, "chartId", f.expectedChart,
                       static_cast<uint32_t>(hit.chartId));
                ok = false;
            }
        }
        if (cpuCategory != f.expectedCategory) {
            r.fail(ctx, "category", f.expectedCategory, cpuCategory);
            ok = false;
        }
        // Sun-occlusion flag: frontface with zero rgb implies occluded sun
        // (B=0 in Phase 4); the golden fixture records the expectation.
        if (f.expectedCategory == 0) {
            const bool rgbZero = sample.rgb.r == 0.0f && sample.rgb.g == 0.0f &&
                                 sample.rgb.b == 0.0f;
            if (f.sunOccluded != 0 && !rgbZero) {
                r.fail(ctx, "sunOccluded", 0.0, sample.rgb.r);
                ok = false;
            }
        }
        if (!ok) ++r.fixturesFailed;
    }
}

// ---------------------------------------------------------------------------
// GPU harness
// ---------------------------------------------------------------------------

struct alignas(16) GpuTransportRequest {
    float origin[4];
    float ray[4];
    float hit0[4];
    float hit1[4];
    float misc[4];
};
static_assert(sizeof(GpuTransportRequest) == 80);

struct GpuSession {
    bool windowReady = false;
    GLuint shader = 0;
    GLuint requestBuffer = 0;
    GLuint recordBuffer = 0;
    GLuint sceneBuffer = 0;
    ~GpuSession() {
        if (sceneBuffer != 0) glDeleteBuffers(1, &sceneBuffer);
        if (recordBuffer != 0) glDeleteBuffers(1, &recordBuffer);
        if (requestBuffer != 0) glDeleteBuffers(1, &requestBuffer);
        if (shader != 0) glDeleteProgram(shader);
        if (windowReady) CloseWindow();
    }
};

void countGlErrors(Results& r, const char* stage) {
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        ++r.glErrors;
        r.fail(stage, "glError", 0, e);
    }
}

bool initGpuSession(GpuSession& session, Results& r) {
    SetConfigFlags(0);
    InitWindow(64, 64, "phase4-transport-validation");
    if (!IsWindowReady()) {
        r.fail("gpu.init", "window", 1, 0);
        return false;
    }
    session.windowReady = true;
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        r.fail("gpu.init", "glew", GLEW_OK, 1);
        return false;
    }
    countGlErrors(r, "gpu.init");
    if (!glewIsSupported("GL_ARB_compute_shader") ||
        !glewIsSupported("GL_ARB_shader_image_load_store")) {
        r.fail("gpu.init", "compute/image", 1, 0);
        return false;
    }
    return true;
}

bool uploadScene(GpuSession& session, const ReferenceCornellScene& scene, Results& r) {
    const ReferenceSceneGpuData& data = scene.gpuData();
    glGenBuffers(1, &session.sceneBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, session.sceneBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(ReferenceSceneGpuData), &data,
                 GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, session.sceneBuffer);
    countGlErrors(r, "gpu.scene");
    return session.sceneBuffer != 0;
}

void runGpuFixtures(GpuSession& session, const ReferenceCornellScene& scene,
                    Results& r) {
    std::vector<GpuTransportRequest> requests;
    requests.reserve(sizeof(kGoldenTransport) / sizeof(kGoldenTransport[0]));
    for (const GoldenTransportFixture& f : kGoldenTransport) {
        GpuTransportRequest q{};
        if (f.type == 0) {
            q.origin[0] = static_cast<float>(f.ox);
            q.origin[1] = static_cast<float>(f.oy);
            q.origin[2] = static_cast<float>(f.oz);
            q.origin[3] = 0.0f;
        } else {
            q.origin[3] = 1.0f;
        }
        q.ray[0] = static_cast<float>(f.dx);
        q.ray[1] = static_cast<float>(f.dy);
        q.ray[2] = static_cast<float>(f.dz);
        q.ray[3] = static_cast<float>(f.maxT);
        q.hit0[0] = static_cast<float>(f.hx);
        q.hit0[1] = static_cast<float>(f.hy);
        q.hit0[2] = static_cast<float>(f.hz);
        q.hit0[3] = static_cast<float>(f.hdist);
        q.hit1[0] = static_cast<float>(f.ex);
        q.hit1[1] = static_cast<float>(f.ey);
        q.hit1[2] = static_cast<float>(f.ez);
        q.hit1[3] = static_cast<float>(f.materialKind);
        q.misc[0] = static_cast<float>(f.thetaIndex);
        q.misc[1] = static_cast<float>(f.probeSize);
        requests.push_back(q);
    }

    const size_t count = requests.size();
    glGenBuffers(1, &session.requestBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, session.requestBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(count * sizeof(GpuTransportRequest)),
                 requests.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &session.recordBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, session.recordBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(count * 4 * sizeof(float)), nullptr,
                 GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, session.requestBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, session.recordBuffer);

    glUseProgram(session.shader);
    glUniform1i(glGetUniformLocation(session.shader, "uMode"), 0);
    glUniform1i(glGetUniformLocation(session.shader, "uLayoutRequest"), 0);
    glUniform1i(glGetUniformLocation(session.shader, "uRequestCount"),
                static_cast<GLint>(count));
    glDispatchCompute(1, static_cast<GLuint>((count + 63) / 64), 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    std::vector<float> records(count * 4);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, session.recordBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(count * 4 * sizeof(float)),
                       records.data());
    countGlErrors(r, "gpu.fixtures.dispatch");

    for (size_t i = 0; i < count; ++i) {
        const GoldenTransportFixture& f = kGoldenTransport[i];
        const std::string ctx = std::string("gpu.fixture.") + f.name;
        const auto expected = cpuExpect(scene, f, nullptr, nullptr);
        const float* actual = &records[i * 4];
        bool ok = true;
        if (!close(actual[3], expected.alpha, kGpuEpsilon)) {
            r.fail(ctx, "alpha", expected.alpha, actual[3]);
            ok = false;
        }
        const float rgbError = std::max({std::abs(actual[0] - expected.rgb.r),
                                         std::abs(actual[1] - expected.rgb.g),
                                         std::abs(actual[2] - expected.rgb.b)});
        if (rgbError > kGpuEpsilon) {
            r.fail(ctx, "rgb.r", expected.rgb.r, actual[0]);
            ok = false;
        }
        // GPU must also agree with the independent golden, not only the CPU.
        const float goldenError = std::max({std::abs(actual[0] - static_cast<float>(f.er)),
                                            std::abs(actual[1] - static_cast<float>(f.eg)),
                                            std::abs(actual[2] - static_cast<float>(f.eb))});
        if (goldenError > kGoldenRgbEpsilon * 2.0f) {
            r.fail(ctx, "golden.rgb.r", f.er, actual[0]);
            ok = false;
        }
        if (!close(actual[3], f.ealpha, kGoldenAlphaEpsilon * 2.0f)) {
            r.fail(ctx, "golden.alpha", f.ealpha, actual[3]);
            ok = false;
        }
        if (!ok) ++r.gpuFixtureFailures;
    }
}

reftransport::LocalSample bandExpect(const ReferenceCornellScene& scene,
                                     const glm::vec2& globalUv, bool* activeOut) {
    const auto decode = reflayout::decodeGlobalUv(globalUv);
    if (activeOut)
        *activeOut = decode.active;
    if (!decode.active)
        return {};
    const glm::vec3 chartNormal = reflayout::chart(decode.chartId).normal;
    return reftransport::traceAndShade(
        scene, decode.probePosition + chartNormal * 0.001f, decode.probeDirection,
        decode.maxTraceDistance, decode.thetaIndex, decode.probeSize);
}

void runBandTransport(GpuSession& session, const ReferenceCornellScene& scene,
                      Results& r, ReferenceRcAtlases& atlases) {
    std::vector<float> pixels(static_cast<size_t>(reflayout::kPhysicalWidth) *
                              reflayout::kPhysicalHeight * 4);

    // Deterministic sample set: layout direction-fixture UVs plus LCG samples.
    std::vector<glm::vec2> samples;
    for (const GoldenDirectionFixture& f : kGoldenDirection)
        samples.emplace_back(static_cast<float>(f.uvx), static_cast<float>(f.uvy));
    uint32_t state = 0x51ab3c29u;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 8) & 0xFFFFFF) / 16777216.0f;
    };
    for (int i = 0; i < 1024; ++i)
        samples.emplace_back(next() * 1024.0f, next() * 3072.0f);

    std::vector<float> hitDistances;
    for (uint32_t c = 0; c < reflayout::kCascadeCount; ++c) {
        const GLuint texture = atlases.writeTexture(c);
        glUseProgram(session.shader);
        glUniform1i(glGetUniformLocation(session.shader, "uMode"), 1);
        glUniform1i(glGetUniformLocation(session.shader, "uCascade"),
                    static_cast<GLint>(c));
        glUniform1i(glGetUniformLocation(session.shader, "uEnableUpperMerge"), 0);
        glUniform1i(glGetUniformLocation(session.shader, "uHistoryValid"), 0);
        glUniform1i(glGetUniformLocation(session.shader, "uPhysicalWidth"),
                    reflayout::kPhysicalWidth);
        glUniform1i(glGetUniformLocation(session.shader, "uPhysicalHeight"),
                    reflayout::kPhysicalHeight);
        glBindImageTexture(2, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glDispatchCompute(reflayout::kPhysicalWidth / 8, reflayout::kPhysicalHeight / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        countGlErrors(r, "gpu.band.dispatch");

        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        countGlErrors(r, "gpu.band.readback");

        // Payload-wide predicates: finite, nonnegative RGB; alpha semantics.
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (!std::isfinite(pixels[i]) || !std::isfinite(pixels[i + 1]) ||
                !std::isfinite(pixels[i + 2]) || !std::isfinite(pixels[i + 3])) {
                r.fail("payload.c" + std::to_string(c), "finite", 1.0, 0.0);
                ++r.payloadFailures;
                break;
            }
            if (pixels[i] < 0.0f || pixels[i + 1] < 0.0f || pixels[i + 2] < 0.0f) {
                r.fail("payload.c" + std::to_string(c), "nonnegative", 0.0, pixels[i]);
                ++r.payloadFailures;
                break;
            }
        }

        // Sampled texels vs CPU oracle + payload classification agreement.
        // The GPU evaluates one probe per texel at its half-texel center;
        // snap sample UVs to that center so CPU and GPU decode identically.
        for (const glm::vec2& sampleUv : samples) {
            const glm::vec2 uv(std::floor(sampleUv.x) + 0.5f,
                               std::floor(sampleUv.y) + 0.5f);
            const auto decode = reflayout::decodeGlobalUv(uv);
            if (!decode.active || decode.cascade != c)
                continue;
            const int tx = static_cast<int>(decode.physicalUv.x);
            const int ty = static_cast<int>(decode.physicalUv.y);
            const size_t o = (static_cast<size_t>(ty) * reflayout::kPhysicalWidth +
                              static_cast<size_t>(tx)) * 4;
            bool active = false;
            const auto expected = bandExpect(scene, uv, &active);
            ++r.bandSamplesChecked;
            bool ok = true;
            if (!active) {
                r.fail("band.sample", "active", 1, 0);
                ok = false;
            } else {
                const std::string ctx = "band.c" + std::to_string(c) +
                                        "(uv=" + std::to_string(uv.x) + "," +
                                        std::to_string(uv.y) + ")";
                const float alphaError = std::abs(pixels[o + 3] - expected.alpha);
                const float rgbError = std::max({std::abs(pixels[o] - expected.rgb.r),
                                                 std::abs(pixels[o + 1] - expected.rgb.g),
                                                 std::abs(pixels[o + 2] - expected.rgb.b)});
                r.maxAlphaError = std::max(r.maxAlphaError, alphaError);
                r.maxRgbError = std::max(r.maxRgbError, rgbError);
                if (alphaError > kGpuEpsilon) {
                    r.fail(ctx, "alpha", expected.alpha, pixels[o + 3]);
                    ok = false;
                }
                if (rgbError > kGpuEpsilon) {
                    r.fail(ctx, "rgb.r", expected.rgb.r, pixels[o]);
                    ok = false;
                }
                // Classification: negative alpha exactly for sky misses.
                const bool gpuSky = pixels[o + 3] < 0.0f;
                const bool cpuSky = expected.alpha < 0.0f;
                if (gpuSky != cpuSky) {
                    r.fail(ctx, "skyClassification", cpuSky ? 1 : 0, gpuSky ? 1 : 0);
                    ++r.classificationFailures;
                    ok = false;
                }
                if (cpuSky) ++r.skySamples;
                else {
                    ++r.hitSamples;
                    hitDistances.push_back(expected.alpha);
                }
            }
            if (!ok) ++r.bandSamplesFailed;
        }
    }

    // G5: distance alpha must not collapse to a boolean payload.
    std::sort(hitDistances.begin(), hitDistances.end());
    hitDistances.erase(std::unique(hitDistances.begin(), hitDistances.end(),
                                   [](float a, float b) { return a == b; }),
                       hitDistances.end());
    r.distinctHitDistances = static_cast<int>(hitDistances.size());
    if (r.hitSamples > 0 && r.distinctHitDistances < 2) {
        r.fail("payload", "distanceAlphaVariety", 2.0, r.distinctHitDistances);
        ++r.payloadFailures;
    }

    // Inactive texels retain the cleared-state payload.
    for (uint32_t c = 0; c < reflayout::kCascadeCount; ++c) {
        const GLuint texture = atlases.writeTexture(c);
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        for (int ty = 256; ty < reflayout::kPhysicalHeight; ty += 64) {
            for (int tx = 300; tx < reflayout::kPhysicalWidth; tx += 128) {
                const size_t o = (static_cast<size_t>(ty) * reflayout::kPhysicalWidth +
                                  static_cast<size_t>(tx)) * 4;
                if (pixels[o] != 0.0f || pixels[o + 1] != 0.0f || pixels[o + 2] != 0.0f ||
                    pixels[o + 3] != -1.0f) {
                    r.fail("inactive.c" + std::to_string(c), "payload", -1.0, pixels[o + 3]);
                    ++r.inactiveFailures;
                }
            }
        }
    }
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
    out << "  \"schema_version\": \"reference-transport-report-v1\",\n";
    out << "  \"payload_schema\": \"" << reftransport::kPayloadSchema << "\",\n";
    out << "  \"result\": \"" << (passed ? "PASS" : "FAIL") << "\",\n";
    out << "  \"gates\": {\n";
    out << "    \"G5-payload-contract\": \"" << (r.g5() ? "PASS" : "FAIL") << "\",\n";
    out << "    \"G8-material-direct-light\": \"" << (r.g8() ? "PASS" : "FAIL") << "\"\n";
    out << "  },\n";
    out << "  \"gpu\": {\n";
    out << "    \"vendor\": \"" << jsonEscape(glString(GL_VENDOR)) << "\",\n";
    out << "    \"renderer\": \"" << jsonEscape(glString(GL_RENDERER)) << "\",\n";
    out << "    \"opengl_version\": \"" << jsonEscape(glString(GL_VERSION)) << "\"\n";
    out << "  },\n";
    out << "  \"shaders\": [\n";
    const auto& shaderRecords = gl::getShaderSourceRecords();
    for (size_t i = 0; i < shaderRecords.size(); ++i) {
        const auto& s = shaderRecords[i];
        out << "    {\"name\": \"" << jsonEscape(s.logicalName)
            << "\", \"sha256\": \"" << s.sourceHash
            << "\", \"compiled\": " << (s.compiled ? "true" : "false") << "}";
        if (i + 1 < shaderRecords.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"metrics\": {\n";
    out << "    \"fixtures_checked\": " << r.fixturesChecked << ",\n";
    out << "    \"fixtures_failed\": " << r.fixturesFailed << ",\n";
    out << "    \"gpu_fixture_failures\": " << r.gpuFixtureFailures << ",\n";
    out << "    \"band_samples_checked\": " << r.bandSamplesChecked << ",\n";
    out << "    \"band_samples_failed\": " << r.bandSamplesFailed << ",\n";
    out << "    \"payload_failures\": " << r.payloadFailures << ",\n";
    out << "    \"inactive_failures\": " << r.inactiveFailures << ",\n";
    out << "    \"classification_failures\": " << r.classificationFailures << ",\n";
    out << "    \"gl_errors\": " << r.glErrors << ",\n";
    out << "    \"sky_samples\": " << r.skySamples << ",\n";
    out << "    \"hit_samples\": " << r.hitSamples << ",\n";
    out << "    \"distinct_hit_distances\": " << r.distinctHitDistances << ",\n";
    out << "    \"max_rgb_error\": " << r.maxRgbError << ",\n";
    out << "    \"max_alpha_error\": " << r.maxAlphaError << "\n";
    out << "  },\n";
    out << "  \"phase_scope\": {\n";
    out << "    \"temporal_feedback\": \"disabled (B=0)\",\n";
    out << "    \"upper_merge\": \"disabled\",\n";
    out << "    \"calibration_floor\": \"none\",\n";
    out << "    \"proxy_visibility\": \"none\",\n";
    out << "    \"direct_scale_bypass\": \"none\"\n";
    out << "  },\n";
    out << "  \"mismatches\": [\n";
    for (size_t i = 0; i < r.mismatches.size(); ++i) {
        const auto& m = r.mismatches[i];
        out << "    {\"context\": \"" << jsonEscape(m.context)
            << "\", \"field\": \"" << jsonEscape(m.field)
            << "\", \"expected\": " << m.expected
            << ", \"actual\": " << m.actual << "}";
        if (i + 1 < r.mismatches.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.good() && passed;
}

}  // namespace

bool runReferenceTransportValidation(const std::string& reportPath) {
    Results r;
    const ReferenceCornellScene scene;

    // CPU golden checks (no GL required).
    checkCpuFixtures(scene, r);

    // GPU session: fixture request mode, then full-band transport.
    GpuSession session;
    bool gpuOk = initGpuSession(session, r);
    if (gpuOk) {
        gl::setShaderRoot(RC3D_SHADER_ROOT);
        gl::clearShaderSourceRecords();
        session.shader = gl::loadComputeShader(
            gl::resolveShaderPath("reference_transport.comp"),
            "reference_transport.comp");
        r.shaderLoaded = session.shader != 0;
        if (!r.shaderLoaded) {
            r.fail("gpu.shader", "reference_transport.comp", 1, 0);
            gpuOk = false;
        }
        countGlErrors(r, "gpu.shader");
    }
    if (gpuOk)
        gpuOk = uploadScene(session, scene, r);
    if (gpuOk)
        runGpuFixtures(session, scene, r);

    ReferenceRcAtlases atlases;
    r.atlasesAllocated = gpuOk && atlases.allocate();
    if (r.atlasesAllocated)
        runBandTransport(session, scene, r, atlases);
    else if (gpuOk)
        r.fail("atlases", "allocate", 1, 0);

    const bool passed = writeReport(reportPath, r);
    std::cout << "[PHASE4] transport report=" << reportPath
              << " result=" << (passed ? "PASS" : "FAIL") << "\n";
    return passed;
}
