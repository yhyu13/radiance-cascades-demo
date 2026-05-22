// 2026-05-22 HDR-EXR honest metric (doc/7/hdr_exr_metric_impl.md).
// Thin C++ wrapper around tinyexr's SaveEXR isolated from raylib.h to
// avoid the winuser.h CloseWindow/ShowCursor linkage clash that triggers
// when tinyexr.h pulls <windows.h>.
#pragma once

#include <cstddef>

namespace exrw {

// Writes a 32-bit float RGB image as a single-part EXR (PIZ-compressed
// by tinyexr default). `rgb` is packed [r0,g0,b0,r1,g1,b1,...], row-major,
// top-left origin. Caller must Y-flip GL framebuffer data before calling.
// Returns true on success, false otherwise (logs to stderr).
bool save_rgb32f_exr(const char* path, const float* rgb, int width, int height);

}  // namespace exrw
