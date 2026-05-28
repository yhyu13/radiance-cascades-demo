// 2026-05-22 HDR-EXR honest metric (doc/7/hdr_exr_metric_impl.md).
// Isolated TU for the tinyexr header so its <windows.h> include does NOT
// reach raylib.h (would clash on CloseWindow / ShowCursor; same isolation
// rationale as rdoc_helper.cpp).

// Silence MSVC strict warnings (set in CMake /W4 /WX-) for the third-party
// implementation block. Our own thin wrapper at the bottom is small.
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#define TINYEXR_USE_STB_ZLIB 0
#define TINYEXR_USE_THREAD 0
#include "tinyexr.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "exr_writer.h"

#include <cstdio>

namespace exrw {

bool save_rgb32f_exr(const char* path, const float* rgb, int width, int height) {
    if (!path || !rgb || width <= 0 || height <= 0) return false;
    const char* err = nullptr;
    int ret = SaveEXR(rgb, width, height, /*components=*/3,
                      /*save_as_fp16=*/0, path, &err);
    if (ret != TINYEXR_SUCCESS) {
        std::fprintf(stderr, "[exrw] SaveEXR(%s) failed ret=%d err=%s\n",
                     path, ret, err ? err : "(none)");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    return true;
}

bool save_rgba32f_exr(const char* path, const float* rgba, int width, int height) {
    if (!path || !rgba || width <= 0 || height <= 0) return false;
    const char* err = nullptr;
    int ret = SaveEXR(rgba, width, height, /*components=*/4,
                      /*save_as_fp16=*/0, path, &err);
    if (ret != TINYEXR_SUCCESS) {
        std::fprintf(stderr, "[exrw] SaveEXR(%s) failed ret=%d err=%s\n",
                     path, ret, err ? err : "(none)");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    return true;
}

}  // namespace exrw
