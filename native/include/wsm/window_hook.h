#pragma once
#include <windows.h>
namespace wsm {
// Calls may be repeated; overlapping or reentrant calls fail. Only messages for
// the given same-process window and this invocation can start the callback.
// Callback/context must remain valid until return. Timeout applies only before
// execution starts; a running callback is always allowed to finish. C++ callback
// exceptions are caught and reported as failed. The containing module is pinned
// for process lifetime; module must identify that code owner.
enum class HookResult { completed, timeout, failed };
HookResult run_window_hook(HMODULE module, HWND window, DWORD thread_id,
                           void (*callback)(void*), void* context, DWORD timeout_ms);
// Retain callback/context ownership across pre-execution timeouts until the
// window resumes or is destroyed. Intended for required main-thread cleanup.
HookResult run_window_hook_recovering(HMODULE module, HWND window, DWORD thread_id,
    void (*callback)(void*), void* context, DWORD timeout_ms, void (*on_timeout)(void*) = nullptr);
}
