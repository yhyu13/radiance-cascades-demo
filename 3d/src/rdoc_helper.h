#pragma once
// Phase 6b: thin shim isolating Windows API calls (LoadLibrary / GetProcAddress)
// from TUs that also include raylib.h (which declares CloseWindow / ShowCursor
// incompatibly with winuser.h).
//
// The 'renderdoc_app.h' header does NOT include <windows.h>, so it is safe to
// include in both this header and in demo3d.h.

#include "renderdoc_app.h"

#ifdef _WIN32
// Load the RenderDoc in-process API from renderdoc.dll.
// Returns true on success and writes the API pointer to *out_api.
// Returns false (and leaves *out_api unchanged) if RenderDoc is not installed.
bool rdoc_load_api(RENDERDOC_API_1_6_0** out_api);
#endif
