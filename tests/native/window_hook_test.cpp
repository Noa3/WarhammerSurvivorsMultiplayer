#include "wsm/window_hook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
std::atomic<DWORD> callback_thread{0};
std::atomic<int> callback_count{0};
HWND window_handle;
DWORD owner_thread;
wsm::HookResult outcome = wsm::HookResult::failed;
wsm::HookResult repeat_outcome = wsm::HookResult::failed;
void callback(void*) {
    callback_thread.store(GetCurrentThreadId());
    ++callback_count;
}
LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
DWORD WINAPI run(void*) {
    outcome = wsm::run_window_hook(GetModuleHandleW(nullptr), window_handle, owner_thread,
                                   callback, nullptr, 3000);
    repeat_outcome = wsm::run_window_hook(GetModuleHandleW(nullptr), window_handle, owner_thread,
                                          callback, nullptr, 3000);
    PostMessageW(window_handle, WM_CLOSE, 0, 0);
    return 0;
}
}
#include "window_hook_advanced.cpp"
int main(int argc, char** argv) {
    WNDCLASSW wc{}; wc.lpfnWndProc = window_proc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WsmHookFixture";
    if (!RegisterClassW(&wc)) return 1;
    window_handle = CreateWindowW(wc.lpszClassName, L"fixture", WS_OVERLAPPEDWINDOW,
                                  0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window_handle) return 1;
    owner_thread = GetCurrentThreadId();
    // Invalid ownership must fail before arming; then the valid path can run.
    if (wsm::run_window_hook(wc.hInstance, window_handle, owner_thread + 1, callback, nullptr, 1)
        != wsm::HookResult::failed) return 1;
    if (argc > 1 && std::strcmp(argv[1], "advanced") == 0)
        return advanced_test(wc.hInstance, wc.lpszClassName);
    if (argc > 1) {
        auto result = wsm::run_window_hook(wc.hInstance, window_handle, owner_thread, callback, nullptr, 100);
        MSG queued{}; while (PeekMessageW(&queued, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&queued);
        if (result != wsm::HookResult::timeout || callback_count != 0) {
            std::puts("FAIL: no-pump timeout or late callback"); return 1;
        }
        DestroyWindow(window_handle);
        std::puts("PASS: timeout does not invoke a late callback"); return 0;
    }
    HANDLE worker = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
    if (!worker) return 1;
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    WaitForSingleObject(worker, 5000); CloseHandle(worker);
    if (outcome != wsm::HookResult::completed || repeat_outcome != wsm::HookResult::completed ||
        callback_thread != owner_thread || callback_count != 2) {
        std::printf("FAIL: outcome=%d repeat=%d owner=%lu callback=%lu count=%d error=%lu\n", static_cast<int>(outcome),
                    static_cast<int>(repeat_outcome), owner_thread, callback_thread.load(), callback_count.load(), GetLastError()); return 1;
    }
    std::printf("PASS: two WH_GETMESSAGE callbacks on HWND owner thread %lu; wrong thread refused\n", owner_thread);
    return 0;
}
