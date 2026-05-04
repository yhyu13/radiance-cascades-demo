// Phase 6b: Windows API shim for RenderDoc DLL loading.
// This TU MUST NOT include raylib.h or demo3d.h — those headers conflict with
// <windows.h> via winuser.h (CloseWindow / ShowCursor overload clash).
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "renderdoc_app.h"
#include "rdoc_helper.h"

bool rdoc_load_api(RENDERDOC_API_1_6_0** out_api) {
    HMODULE mod = GetModuleHandleA("renderdoc.dll");
    if (!mod)
        mod = LoadLibraryA("C:/Program Files/RenderDoc/renderdoc.dll");
    if (!mod)
        return false;
    pRENDERDOC_GetAPI getApi =
        (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
    if (!getApi)
        return false;
    return getApi(eRENDERDOC_API_Version_1_6_0, (void**)out_api) != 0;
}

#endif // _WIN32
