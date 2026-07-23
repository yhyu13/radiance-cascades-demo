#ifndef REFERENCE_RC_ATLASES_H
#define REFERENCE_RC_ATLASES_H

#include <GL/glew.h>

#include <array>
#include <cstdint>

// Phase 3: owns the six persistent atlas pairs required by the reference
// parity kernel. Each cascade owns a read texture and a write texture of
// 1024x512 RGBA32F texels (physical per-cascade storage from plan 7.3).
// Textures use nearest filtering: alpha carries first-hit distance or the
// negative sky sentinel and must never be linearly filtered.
// Phase 3 only allocates and labels the pairs and validates cleared state;
// transport is not implemented yet.

class ReferenceRcAtlases final {
public:
    ReferenceRcAtlases() = default;
    ~ReferenceRcAtlases();
    ReferenceRcAtlases(const ReferenceRcAtlases&) = delete;
    ReferenceRcAtlases& operator=(const ReferenceRcAtlases&) = delete;

    bool allocate();
    void reset() noexcept;

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

private:
    struct Pair {
        GLuint read = 0;
        GLuint write = 0;
    };
    std::array<Pair, 6> pairs_{};
    bool valid_ = false;
    bool historyValid_ = false;
    uint64_t historyGeneration_ = 0;
};

#endif
