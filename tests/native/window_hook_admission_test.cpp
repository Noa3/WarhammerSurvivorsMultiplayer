#include "wsm/window_hook.h"
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
struct Fixture {
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HWND window = nullptr;
    DWORD owner = 0;
    std::atomic<int> calls{0};
    std::atomic<DWORD> callback_thread{0};
    std::atomic<int> reentry{-1};
    std::atomic<int> recovery_timeouts{0};
};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
void count(void* data) {
    auto& f = *static_cast<Fixture*>(data);
    ++f.calls;
    f.callback_thread = GetCurrentThreadId();
}
#ifndef LEGACY_BASELINE
struct Count { std::atomic<int> calls{0}; std::atomic<DWORD> thread{0}; };
void count_separate(void* data) {
    auto& c = *static_cast<Count*>(data);
    ++c.calls;
    c.thread = GetCurrentThreadId();
}
void timed_out(void* data) { ++static_cast<Fixture*>(data)->recovery_timeouts; }
#endif
void hold(void* data) {
    auto& f = *static_cast<Fixture*>(data);
#ifndef LEGACY_BASELINE
    const auto start = GetTickCount64();
    const auto result = wsm::run_window_hook_waiting(GetModuleHandleW(nullptr), f.window, f.owner, count, &f, 1000);
    f.reentry = result == wsm::HookResult::failed && GetTickCount64() - start < 500 ? 1 : 0;
#endif
    SetEvent(f.entered);
    WaitForSingleObject(f.release, 5000);
}
}
int main() {
    Fixture f;
    if (!f.ready || !f.entered || !f.release) return 2;
    std::thread target([&] {
        f.owner = GetCurrentThreadId();
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = procedure;
        cls.lpszClassName = L"WSMAdmissionOwnedFixture";
        if (RegisterClassW(&cls))
            f.window = CreateWindowW(cls.lpszClassName, L"fixture", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
        SetEvent(f.ready);
        if (f.window) { MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg); }
    });
    if (WaitForSingleObject(f.ready, 5000) != WAIT_OBJECT_0 || !f.window) {
        target.join(); return 2;
    }
    const auto module = GetModuleHandleW(nullptr);
    auto first_result = wsm::HookResult::failed;
    std::thread first([&] { first_result = wsm::run_window_hook(module, f.window, f.owner, hold, &f, 3000); });
    bool ok = WaitForSingleObject(f.entered, 4000) == WAIT_OBJECT_0;
    const auto direct = wsm::run_window_hook(module, f.window, f.owner, count, &f, 20);
    // Value 3 is the new busy ordinal, so the baseline exercises the old binary behavior.
    const bool direct_busy = direct == static_cast<wsm::HookResult>(3);
    std::printf("direct contention result=%d expected busy=3: %s\n", static_cast<int>(direct), direct_busy ? "PASS" : "FAIL");
    ok = ok && direct_busy;
#ifndef LEGACY_BASELINE
    const auto invalid = wsm::run_window_hook_waiting(module, f.window, f.owner + 1, count, &f, 100);
    const auto bad_module = wsm::run_window_hook_waiting(nullptr, f.window, f.owner, count, &f, 100);
    const auto start = GetTickCount64();
    const auto deadline = wsm::run_window_hook_waiting(module, f.window, f.owner, count, &f, 80);
    const auto elapsed = GetTickCount64() - start;
    const bool denied = invalid == wsm::HookResult::failed && bad_module == wsm::HookResult::failed &&
        deadline == wsm::HookResult::busy && elapsed >= 60 && elapsed < 1000 && f.calls == 0;
    std::printf("invalid identity/module, deadline and reentry: %s (elapsed=%llu ms)\n", denied && f.reentry == 1 ? "PASS" : "FAIL", elapsed);
    ok = ok && denied && f.reentry == 1;
    auto waiting = wsm::HookResult::failed;
    auto recovering = wsm::HookResult::failed;
    Count waiting_count;
    std::atomic<bool> started{false}, recovery_started{false};
    std::thread second([&] { started = true; waiting = wsm::run_window_hook_waiting(module, f.window, f.owner, count_separate, &waiting_count, 3000); });
    std::thread recovery([&] { recovery_started = true; recovering = wsm::run_window_hook_recovering(module, f.window, f.owner, count, &f, 3000, timed_out); });
    while (!started.load() || !recovery_started.load()) Sleep(1);
    Sleep(80);
    const bool no_false_timeout = f.recovery_timeouts == 0;
    SetEvent(f.release);
    second.join();
    recovery.join();
    const bool completed = waiting == wsm::HookResult::completed && recovering == wsm::HookResult::completed &&
        f.calls == 1 && f.callback_thread == f.owner && waiting_count.calls == 1 && waiting_count.thread == f.owner &&
        no_false_timeout && f.recovery_timeouts == 0;
    Sleep(100);
    const bool no_late = f.calls == 1 && waiting_count.calls == 1;
    std::printf("waiting and recovery exactly once on owner; busy is not timeout; no late callback: %s\n", completed && no_late ? "PASS" : "FAIL");
    ok = ok && completed && no_late;
#else
    SetEvent(f.release);
#endif
    first.join();
    ok = ok && first_result == wsm::HookResult::completed;
#ifndef LEGACY_BASELINE
    ResetEvent(f.entered); ResetEvent(f.release);
    std::atomic<bool> returned{false};
    auto running_result = wsm::HookResult::failed;
    std::thread running([&] {
        running_result = wsm::run_window_hook_waiting(module, f.window, f.owner, hold, &f, 100);
        returned = true;
    });
    const bool running_started = WaitForSingleObject(f.entered, 2000) == WAIT_OBJECT_0;
    Sleep(200);
    const bool still_owned = !returned.load();
    SetEvent(f.release); running.join();
    const bool running_ok = running_started && still_owned && running_result == wsm::HookResult::completed;
    std::printf("waiting retains running callback beyond deadline: %s\n", running_ok ? "PASS" : "FAIL");
    ok = ok && running_ok;
#endif
    PostMessageW(f.window, WM_CLOSE, 0, 0); target.join();
#ifndef LEGACY_BASELINE
    WNDCLASSW cls{}; cls.hInstance = module; cls.lpfnWndProc = procedure; cls.lpszClassName = L"WSMAdmissionNoPump";
    const bool registered = RegisterClassW(&cls) != 0;
    HWND idle = registered ? CreateWindowW(cls.lpszClassName, L"idle", WS_OVERLAPPED, 0, 0, 100, 100, nullptr, nullptr, module, nullptr) : nullptr;
    auto timeout = wsm::HookResult::failed;
    Count idle_count;
    const DWORD idle_owner = GetCurrentThreadId();
    std::thread no_pump([&] { timeout = wsm::run_window_hook_waiting(module, idle, idle_owner, count_separate, &idle_count, 80); });
    no_pump.join();
    MSG pending{};
    while (PeekMessageW(&pending, idle, 0, 0, PM_REMOVE)) DispatchMessageW(&pending);
    const bool timeout_ok = idle && timeout == wsm::HookResult::timeout && idle_count.calls == 0;
    std::printf("waiting unpumped timeout and no late callback: %s\n", timeout_ok ? "PASS" : "FAIL");
    ok = ok && timeout_ok;
    if (idle) DestroyWindow(idle);
#endif
    CloseHandle(f.ready); CloseHandle(f.entered); CloseHandle(f.release);
    return ok ? 0 : 1;
}
