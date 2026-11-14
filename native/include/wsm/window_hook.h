#pragma once
#include <windows.h>
namespace wsm {
// One invocation per process. The callback and its context must remain alive until
// process exit; an already-running callback cannot safely be cancelled.
enum class HookResult { completed, timeout, failed };
HookResult run_window_hook(HMODULE module, HWND window, DWORD thread_id,
                           void (*callback)(void*), void* context, DWORD timeout_ms);
}
