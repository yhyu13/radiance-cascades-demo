#include "reference_pt.h"

#include "reference_transport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace {

// PCG32 deterministic RNG (per-pixel seeded; identical across runs/threads).
struct Rng {
    uint64_t state;
    uint64_t inc;
    void seed(uint64_t s, uint64_t seq) {
        state = 0;
        inc = (seq << 1u) | 1u;
        next();
        state += s;
        next();
    }
    uint32_t next() {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }
    float nextFloat() {
        return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f);
    }
};

glm::vec3 cosineSampleHemisphere(const glm::vec3& normal, Rng& rng) {
    const float u1 = rng.nextFloat();
    const float u2 = rng.nextFloat();
    const float r = std::sqrt(u1);
    const float phi = 2.0f * 3.141592653f * u2;
    const float x = r * std::cos(phi);
    const float y = r * std::sin(phi);
    const float z = std::sqrt(std::max(0.0f, 1.0f - u1));
    glm::vec3 tangent = std::abs(normal.y) < 0.999f
        ? glm::normalize(glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f)))
        : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 bitangent = glm::cross(normal, tangent);
    return glm::normalize(tangent * x + bitangent * y + normal * z);
}

template <typename SceneT>
glm::vec3 shadePt(const SceneT& scene, glm::vec3 origin,
                  glm::vec3 direction, int maxBounces, Rng& rng,
                  uint64_t& rayCount, bool reflectiveZero) {
    glm::vec3 color(0.0f);
    glm::vec3 throughput(1.0f);
    for (int bounce = 0; bounce < maxBounces; ++bounce) {
        ++rayCount;
        const ReferenceTraceHit hit = scene.trace(origin, direction, 10000.0f);
        if (!hit.hit) {
            color += throughput * scene.getSkyLight(direction);
            break;
        }
        switch (hit.materialKind) {
            case ReferenceMaterialKind::BlackUncharted:
                return color;
            case ReferenceMaterialKind::Emissive:
                color += throughput * hit.reflectanceOrEmission;
                return color;
            case ReferenceMaterialKind::Reflective: {
                if (reflectiveZero)
                    return color;  // locked RC-kernel policy: zero contribution
                direction = glm::normalize(
                    direction - 2.0f * glm::dot(direction, hit.normal) * hit.normal);
                origin = hit.position + hit.normal * 0.001f;
                continue;
            }
            case ReferenceMaterialKind::Diffuse:
                break;
            case ReferenceMaterialKind::Sky:
                color += throughput * scene.getSkyLight(direction);
                return color;
        }

        // Diffuse: next-event estimation for the directional sun, then a
        // cosine-distributed continuation (normalized cosine measure).
        glm::vec3 normal = hit.normal;
        if (glm::dot(normal, direction) >= 0.0f)
            normal = -normal;
        const float ndl = glm::dot(normal, scene.sunDirection());
        if (ndl > 0.0f) {
            ++rayCount;
            const ReferenceTraceHit shadow =
                scene.trace(hit.position + normal * reftransport::kShadowBias,
                            scene.sunDirection(), reftransport::kShadowMaxDistance);
            if (!shadow.hit)
                color += throughput * hit.reflectanceOrEmission *
                         (scene.sunRadiance() * ndl);
        }
        throughput *= hit.reflectanceOrEmission;
        direction = cosineSampleHemisphere(normal, rng);
        origin = hit.position + normal * 0.001f;
    }
    return color;
}

}  // namespace

template <typename SceneT>
ReferencePtResult renderReferencePTImpl(const SceneT& scene,
                                        const ReferenceCamera& camera,
                                        const ReferencePtOptions& options) {
    ReferencePtResult result;
    result.width = options.width;
    result.height = options.height;
    result.samplesPerPixel = options.samplesPerPixel;
    result.pixels.assign(static_cast<size_t>(options.width) * options.height,
                         glm::vec3(0.0f));

    const auto start = std::chrono::steady_clock::now();
    std::atomic<uint64_t> rays{0};
    std::atomic<int> rowsDone{0};
    int threadCount = options.threads > 0
        ? options.threads
        : static_cast<int>(std::thread::hardware_concurrency());
    threadCount = std::max(1, std::min(threadCount, 64));

    const int rowsPerChunk = 8;
    std::atomic<int> nextRow{0};
    auto worker = [&]() {
        uint64_t localRays = 0;
        for (;;) {
            const int y0 = nextRow.fetch_add(rowsPerChunk);
            if (y0 >= options.height)
                break;
            const int y1 = std::min(options.height, y0 + rowsPerChunk);
            for (int y = y0; y < y1; ++y) {
                for (int x = 0; x < options.width; ++x) {
                    const size_t index = static_cast<size_t>(y) * options.width + x;
                    Rng rng;
                    rng.seed(options.seed, static_cast<uint64_t>(index));
                    const glm::vec2 ndc(
                        (static_cast<float>(x) + 0.5f) / options.width * 2.0f - 1.0f,
                        (static_cast<float>(y) + 0.5f) / options.height * 2.0f - 1.0f);
                    const glm::vec3 rayDir = camera.ray(ndc);
                    glm::vec3 sum(0.0f);
                    for (int s = 0; s < options.samplesPerPixel; ++s)
                        sum += shadePt(scene, camera.position, rayDir,
                                       options.maxBounces, rng, localRays,
                                       options.reflectiveZero);
                    result.pixels[index] =
                        sum / static_cast<float>(options.samplesPerPixel);
                }
            }
            rowsDone.fetch_add(1);
        }
        rays.fetch_add(localRays);
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(threadCount));
    for (int i = 0; i < threadCount; ++i)
        pool.emplace_back(worker);
    for (auto& t : pool)
        t.join();

    result.raysTraced = rays.load();
    result.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "[REFERENCE-PT] " << options.width << "x" << options.height
              << " spp=" << options.samplesPerPixel
              << " bounces=" << options.maxBounces
              << " rays=" << result.raysTraced
              << " seconds=" << result.seconds << "\n";
    return result;
}

ReferencePtResult renderReferencePT(const ReferenceCornellScene& scene,
                                    const ReferenceCamera& camera,
                                    const ReferencePtOptions& options) {
    return renderReferencePTImpl(scene, camera, options);
}

ReferencePtResult renderReferencePT(const ReferenceLegacyCornellScene& scene,
                                    const ReferenceCamera& camera,
                                    const ReferencePtOptions& options) {
    return renderReferencePTImpl(scene, camera, options);
}
