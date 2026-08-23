#include "reference_layout_validation.h"

#include "config.h"
#include "gl_helpers.h"
#include "reference_cornell_scene.h"
#include "reference_layout.h"
#include "reference_rc_atlases.h"

#include "raylib.h"

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

#include "reference_layout_golden.inc"

constexpr float kGoldenEpsilon = 2.0e-5f;
constexpr float kGpuEpsilon = 2.0e-5f;
constexpr size_t kMaxRecordedMismatches = 32;

struct Mismatch {
    std::string context;
    std::string field;
    double expected = 0.0;
    double actual = 0.0;
};

struct Results {
    std::vector<Mismatch> mismatches;
    int layoutFixturesChecked = 0;
    int layoutFixturesFailed = 0;
    int directionFixturesChecked = 0;
    int directionFixturesFailed = 0;
    int intervalFixturesChecked = 0;
    int intervalFixturesFailed = 0;
    int transitionChecksFailed = 0;
    int hemisphereSumsFailed = 0;
    int ringCoverageFailed = 0;
    int coverageSamplesChecked = 0;
    int coverageSamplesFailed = 0;
    int gpuDecodeFailures = 0;
    int bandStatFailures = 0;
    int bandDirectionFailures = 0;
    int blockerFailures = 0;
    int clearedStateFailures = 0;
    int glErrors = 0;
    bool shaderLoaded = false;
    bool atlasesAllocated = false;
    float maxAxisError = 0.0f;
    float maxDirectionError = 0.0f;
    float maxWeightError = 0.0f;

    void fail(const std::string& context, const std::string& field,
              double expected, double actual) {
        if (mismatches.size() < kMaxRecordedMismatches)
            mismatches.push_back({context, field, expected, actual});
    }

    bool passed() const {
        return layoutFixturesFailed == 0 && directionFixturesFailed == 0 &&
               intervalFixturesFailed == 0 && transitionChecksFailed == 0 &&
               hemisphereSumsFailed == 0 && ringCoverageFailed == 0 &&
               coverageSamplesFailed == 0 && gpuDecodeFailures == 0 &&
               bandStatFailures == 0 && bandDirectionFailures == 0 &&
               blockerFailures == 0 && clearedStateFailures == 0 &&
               glErrors == 0 && shaderLoaded && atlasesAllocated;
    }
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

// ---------------------------------------------------------------------------
// CPU golden checks
// ---------------------------------------------------------------------------

void checkLayoutFixtures(Results& r) {
    for (const GoldenLayoutFixture& f : kGoldenLayout) {
        const reflayout::ReferenceProbeDecode d =
            reflayout::decodeGlobalUv({static_cast<float>(f.uvx), static_cast<float>(f.uvy)});
        ++r.layoutFixturesChecked;
        const std::string ctx = "layout(uv=" + std::to_string(f.uvx) + "," +
                                std::to_string(f.uvy) + ")";
        bool ok = true;
        if (static_cast<int>(d.active) != f.active) {
            r.fail(ctx, "active", f.active, d.active ? 1 : 0);
            ok = false;
        }
        if (!d.active) {
            if (!ok) ++r.layoutFixturesFailed;
            continue;
        }
        if (static_cast<int>(d.chartId) != f.chartId) { r.fail(ctx, "chartId", f.chartId, d.chartId); ok = false; }
        if (static_cast<int>(d.materialId) != f.materialId) { r.fail(ctx, "materialId", f.materialId, d.materialId); ok = false; }
        if (static_cast<int>(d.cascade) != f.cascade) { r.fail(ctx, "cascade", f.cascade, d.cascade); ok = false; }
        if (static_cast<int>(d.probeSize) != f.probeSize) { r.fail(ctx, "probeSize", f.probeSize, d.probeSize); ok = false; }
        if (!close(d.probePosition.x, f.ppx, kGoldenEpsilon) ||
            !close(d.probePosition.y, f.ppy, kGoldenEpsilon) ||
            !close(d.probePosition.z, f.ppz, kGoldenEpsilon)) {
            r.fail(ctx, "probePos.x", f.ppx, d.probePosition.x);
            r.maxAxisError = std::max(r.maxAxisError,
                std::max({std::abs(d.probePosition.x - static_cast<float>(f.ppx)),
                          std::abs(d.probePosition.y - static_cast<float>(f.ppy)),
                          std::abs(d.probePosition.z - static_cast<float>(f.ppz))}));
            ok = false;
        }
        if (!close(d.maxTraceDistance, f.reach, 0.0)) {
            r.fail(ctx, "reach", f.reach, d.maxTraceDistance);
            ok = false;
        }
        if (!close(d.physicalUv.x, f.physx, kGoldenEpsilon) ||
            !close(d.physicalUv.y, f.physy, kGoldenEpsilon)) {
            r.fail(ctx, "physicalUv", f.physx, d.physicalUv.x);
            ok = false;
        }
        // Physical round-trip through the locked mapping.
        const glm::vec2 rt = reflayout::physicalToGlobal(d.cascade, d.physicalUv);
        if (!close(rt.x, f.uvx, kGoldenEpsilon) || !close(rt.y, f.uvy, kGoldenEpsilon)) {
            r.fail(ctx, "physicalRoundTrip", f.uvx, rt.x);
            ok = false;
        }
        if (!ok) ++r.layoutFixturesFailed;
    }
    // Physical mapping for inactive-chart addresses: out-of-domain addresses
    // must not map; in-domain unused chart regions (interior page x>=256) map
    // to physical storage that stays in the inactive cleared state.
    for (const GoldenLayoutFixture& f : kGoldenLayout) {
        if (f.active != 0)
            continue;
        const bool inDomain = f.uvx >= 0.0 && f.uvx < 1024.0 &&
                              f.uvy >= 0.0 && f.uvy < 3072.0;
        for (uint32_t c = 0; c < reflayout::kCascadeCount; ++c) {
            const glm::vec2 phys = reflayout::globalToPhysical(
                c, {static_cast<float>(f.uvx), static_cast<float>(f.uvy)});
            if (!inDomain && phys.x >= 0.0f) {
                r.fail("inactive(uv=" + std::to_string(f.uvx) + ")", "physicalX", -1.0, phys.x);
                ++r.layoutFixturesFailed;
            }
            if (inDomain) {
                const float bandY0 = 256.0f * static_cast<float>(c);
                const bool inBand = (f.uvy >= bandY0 && f.uvy < bandY0 + 256.0) ||
                                    (f.uvy >= 1536.0 + bandY0 && f.uvy < 1536.0 + bandY0 + 256.0);
                if (inBand && phys.x < 0.0f) {
                    r.fail("inactive-unused(uv=" + std::to_string(f.uvx) + ")", "physicalX", 1.0, -1.0);
                    ++r.layoutFixturesFailed;
                }
                if (!inBand && phys.x >= 0.0f) {
                    r.fail("inactive-band(uv=" + std::to_string(f.uvx) + ")", "physicalX", -1.0, phys.x);
                    ++r.layoutFixturesFailed;
                }
            }
        }
    }
}

void checkDirectionFixtures(Results& r) {
    for (const GoldenDirectionFixture& f : kGoldenDirection) {
        const reflayout::ReferenceProbeDecode d =
            reflayout::decodeGlobalUv({static_cast<float>(f.uvx), static_cast<float>(f.uvy)});
        ++r.directionFixturesChecked;
        const std::string ctx = "direction(uv=" + std::to_string(f.uvx) + "," +
                                std::to_string(f.uvy) + ")";
        bool ok = true;
        if (!d.active) { r.fail(ctx, "active", 1, 0); ok = false; }
        const float dirError = std::max({
            std::abs(d.probeDirection.x - static_cast<float>(f.dx)),
            std::abs(d.probeDirection.y - static_cast<float>(f.dy)),
            std::abs(d.probeDirection.z - static_cast<float>(f.dz))});
        r.maxDirectionError = std::max(r.maxDirectionError, dirError);
        if (dirError > kGoldenEpsilon) { r.fail(ctx, "probeDir.x", f.dx, d.probeDirection.x); ok = false; }
        if (!close(d.thetaIndex, f.thetai, kGoldenEpsilon)) { r.fail(ctx, "thetaIndex", f.thetai, d.thetaIndex); ok = false; }
        if (!close(d.theta, f.theta, kGoldenEpsilon)) { r.fail(ctx, "theta", f.theta, d.theta); ok = false; }
        if (!close(d.phi, f.phi, kGoldenEpsilon)) { r.fail(ctx, "phi", f.phi, d.phi); ok = false; }
        const float weightError = std::max(
            std::abs(d.solidAngleWeight - static_cast<float>(f.saw)),
            std::abs(d.lambertWeight - static_cast<float>(f.lw)));
        r.maxWeightError = std::max(r.maxWeightError, weightError);
        if (weightError > kGoldenEpsilon) { r.fail(ctx, "solidAngleWeight", f.saw, d.solidAngleWeight); ok = false; }
        if (d.directionBinCount != f.binCount) { r.fail(ctx, "binCount", f.binCount, d.directionBinCount); ok = false; }
        if (d.directionBinIndex != f.binIndex) { r.fail(ctx, "binIndex", f.binIndex, d.directionBinIndex); ok = false; }
        // Finite, normalized, and on the chart-normal hemisphere.
        if (d.active) {
            const float len = glm::length(d.probeDirection);
            if (!std::isfinite(len) || std::abs(len - 1.0f) > 1.0e-4f) {
                r.fail(ctx, "dirLength", 1.0, len);
                ok = false;
            }
            const float hemi = glm::dot(d.probeDirection,
                                        reflayout::chart(d.chartId).normal);
            if (hemi <= 0.0f) { r.fail(ctx, "hemisphere", 1.0, hemi); ok = false; }
        }
        if (!ok) ++r.directionFixturesFailed;
    }
}

void checkIntervalsAndTransitions(Results& r) {
    float previous = -1.0f;
    for (const GoldenIntervalFixture& f : kGoldenIntervals) {
        ++r.intervalFixturesChecked;
        const float reach = reflayout::cascadeReach(static_cast<uint32_t>(f.cascade));
        if (!close(reach, f.reach, 0.0)) {
            r.fail("interval", "reach.c" + std::to_string(f.cascade), f.reach, reach);
            ++r.intervalFixturesFailed;
        }
        if (reflayout::cascadeProbeSize(static_cast<uint32_t>(f.cascade)) !=
            static_cast<uint32_t>(f.probeSize)) {
            r.fail("interval", "probeSize", f.probeSize,
                   reflayout::cascadeProbeSize(static_cast<uint32_t>(f.cascade)));
            ++r.intervalFixturesFailed;
        }
        if (reach <= previous) {
            r.fail("interval", "monotonic", previous, reach);
            ++r.intervalFixturesFailed;
        }
        previous = reach;
    }
    if (previous != reflayout::kC5Reach) {
        r.fail("interval", "c5Reach", reflayout::kC5Reach, previous);
        ++r.intervalFixturesFailed;
    }

    for (const GoldenTransitionFixture& f : kGoldenTransitions) {
        float minDist = -1.0f;
        float interval = -1.0f;
        reflayout::mergeTransition(static_cast<uint32_t>(f.cascade), minDist, interval);
        if (!close(minDist, f.minDist, 0.0) || !close(interval, f.interval, 0.0) ||
            !close(reflayout::mergeBase(static_cast<uint32_t>(f.cascade)), f.base, 0.0)) {
            r.fail("transition", "c" + std::to_string(f.cascade), f.minDist, minDist);
            ++r.transitionChecksFailed;
        }
    }
    // C0 special transition samples (independent of ray reach).
    const float base = reflayout::mergeBase(0);
    const struct { float d; float l; } samples[] = {
        {0.0f, 1.0f}, {0.5f * base, 0.75f}, {base, 0.5f},
        {1.5f * base, 0.25f}, {2.0f * base, 0.0f}, {3.0f * base, 0.0f}};
    for (const auto& s : samples) {
        const float l = reflayout::mergeLerp(0, s.d);
        if (!close(l, s.l, kGoldenEpsilon)) {
            r.fail("transition.c0", "lerp", s.l, l);
            ++r.transitionChecksFailed;
        }
    }
}

void checkHemisphereSums(Results& r) {
    for (const GoldenHemisphereSum& f : kGoldenHemisphere) {
        const uint32_t psize = static_cast<uint32_t>(f.probeSize);
        const uint32_t cascade = static_cast<uint32_t>(std::log2(psize)) - 1u;
        // Direction index (dx,dy) lives at texel (dx*probePositions + 0.5,
        // dy*probePositions + 0.5): angular resolution x spatial density.
        const float probePositions = 256.0f / static_cast<float>(psize);
        float sumSaw = 0.0f;
        float sumW = 0.0f;
        for (uint32_t j = 0; j < psize; ++j) {
            for (uint32_t i = 0; i < psize; ++i) {
                const glm::vec2 uv(static_cast<float>(i) * probePositions + 0.5f,
                                   256.0f * static_cast<float>(cascade) +
                                       static_cast<float>(j) * probePositions + 0.5f);
                const auto d = reflayout::decodeGlobalUv(uv);
                sumSaw += d.solidAngleWeight;
                sumW += d.solidAngleWeight * d.lambertWeight;
            }
        }
        const double relSaw = std::abs(sumSaw - f.solidAngleSum) / std::max(1.0, std::abs(f.solidAngleSum));
        const double relW = std::abs(sumW - f.cosineWeightedSum) / std::max(1.0, std::abs(f.cosineWeightedSum));
        if (relSaw > 1.0e-3 || relW > 1.0e-3) {
            r.fail("hemisphere.ps" + std::to_string(f.probeSize), "solidAngleSum",
                   f.solidAngleSum, sumSaw);
            ++r.hemisphereSumsFailed;
        }
    }
}

void checkRingCoverage(Results& r) {
    // Recompute bin coverage from the CPU oracle: every ring's bin indices must
    // form a complete permutation of 0..binCount-1.
    for (uint32_t p = 1; p <= 6; ++p) {
        const uint32_t psize = 1u << p;
        const uint32_t cascade = p - 1u;
        const float probePositions = 256.0f / static_cast<float>(psize);
        std::vector<std::vector<int>> bins;
        for (uint32_t j = 0; j < psize; ++j) {
            for (uint32_t i = 0; i < psize; ++i) {
                const glm::vec2 uv(static_cast<float>(i) * probePositions + 0.5f,
                                   256.0f * static_cast<float>(cascade) +
                                       static_cast<float>(j) * probePositions + 0.5f);
                const auto d = reflayout::decodeGlobalUv(uv);
                const int ring = static_cast<int>(d.thetaIndex);
                if (static_cast<int>(bins.size()) <= ring)
                    bins.resize(static_cast<size_t>(ring) + 1);
                bins[static_cast<size_t>(ring)].push_back(d.directionBinIndex);
            }
        }
        for (size_t ring = 0; ring < bins.size(); ++ring) {
            if (bins[ring].empty())
                continue;
            std::vector<int> idx = bins[ring];
            std::sort(idx.begin(), idx.end());
            for (size_t k = 0; k < idx.size(); ++k) {
                if (idx[k] != static_cast<int>(k)) {
                    r.fail("ring.ps" + std::to_string(psize), "coverage",
                           static_cast<double>(k), static_cast<double>(idx[k]));
                    ++r.ringCoverageFailed;
                    break;
                }
            }
        }
    }
}

void checkBlockers(Results& r) {
    const ReferenceCornellScene scene;
    for (const GoldenIntervalFixture& f : kGoldenIntervals) {
        const uint32_t c = static_cast<uint32_t>(f.cascade);
        const float reach = reflayout::cascadeReach(c);
        if (f.unbounded == 0) {
            // Inside: floor ray for short reaches, X-span ray for C4 (room is
            // 0.5 high; a 1.0 reach cannot start above the ceiling).
            if (c < 4) {
                const float hIn = reach * 0.99f;
                const auto hitIn = scene.trace({0.25f, hIn, 0.25f}, {0.0f, -1.0f, 0.0f}, reach);
                if (!hitIn.hit || hitIn.chartId != ReferenceChartId::Floor) {
                    r.fail("blocker.c" + std::to_string(c), "inside", 1, hitIn.hit ? 1 : 0);
                    ++r.blockerFailures;
                }
            } else {
                const auto hitIn = scene.trace({0.004f, 0.25f, 0.25f}, {1.0f, 0.0f, 0.0f}, reach);
                if (!hitIn.hit || hitIn.chartId != ReferenceChartId::WallX1 ||
                    hitIn.distance >= reach) {
                    r.fail("blocker.c4", "inside", 1, hitIn.hit ? 1 : 0);
                    ++r.blockerFailures;
                }
            }
            // Outside: nearest +X wall is 0.996 away for C0-C3 (miss), and C4
            // uses a diagonal ray that threads the interior-wall openings with
            // the nearest remaining surface beyond the reach.
            if (c < 4) {
                const auto hitOut = scene.trace({0.004f, 0.25f, 0.25f}, {1.0f, 0.0f, 0.0f}, reach);
                if (hitOut.hit || hitOut.materialKind != ReferenceMaterialKind::Sky) {
                    r.fail("blocker.c" + std::to_string(c), "outside", 0, hitOut.hit ? 1 : 0);
                    ++r.blockerFailures;
                }
            } else {
                const glm::vec3 dir = glm::normalize(glm::vec3(1.0f, 0.01f, 1.0f));
                const auto hitOut = scene.trace({0.005f, 0.005f, 0.005f}, dir, reach);
                if (hitOut.hit || hitOut.materialKind != ReferenceMaterialKind::Sky) {
                    r.fail("blocker.c4", "outside", 0, hitOut.hit ? 1 : 0);
                    ++r.blockerFailures;
                }
            }
        } else {
            const auto hit = scene.trace({0.25f, 0.25f, 0.25f}, {0.0f, -1.0f, 0.0f}, reach);
            if (!hit.hit || hit.distance >= reach) {
                r.fail("blocker.c5", "sceneReach", 1, hit.hit ? 1 : 0);
                ++r.blockerFailures;
            }
            // The entire parity scene fits within the C5 reach.
            constexpr double sceneDiagonal = 1.224744871391589;
            if (!(sceneDiagonal < reach)) {
                r.fail("blocker.c5", "diagonal", sceneDiagonal, reach);
                ++r.blockerFailures;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// GPU harness
// ---------------------------------------------------------------------------

struct alignas(16) GpuLayoutRecord {
    float probePos[4];
    float probeDir[4];
    float angles[4];
    float weights[4];
    float misc[4];
};
static_assert(sizeof(GpuLayoutRecord) == 80);

struct GpuSession {
    bool windowReady = false;
    GLuint shader = 0;
    GLuint requestBuffer = 0;
    GLuint recordBuffer = 0;

    ~GpuSession() {
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
    InitWindow(64, 64, "phase3-layout-validation");
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

bool loadDiagnosticShader(GpuSession& session, Results& r) {
    gl::setShaderRoot(RC3D_SHADER_ROOT);
    gl::clearShaderSourceRecords();
    session.shader = gl::loadComputeShader(
        gl::resolveShaderPath("reference_layout_diag.comp"), "reference_layout_diag.comp");
    r.shaderLoaded = session.shader != 0;
    if (!r.shaderLoaded)
        r.fail("gpu.shader", "reference_layout_diag.comp", 1, 0);
    countGlErrors(r, "gpu.shader");
    return r.shaderLoaded;
}

void runGpuDecode(GpuSession& session, Results& r,
                  const std::vector<glm::vec2>& requests) {
    const size_t count = requests.size();
    glGenBuffers(1, &session.requestBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, session.requestBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(count * sizeof(glm::vec2)),
                 requests.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &session.recordBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, session.recordBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 static_cast<GLsizeiptr>(count * sizeof(GpuLayoutRecord)),
                 nullptr, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, session.requestBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, session.recordBuffer);

    glUseProgram(session.shader);
    glUniform1i(glGetUniformLocation(session.shader, "uMode"), 0);
    glUniform1i(glGetUniformLocation(session.shader, "uRequestCount"),
                static_cast<GLint>(count));
    const GLuint groupsY = static_cast<GLuint>((count + 63) / 64);
    glDispatchCompute(1, groupsY, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

    std::vector<GpuLayoutRecord> records(count);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, session.recordBuffer);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                       static_cast<GLsizeiptr>(count * sizeof(GpuLayoutRecord)),
                       records.data());
    countGlErrors(r, "gpu.decode.dispatch");

    for (size_t i = 0; i < count; ++i) {
        const auto expected = reflayout::decodeGlobalUv(requests[i]);
        const GpuLayoutRecord& actual = records[i];
        ++r.coverageSamplesChecked;
        bool ok = true;
        const std::string ctx = "gpu.decode(uv=" + std::to_string(requests[i].x) + "," +
                                std::to_string(requests[i].y) + ")";
        if ((actual.angles[3] > 0.5f) != expected.active) {
            r.fail(ctx, "active", expected.active ? 1 : 0, actual.angles[3]);
            ok = false;
        }
        if (!expected.active) {
            if (!ok) ++r.coverageSamplesFailed;
            continue;
        }
        if (!close(actual.probePos[0], expected.probePosition.x, kGpuEpsilon) ||
            !close(actual.probePos[1], expected.probePosition.y, kGpuEpsilon) ||
            !close(actual.probePos[2], expected.probePosition.z, kGpuEpsilon) ||
            !close(actual.probePos[3], static_cast<float>(expected.probeSize), 0.0)) {
            r.fail(ctx, "probePos", expected.probePosition.x, actual.probePos[0]);
            ok = false;
        }
        if (!close(actual.probeDir[0], expected.probeDirection.x, kGpuEpsilon) ||
            !close(actual.probeDir[1], expected.probeDirection.y, kGpuEpsilon) ||
            !close(actual.probeDir[2], expected.probeDirection.z, kGpuEpsilon) ||
            !close(actual.probeDir[3], expected.maxTraceDistance, 0.0)) {
            r.fail(ctx, "probeDir", expected.probeDirection.x, actual.probeDir[0]);
            ok = false;
        }
        if (!close(actual.angles[0], expected.thetaIndex, kGpuEpsilon) ||
            !close(actual.angles[1], expected.theta, kGpuEpsilon) ||
            !close(actual.angles[2], expected.phi, kGpuEpsilon)) {
            r.fail(ctx, "angles", expected.theta, actual.angles[1]);
            ok = false;
        }
        if (!close(actual.weights[0], expected.solidAngleWeight, kGpuEpsilon) ||
            !close(actual.weights[1], expected.lambertWeight, kGpuEpsilon) ||
            !close(actual.weights[2], static_cast<float>(expected.chartId), 0.0) ||
            !close(actual.weights[3], static_cast<float>(expected.materialId), 0.0)) {
            r.fail(ctx, "weights", expected.solidAngleWeight, actual.weights[0]);
            ok = false;
        }
        if (!close(actual.misc[0], expected.physicalUv.x, kGpuEpsilon)) {
            r.fail(ctx, "misc.physicalX", expected.physicalUv.x, actual.misc[0]);
            ok = false;
        }
        if (!close(actual.misc[1], expected.physicalUv.y, kGpuEpsilon)) {
            r.fail(ctx, "misc.physicalY", expected.physicalUv.y, actual.misc[1]);
            ok = false;
        }
        if (!close(actual.misc[2], static_cast<float>(expected.directionBinCount), 0.0)) {
            r.fail(ctx, "misc.binCount", expected.directionBinCount, actual.misc[2]);
            ok = false;
        }
        if (!close(actual.misc[3], static_cast<float>(expected.directionBinIndex), 0.0)) {
            r.fail(ctx, "misc.binIndex", expected.directionBinIndex, actual.misc[3]);
            ok = false;
        }
        if (!ok) {
            ++r.coverageSamplesFailed;
            ++r.gpuDecodeFailures;
        }
    }
}

void runBandMarkers(GpuSession& session, Results& r, ReferenceRcAtlases& atlases) {
    std::vector<float> pixels(static_cast<size_t>(reflayout::kPhysicalWidth) *
                              reflayout::kPhysicalHeight * 4);
    for (uint32_t c = 0; c < reflayout::kCascadeCount; ++c) {
        const GLuint texture = atlases.writeTexture(c);
        glUseProgram(session.shader);
        glUniform1i(glGetUniformLocation(session.shader, "uMode"), 1);
        glUniform1i(glGetUniformLocation(session.shader, "uCascade"),
                    static_cast<GLint>(c));
        glUniform1f(glGetUniformLocation(session.shader, "uC0Log2Offset"), 0.0f);
        glBindImageTexture(2, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glDispatchCompute(reflayout::kPhysicalWidth / 8, reflayout::kPhysicalHeight / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        countGlErrors(r, "gpu.band.dispatch");

        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        countGlErrors(r, "gpu.band.readback");

        const GoldenBandStat& stat = kGoldenBandStats[c];
        int active = 0;
        int inactive = 0;
        bool reachOk = true;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i + 3] >= 0.0f) {
                ++active;
                if (!close(pixels[i + 3], stat.reach, 0.0))
                    reachOk = false;
            } else if (pixels[i + 3] == -1.0f) {
                ++inactive;
            } else {
                reachOk = false;
            }
        }
        if (active != stat.activeTexels) {
            r.fail("band.c" + std::to_string(c), "activeTexels", stat.activeTexels, active);
            ++r.bandStatFailures;
        }
        if (inactive != stat.inactiveTexels) {
            r.fail("band.c" + std::to_string(c), "inactiveTexels", stat.inactiveTexels, inactive);
            ++r.bandStatFailures;
        }
        if (!reachOk) {
            r.fail("band.c" + std::to_string(c), "reach", stat.reach, -1.0);
            ++r.bandStatFailures;
        }

        // Direction readback at every golden direction fixture for this cascade.
        for (const GoldenDirectionFixture& f : kGoldenDirection) {
            const auto expected = reflayout::decodeGlobalUv(
                {static_cast<float>(f.uvx), static_cast<float>(f.uvy)});
            if (!expected.active || expected.cascade != c)
                continue;
            const int tx = static_cast<int>(expected.physicalUv.x);
            const int ty = static_cast<int>(expected.physicalUv.y);
            const size_t o = (static_cast<size_t>(ty) * reflayout::kPhysicalWidth +
                              static_cast<size_t>(tx)) * 4;
            const glm::vec3 dir(pixels[o] * 2.0f - 1.0f,
                                pixels[o + 1] * 2.0f - 1.0f,
                                pixels[o + 2] * 2.0f - 1.0f);
            const float err = glm::length(dir - expected.probeDirection);
            if (err > 2.0e-4f) {
                r.fail("bandDir.c" + std::to_string(c), "dir", f.dx, dir.x);
                ++r.bandDirectionFailures;
            }
            if (!close(pixels[o + 3], expected.maxTraceDistance, 0.0)) {
                r.fail("bandDir.c" + std::to_string(c), "reach",
                       expected.maxTraceDistance, pixels[o + 3]);
                ++r.bandDirectionFailures;
            }
        }
    }
}

std::vector<glm::vec2> buildCoverageRequests() {
    std::vector<glm::vec2> requests;
    requests.reserve(8192);
    for (const GoldenLayoutFixture& f : kGoldenLayout)
        requests.emplace_back(static_cast<float>(f.uvx), static_cast<float>(f.uvy));
    for (const GoldenDirectionFixture& f : kGoldenDirection)
        requests.emplace_back(static_cast<float>(f.uvx), static_cast<float>(f.uvy));
    // Deterministic LCG coverage over the whole domain including out-of-range.
    uint32_t state = 0x9e3779b9u;
    auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 8) & 0xFFFFFF) / 16777216.0f;
    };
    for (int i = 0; i < 2048; ++i)
        requests.emplace_back((next() * 1.02f - 0.01f) * 1024.0f,
                              (next() * 1.02f - 0.01f) * 3072.0f);
    // Band/page boundary neighborhoods.
    for (float y = 127.5f; y < 3072.0f; y += 128.0f) {
        for (float x = 63.5f; x < 1024.0f; x += 128.0f)
            requests.emplace_back(x, y);
    }
    return requests;
}

bool writeReport(const std::string& path, const Results& r,
                 const std::vector<std::string>& notes) {
    const std::filesystem::path reportPath(path);
    std::error_code ec;
    if (reportPath.has_parent_path())
        std::filesystem::create_directories(reportPath.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (ec || !out)
        return false;

    const bool passed = r.passed();
    const bool g2 = r.layoutFixturesFailed == 0 && r.coverageSamplesFailed == 0 &&
                    r.bandStatFailures == 0 && r.glErrors == 0;
    const bool g3 = r.directionFixturesFailed == 0 && r.hemisphereSumsFailed == 0 &&
                    r.ringCoverageFailed == 0 && r.bandDirectionFailures == 0 &&
                    r.gpuDecodeFailures == 0;
    const bool g4 = r.intervalFixturesFailed == 0 && r.transitionChecksFailed == 0 &&
                    r.blockerFailures == 0;

    out << "{\n";
    out << "  \"schema_version\": \"reference-layout-report-v1\",\n";
    out << "  \"result\": \"" << (passed ? "PASS" : "FAIL") << "\",\n";
    out << "  \"gates\": {\n";
    out << "    \"G2-cascade-layout\": \"" << (g2 ? "PASS" : "FAIL") << "\",\n";
    out << "    \"G3-direction-mapping\": \"" << (g3 ? "PASS" : "FAIL") << "\",\n";
    out << "    \"G4-interval-contract\": \"" << (g4 ? "PASS" : "FAIL") << "\"\n";
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
    out << "  \"atlases\": {\n";
    out << "    \"cascades\": " << reflayout::kCascadeCount << ",\n";
    out << "    \"pairs\": " << reflayout::kCascadeCount << ",\n";
    out << "    \"format\": \"RGBA32F\",\n";
    out << "    \"physical_dimensions\": [" << reflayout::kPhysicalWidth << ", "
        << reflayout::kPhysicalHeight << "],\n";
    out << "    \"logical_domain\": [" << reflayout::kLogicalWidth << ", "
        << reflayout::kLogicalHeight << "],\n";
    out << "    \"allocated\": " << (r.atlasesAllocated ? "true" : "false") << "\n";
    out << "  },\n";
    out << "  \"metrics\": {\n";
    out << "    \"layout_fixtures_checked\": " << r.layoutFixturesChecked << ",\n";
    out << "    \"layout_fixtures_failed\": " << r.layoutFixturesFailed << ",\n";
    out << "    \"direction_fixtures_checked\": " << r.directionFixturesChecked << ",\n";
    out << "    \"direction_fixtures_failed\": " << r.directionFixturesFailed << ",\n";
    out << "    \"interval_fixtures_failed\": " << r.intervalFixturesFailed << ",\n";
    out << "    \"transition_checks_failed\": " << r.transitionChecksFailed << ",\n";
    out << "    \"hemisphere_sums_failed\": " << r.hemisphereSumsFailed << ",\n";
    out << "    \"ring_coverage_failed\": " << r.ringCoverageFailed << ",\n";
    out << "    \"coverage_samples_checked\": " << r.coverageSamplesChecked << ",\n";
    out << "    \"coverage_samples_failed\": " << r.coverageSamplesFailed << ",\n";
    out << "    \"gpu_decode_failures\": " << r.gpuDecodeFailures << ",\n";
    out << "    \"band_stat_failures\": " << r.bandStatFailures << ",\n";
    out << "    \"band_direction_failures\": " << r.bandDirectionFailures << ",\n";
    out << "    \"blocker_failures\": " << r.blockerFailures << ",\n";
    out << "    \"cleared_state_failures\": " << r.clearedStateFailures << ",\n";
    out << "    \"gl_errors\": " << r.glErrors << ",\n";
    out << "    \"max_probe_position_error\": " << r.maxAxisError << ",\n";
    out << "    \"max_direction_error\": " << r.maxDirectionError << ",\n";
    out << "    \"max_weight_error\": " << r.maxWeightError << "\n";
    out << "  },\n";
    out << "  \"reaches\": [";
    for (uint32_t c = 0; c < reflayout::kCascadeCount; ++c) {
        if (c > 0) out << ", ";
        out << reflayout::cascadeReach(c);
    }
    out << "],\n";
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
    out << "  ],\n";
    out << "  \"notes\": [";
    for (size_t i = 0; i < notes.size(); ++i) {
        if (i > 0) out << ", ";
        out << "\"" << jsonEscape(notes[i]) << "\"";
    }
    out << "]\n";
    out << "}\n";
    return out.good() && passed;
}

}  // namespace

bool runReferenceLayoutValidation(const std::string& reportPath) {
    Results r;

    // CPU golden checks (no GL required).
    checkLayoutFixtures(r);
    checkDirectionFixtures(r);
    checkIntervalsAndTransitions(r);
    checkHemisphereSums(r);
    checkRingCoverage(r);
    checkBlockers(r);

    // GPU session: allocation, GLSL decode cross-check, band readback evidence.
    GpuSession session;
    bool gpuOk = initGpuSession(session, r);
    if (gpuOk)
        gpuOk = loadDiagnosticShader(session, r);
    if (gpuOk) {
        const std::vector<glm::vec2> requests = buildCoverageRequests();
        runGpuDecode(session, r, requests);
    }

    ReferenceRcAtlases atlases;
    r.atlasesAllocated = gpuOk && atlases.allocate();
    if (r.atlasesAllocated) {
        for (uint32_t c = 0; c < reflayout::kCascadeCount; ++c) {
            if (!atlases.verifyClearedState(c, true) || !atlases.verifyClearedState(c, false)) {
                r.fail("atlases.c" + std::to_string(c), "clearedState", -1.0, 0.0);
                ++r.clearedStateFailures;
            }
        }
        runBandMarkers(session, r, atlases);
    } else {
        r.fail("atlases", "allocate", 1, 0);
    }

    std::vector<std::string> notes = {
        "Phase 3 layout kernel only: no radiance, feedback, or merge code is enabled.",
        "Direction and interval outputs are diagnostic values, not lighting.",
        "theta literal 3.14192653 preserved from CubeA.glsl by parity requirement."
    };
    const bool passed = writeReport(reportPath, r, notes);
    std::cout << "[PHASE3] layout report=" << reportPath
              << " result=" << (passed ? "PASS" : "FAIL") << "\n";
    return passed;
}
