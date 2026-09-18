#pragma once
#include <windows.h>
#include "core/Window.h"
// Resolve ONLY the Window owned by this test's current thread. Calling GLFW
// directly from a plugin would use that DLL's separate static GLFW instance.
inline HWND WO05NativeWindow(Cosmic::Window& window)
{
    struct Search { Cosmic::Window* owner; HWND found; } search{&window,nullptr};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM context) -> BOOL
    {
        auto& search = *reinterpret_cast<Search*>(context);
        if (GetPropW(hwnd,L"CosmicWindowPtr")==search.owner)
        { search.found=hwnd; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.found;
}
