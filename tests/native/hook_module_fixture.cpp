#include "wsm/window_hook.h"

namespace {
void increment(void* context) { InterlockedIncrement(static_cast<LONG*>(context)); }
}
extern "C" __declspec(dllexport) int __cdecl fixture_hook(HWND window, DWORD thread, LONG* counter) {
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&fixture_hook), &self)) return 2;
    const auto result = wsm::run_window_hook(self, window, thread, increment, counter, 3000);
    FreeLibrary(self);
    return static_cast<int>(result);
}
