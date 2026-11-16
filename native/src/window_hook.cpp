#include "wsm/window_hook.h"
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace {
enum class State { idle, armed, running, completed, failed };
std::mutex invocation_mutex;
std::mutex state_mutex;
std::condition_variable state_changed;
State state = State::idle;
HWND target_window = nullptr;
UINT wake_message = 0;
WPARAM generation = 0;
void (*target)(void*) = nullptr;
void* target_context = nullptr;
bool deadline_active = false;
std::chrono::steady_clock::time_point target_deadline;

LRESULT CALLBACK hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    void (*callback)(void*) = nullptr;
    void* context = nullptr;
    if (code >= 0 && wparam == PM_REMOVE && lparam) {
        const auto* message = reinterpret_cast<const MSG*>(lparam);
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (state == State::armed && message->hwnd == target_window &&
                message->message == wake_message && message->wParam == generation &&
                (!deadline_active || std::chrono::steady_clock::now() < target_deadline)) {
                state = State::running;
                callback = target;
                context = target_context;
            }
        }
        if (callback) {
            bool success = false;
            try {
                callback(context);
                success = true;
            } catch (...) {
                // Never let a C++ exception cross the Windows callback boundary.
            }
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                state = success ? State::completed : State::failed;
            }
            state_changed.notify_one();
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}
}

namespace wsm {
static HookResult run_hook(HMODULE module, HWND window, DWORD thread_id,
    void (*callback)(void*), void* context, DWORD timeout_ms,
    const std::chrono::steady_clock::time_point* deadline) {
    DWORD pid = 0;
    if (!window || !thread_id || !callback || GetWindowThreadProcessId(window, &pid) != thread_id ||
        pid != GetCurrentProcessId()) return HookResult::failed;

    // Unhooking does not wait for a hook procedure already chaining through the
    // OS. Pin the actual code owner, not just an assumed caller-supplied handle.
    // Validate before admission so an invalid module is never reported as busy.
    HMODULE code_owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&hook_proc), &code_owner) || code_owner != module)
        return HookResult::failed;
    std::unique_lock<std::mutex> invocation_lock(invocation_mutex, std::try_to_lock);
    if (!invocation_lock.owns_lock()) return HookResult::busy;
    if (deadline && std::chrono::steady_clock::now() >= *deadline) return HookResult::busy;
    const UINT message = RegisterWindowMessageW(L"WSM.WindowHook.Wake.v1");
    if (!message) return HookResult::failed;

    std::unique_lock<std::mutex> lock(state_mutex);
    // Never reuse a nonce, because an old post may survive.
    if (generation == (std::numeric_limits<WPARAM>::max)()) return HookResult::failed;
    ++generation;
    target_window = window;
    wake_message = message;
    target = callback;
    target_context = context;
    deadline_active = deadline != nullptr;
    if (deadline) target_deadline = *deadline;
    state = State::armed;
    HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, hook_proc, module, thread_id);
    if (!hook) {
        state = State::idle;
        target = nullptr;
        target_context = nullptr;
        target_window = nullptr;
        return HookResult::failed;
    }
    const bool posted = PostMessageW(window, message, generation, 0) != FALSE;
    if (posted) {
        if (deadline) {
            state_changed.wait_until(lock, *deadline, [] { return state != State::armed; });
        } else {
            state_changed.wait_for(lock, std::chrono::milliseconds(timeout_ms), [] {
                return state != State::armed;
            });
        }
    }
    const bool expired = posted && state == State::armed;
    if (state == State::armed) state = State::idle;
    // Once started, the callback must finish before its caller's context dies.
    state_changed.wait(lock, [] { return state != State::running; });
    const HookResult result = expired ? HookResult::timeout :
        (state == State::completed ? HookResult::completed : HookResult::failed);
    state = State::idle;
    target = nullptr;
    target_context = nullptr;
    target_window = nullptr;
    lock.unlock();
    UnhookWindowsHookEx(hook);
    return result;
}
HookResult run_window_hook(HMODULE module, HWND window, DWORD thread_id,
    void (*callback)(void*), void* context, DWORD timeout_ms) {
    return run_hook(module, window, thread_id, callback, context, timeout_ms, nullptr);
}
HookResult run_window_hook_waiting(HMODULE module, HWND window, DWORD thread_id,
    void (*callback)(void*), void* context, DWORD timeout_ms) {
    if (thread_id == GetCurrentThreadId()) return HookResult::failed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const auto result = run_hook(module, window, thread_id, callback, context, timeout_ms, &deadline);
        if (result != HookResult::busy) return result;
        if (std::chrono::steady_clock::now() >= deadline) return HookResult::busy;
        Sleep(1);
        if (std::chrono::steady_clock::now() >= deadline) return HookResult::busy;
    }
}
HookResult run_window_hook_recovering(HMODULE module, HWND window, DWORD thread_id,
    void (*callback)(void*), void* context, DWORD timeout_ms, void (*on_timeout)(void*)) {
    while (IsWindow(window)) {
        const auto result = run_window_hook(module, window, thread_id, callback, context, timeout_ms);
        if (result != HookResult::timeout && result != HookResult::busy) return result;
        if (result == HookResult::timeout && on_timeout) on_timeout(context);
        Sleep(10);
    }
    return HookResult::failed;
}
}
