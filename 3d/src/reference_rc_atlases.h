#ifndef REFERENCE_RC_ATLASES_H
#define REFERENCE_RC_ATLASES_H

#include <GL/glew.h>

#include <array>
#include <cstdint>

// Owns the six persistent atlas pairs required by the reference parity
// kernel. Each cascade owns a read/write pair of RGBA32F textures
// (physical per-cascade storage from plan 7.3; default 1024x512).
//
// Filter contract (Phase 12-A): plan §7.5 / G5 said NEAREST because alpha
// is first-hit distance or the negative sky sentinel. Production historically
// used GL_LINEAR to analogize ShaderToy cubemap textureLod, which interpolates
// all channels including distance. The runtime filter is therefore selectable
// (`RcAtlasFilter`) so G7/G9 can A/B the two policies; default remains LINEAR
// until that ruling is recorded in semantic_parity_differences.md.

enum class RcAtlasFilter { Linear, Nearest };

inline const char* rcAtlasFilterName(RcAtlasFilter filter) {
    return filter == RcAtlasFilter::Nearest ? "nearest" : "linear";
}

void setDefaultRcAtlasFilter(RcAtlasFilter filter);
RcAtlasFilter defaultRcAtlasFilter();

struct RcAtlasOccupancy {
    int total = 0;
    int inactive = 0;  // RGB zero AND alpha -1. Layout padding matches this
                       // exactly on parity. Zero-weight sky bins can collide.
    int active = 0;
};

RcAtlasOccupancy countAtlasOccupancy(GLuint texture, int width, int height);

class ReferenceRcAtlases final {
public:
    ReferenceRcAtlases();
    explicit ReferenceRcAtlases(RcAtlasFilter filter) : filter_(filter) {}
    // Legacy Cornell layout uses 1472x256 single-page physical storage.
    ReferenceRcAtlases(int physicalWidth, int physicalHeight,
                       RcAtlasFilter filter = RcAtlasFilter::Linear)
        : width_(physicalWidth), height_(physicalHeight), filter_(filter) {}
    ~ReferenceRcAtlases();
    ReferenceRcAtlases(const ReferenceRcAtlases&) = delete;
    ReferenceRcAtlases& operator=(const ReferenceRcAtlases&) = delete;

    bool allocate();
    void reset() noexcept;

    void setFilter(RcAtlasFilter filter) { filter_ = filter; }
    RcAtlasFilter filter() const { return filter_; }

    bool valid() const { return valid_; }
    GLuint readTexture(uint32_t cascade) const { return pairs_[cascade].read; }
    GLuint writeTexture(uint32_t cascade) const { return pairs_[cascade].write; }

    bool historyValid() const { return historyValid_; }
    void setHistoryValid(bool value) { historyValid_ = value; }
    uint64_t historyGeneration() const { return historyGeneration_; }

    // Temporal contract (plan 7.6): the swap occurs only after all six
    // current-frame cascades complete; a failed or skipped pass does not swap.
    // After the swap, read[C] exposes the just-completed generation and the
    // history generation increments exactly once.
    void swap();

    // Reset and revision invalidation: the read set returns to the locked
    // cleared state (RGB zero, alpha -1), history is invalid, and the
    // generation counter is preserved (it never advances without a swap).
    void invalidateHistory();

    // Cleared-state contract: RGB zero, alpha -1 (negative miss sentinel).
    bool verifyClearedState(uint32_t cascade, bool readSide) const;
    RcAtlasOccupancy occupancy(uint32_t cascade, bool readSide) const;
    int width() const { return width_; }
    int height() const { return height_; }

private:
    struct Pair {
        GLuint read = 0;
        GLuint write = 0;
    };
    std::array<Pair, 6> pairs_{};
    int width_ = 1024;
    int height_ = 512;
    RcAtlasFilter filter_ = RcAtlasFilter::Linear;
    bool valid_ = false;
    bool historyValid_ = false;
    uint64_t historyGeneration_ = 0;
};

#endif
