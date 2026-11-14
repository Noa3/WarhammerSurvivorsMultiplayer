// Included by window_hook_test.cpp to keep this fixture in the existing target.
namespace {
struct AdvancedCall {
    void (*function)(void*);
    void* context;
    DWORD timeout;
    std::atomic<bool> returned{false};
    wsm::HookResult result = wsm::HookResult::failed;
    ULONGLONG elapsed = 0;
};
DWORD WINAPI advanced_worker(void* parameter) {
    auto& call = *static_cast<AdvancedCall*>(parameter);
    const auto start = GetTickCount64();
    call.result = wsm::run_window_hook(GetModuleHandleW(nullptr), window_handle, owner_thread,
                                       call.function, call.context, call.timeout);
    call.elapsed = GetTickCount64() - start;
    call.returned.store(true);
    return 0;
}
void counted(void* parameter) { ++*static_cast<std::atomic<int>*>(parameter); }
void slow(void* parameter) { Sleep(180); counted(parameter); }
void throws_exception(void*) { throw std::runtime_error("fixture exception"); }

bool wait_message(UINT message, HWND window, MSG& msg) {
    const ULONGLONG end = GetTickCount64() + 3000;
    while (GetTickCount64() < end) {
        if (PeekMessageW(&msg, window, message, message, PM_NOREMOVE)) return true;
        Sleep(1);
    }
    return false;
}
bool finish_call(HANDLE worker, AdvancedCall& call) {
    const ULONGLONG end = GetTickCount64() + 4000;
    MSG msg{};
    while (!call.returned.load() && GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(1);
    }
    const bool finished = WaitForSingleObject(worker, 1000) == WAIT_OBJECT_0;
    CloseHandle(worker);
    return finished;
}
int advanced_test(HINSTANCE instance, const wchar_t* class_name) {
    std::atomic<int> first{0}, second{0};
    const UINT wake = RegisterWindowMessageW(L"WSM.WindowHook.Wake.v1");
    HWND other = CreateWindowW(class_name, L"other", WS_OVERLAPPEDWINDOW,
                               0, 0, 100, 100, nullptr, nullptr, instance, nullptr);
    if (!wake || !other) { std::puts("FAIL: advanced fixture setup"); return 1; }
    DWORD desktop_pid = 0;
    DWORD desktop_thread = GetWindowThreadProcessId(GetDesktopWindow(), &desktop_pid);
    if (desktop_pid == GetCurrentProcessId() ||
        wsm::run_window_hook(instance, GetDesktopWindow(), desktop_thread, counted, &first, 1) != wsm::HookResult::failed ||
        wsm::run_window_hook(instance, other, owner_thread + 1, counted, &first, 1) != wsm::HookResult::failed) {
        std::puts("FAIL: ownership validation"); return 1;
    }

    // Leave this invocation's wake message in the queue, then retry with a new
    // stack context. The stale wake must not execute the retry's callback.
    if (wsm::run_window_hook(instance, window_handle, owner_thread, counted, &first, 40) != wsm::HookResult::timeout) {
        std::puts("FAIL: initial no-pump timeout"); return 1;
    }
    PostMessageW(window_handle, WM_NULL, 0, 0);
    PostMessageW(window_handle, wake, 0, 0); // nonce zero is never issued
    AdvancedCall retry{counted, &second, 3000};
    HANDLE worker = CreateThread(nullptr, 0, advanced_worker, &retry, 0, nullptr);
    if (!worker) return 1;
    Sleep(100); // Give the worker time to arm while the queue remains unpumped.
    MSG msg{};
    bool ok = PeekMessageW(&msg, window_handle, wake, wake, PM_REMOVE) != FALSE && first == 0 && second == 0;
    ok = ok && PeekMessageW(&msg, window_handle, WM_NULL, WM_NULL, PM_REMOVE) != FALSE && second == 0;
    ok = ok && PeekMessageW(&msg, window_handle, wake, wake, PM_REMOVE) != FALSE && second == 0;
    // The real wake is now at the front. PM_NOREMOVE must not start it.
    ok = ok && wait_message(wake, window_handle, msg) && second == 0;
    const WPARAM nonce = msg.wParam;
    PostMessageW(other, wake, nonce, 0);
    ok = ok && PeekMessageW(&msg, other, wake, wake, PM_REMOVE) != FALSE && second == 0;
    const auto overlap = wsm::run_window_hook(instance, window_handle, owner_thread, counted, &first, 1);
    ok = ok && overlap == wsm::HookResult::failed;
    ok = finish_call(worker, retry) && ok && retry.result == wsm::HookResult::completed && first == 0 && second == 1;
    if (!ok) { std::puts("FAIL: stale/noise/PM_NOREMOVE/window filter or overlapping invocation"); return 1; }

    AdvancedCall delayed{slow, &second, 50};
    worker = CreateThread(nullptr, 0, advanced_worker, &delayed, 0, nullptr);
    if (!worker || !finish_call(worker, delayed) || delayed.result != wsm::HookResult::completed ||
        delayed.elapsed < 180 || second != 2) {
        std::puts("FAIL: running callback must finish after deadline"); return 1;
    }
    AdvancedCall throwing{throws_exception, nullptr, 3000};
    worker = CreateThread(nullptr, 0, advanced_worker, &throwing, 0, nullptr);
    if (!worker || !finish_call(worker, throwing) || throwing.result != wsm::HookResult::failed) {
        std::puts("FAIL: C++ callback exception must be contained"); return 1;
    }
    AdvancedCall after_exception{counted, &second, 3000};
    worker = CreateThread(nullptr, 0, advanced_worker, &after_exception, 0, nullptr);
    if (!worker || !finish_call(worker, after_exception) || after_exception.result != wsm::HookResult::completed || second != 3) {
        std::puts("FAIL: retry after callback exception"); return 1;
    }
    DestroyWindow(other);
    std::puts("PASS: timeout/retry, stale nonce, noise, wrong window, PM_NOREMOVE, overlap, slow and throwing callbacks");
    return 0;
}
}
