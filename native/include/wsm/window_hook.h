#pragma once
#include <windows.h>
namespace wsm {
// Calls may be repeated; overlapping or reentrant calls return busy. Only messages
// for the given same-process window and this invocation can start the callback.
// Callback/context must remain valid until return. Timeout applies only before
// execution starts; a running callback is always allowed to finish. C++ callback
// exceptions are caught and reported as failed. The containing module is pinned
// for process lifetime; module must identify that code owner.
enum class HookResult { completed, timeout, failed, busy };
HookResult run_window_hook(HMODULE module, HWND window, DWORD thread_id,
                           void (*callback)(void*), void* context, DWORD timeout_ms);
// Worker-only bounded admission retry. Only busy is retried; timeout_ms is the
// total pre-execution budget shared by admission and the window-message wait.
// Exhausted admission returns busy, while an unpumped window returns timeout.
// A callback that starts still owns its context until it actually completes.
// Calls from the target window thread fail immediately to avoid reentrant deadlock.
HookResult run_window_hook_waiting(HMODULE module, HWND window, DWORD thread_id,
    void (*callback)(void*), void* context, DWORD timeout_ms);
// Retain callback/context ownership across busy and pre-execution timeouts until
// the window resumes or is destroyed. on_timeout is called only for a timeout.
// Intended for required main-thread cleanup; do not call from the target thread.
HookResult run_window_hook_recovering(HMODULE module, HWND window, DWORD thread_id,
    void (*callback)(void*), void* context, DWORD timeout_ms, void (*on_timeout)(void*) = nullptr);
}
