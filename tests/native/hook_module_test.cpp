#include "wsm/window_hook.h"
#include <atomic>
#include <cstdio>
#include <thread>

LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wp, lp);
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) { std::puts("FAIL: fixture module could not load"); return 1; }
    auto invoke = reinterpret_cast<int (__cdecl*)(HWND, DWORD, LONG*)>(GetProcAddress(module, "fixture_hook"));
    if (!invoke) return 1;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready) return 1;
    std::atomic<HWND> window{nullptr};
    std::atomic<DWORD> owner{0};
    std::thread ui([&] {
        WNDCLASSW klass{}; klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpfnWndProc = procedure; klass.lpszClassName = L"WsmHookModuleLifetimeFixture";
        if (RegisterClassW(&klass)) {
            window = CreateWindowW(klass.lpszClassName, L"fixture", WS_OVERLAPPEDWINDOW,
                                    0, 0, 64, 64, nullptr, nullptr, klass.hInstance, nullptr);
            owner = GetCurrentThreadId();
        }
        SetEvent(ready);
        if (!window.load()) return;
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
    });
    if (WaitForSingleObject(ready, 3000) != WAIT_OBJECT_0 || !window.load()) {
        std::puts("FAIL: fixture owner thread setup"); ExitProcess(1);
    }
    CloseHandle(ready);
    LONG count = 0;
    const int result = invoke(window.load(), owner.load(), &count);
    PostMessageW(window.load(), WM_CLOSE, 0, 0);
    ui.join();
    if (result != static_cast<int>(wsm::HookResult::completed) || count != 1) return 1;
    // Windows can cache hook modules while their target thread is alive. End
    // that thread before testing the stronger process-lifetime pin contract.
    if (!FreeLibrary(module)) return 1;
    if (GetModuleHandleA("wsm_hook_module_fixture.dll") != module) {
        std::puts("FAIL: scheduler module was unloadable after hook return"); return 1;
    }
    std::puts("PASS: scheduler pins its actual containing module for process lifetime");
    return 0;
}
