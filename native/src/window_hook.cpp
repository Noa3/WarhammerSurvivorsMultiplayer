#include "wsm/window_hook.h"
#include <atomic>
namespace {
// 0 unused, 1 armed, 2 running, 3 complete, 4 expired.
std::atomic<int> state{0};
void (*target)(void*) = nullptr;
void* target_context = nullptr;
LRESULT CALLBACK hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0) {
        int expected = 1;
        if (state.compare_exchange_strong(expected, 2)) {
            target(target_context);
            state.store(3);
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}
}
namespace wsm {
HookResult run_window_hook(HMODULE module, HWND window, DWORD thread_id,
                           void (*callback)(void*), void* context, DWORD timeout_ms) {
    DWORD pid = 0;
    if (!window || !thread_id || !callback || GetWindowThreadProcessId(window, &pid) != thread_id ||
        pid != GetCurrentProcessId()) return HookResult::failed;
    int expected = 0;
    if (!state.compare_exchange_strong(expected, 4)) return HookResult::failed;
    target = callback; target_context = context;
    state.store(1);
    HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, hook_proc, module, thread_id);
    if (!hook) { state.store(4); return HookResult::failed; }
    if (!PostMessageW(window, WM_NULL, 0, 0)) {
        expected = 1;
        bool cancelled = state.compare_exchange_strong(expected, 4);
        UnhookWindowsHookEx(hook);
        while (state.load() == 2) Sleep(10);
        return !cancelled && state.load() == 3 ? HookResult::completed : HookResult::failed;
    }
    ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (state.load() == 1 && GetTickCount64() < deadline) Sleep(10);
    expected = 1;
    bool expired = state.compare_exchange_strong(expected, 4);
    UnhookWindowsHookEx(hook);
    // The context is process-lifetime; don't report timeout while a callback is
    // executing, because its outcome could otherwise arrive after the timeout.
    while (state.load() == 2) Sleep(10);
    return expired ? HookResult::timeout : (state.load() == 3 ? HookResult::completed : HookResult::failed);
}
}
