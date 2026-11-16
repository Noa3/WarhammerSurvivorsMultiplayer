#include "wsm/rewired_input_client.h"
#include <cwchar>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Case {
    std::unique_ptr<wsm::RewiredInputPump> pump;
    std::shared_ptr<wsm::RewiredInputMailbox> mailbox;
};
std::vector<std::unique_ptr<Case>> cases;
Case* current{};
std::string reason;
HWND window{};
BOOL CALLBACK find_window(HWND hwnd, LPARAM) {
    DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid);
    wchar_t name[128]{};
    if (pid == GetCurrentProcessId() && GetClassNameW(hwnd, name, 128) && std::wcscmp(name, L"UnityWndClass") == 0) {
        window = hwnd;
        return FALSE;
    }
    return TRUE;
}
}
extern "C" __declspec(dllexport) int __cdecl fixture_drive_start(int managed, unsigned long os, int mode) {
    reason.clear();
    if (current && current->mailbox && !current->mailbox->stopped()) { reason = "Prior driver is not closed"; return 1; }
    window = nullptr; EnumWindows(find_window, 0);
    if (!window || GetWindowThreadProcessId(window, nullptr) != os || os != GetCurrentThreadId()) return 2;
    ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
    auto game = GetModuleHandleW(L"GameAssembly.dll");
    HMODULE bridge{};
    wsm::Il2CppApi api;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&fixture_drive_start), &bridge) ||
        !wsm::Il2CppApi::resolve(game, api, reason) || !api.resolve_probe(game, reason)) return 3;
    auto value = std::make_unique<Case>();
    current = value.get(); cases.push_back(std::move(value));
    current->pump = std::make_unique<wsm::RewiredInputPump>();
    if (!current->pump->initialize(game, api, api.domain_get(), bridge, window, os, managed, reason)) return 4;
    wsm::RewiredInputClientConfig config;
    config.game = game; config.api = api; config.domain = api.domain_get();
    config.window = window; config.native_thread = os; config.managed_thread = managed; config.epoch = 1;
    config.options = {1, mode == 1 ? 999 : 7, 17, 23, 2, 9};
    if (mode == 2) config.window = GetDesktopWindow();
    if (mode == 3) ++config.native_thread;
    if (mode == 4) ++config.managed_thread;
    if (mode == 6) current->pump->test_fail_start(5);
    auto client = std::make_unique<wsm::RewiredInputClient>(config);
    current->mailbox = client->mailbox();
    return current->pump->start(std::move(client), reason) ? 0 : 5;
}
extern "C" __declspec(dllexport) int __cdecl fixture_drive_submit(unsigned long long epoch, unsigned long long sequence,
                                                                 float x, float y, unsigned duration) {
    return current && current->mailbox && current->mailbox->submit(epoch, sequence, x, y, GetTickCount64(), duration) ? 1 : 0;
}
extern "C" __declspec(dllexport) void __cdecl fixture_drive_stop() {
    if (current && current->mailbox) current->mailbox->stop();
}
// Fixture teardown after preserving an automatic-completion assertion; not a
// substitute for the producer-only stop behavior under test.
extern "C" __declspec(dllexport) void __cdecl fixture_drive_force_pump_stop() {
    if (current && current->pump) current->pump->stop();
}
extern "C" __declspec(dllexport) void __cdecl fixture_drive_drop_worker() {
    if (!current || !current->pump) return;
    auto wrapper = std::move(current->pump);
    std::thread producer([owned = std::move(wrapper)]() mutable { owned.reset(); });
    producer.join();
}
extern "C" __declspec(dllexport) unsigned long long __cdecl fixture_drive_read(int field) {
    if (!current || !current->mailbox) return 0;
    const auto driver = current->mailbox->status();
    const auto pump = current->pump ? current->pump->state() : wsm::RewiredInputPumpState{};
    switch (field) {
    case 0: return static_cast<unsigned>(driver.state);
    case 1: return driver.cleanup_pending;
    case 2: return static_cast<unsigned>(driver.neutrality);
    case 3: return driver.nonzero_attempts;
    case 4: return current->mailbox->stopped();
    case 5: return pump.cleanup_complete;
    case 6: return pump.subscription_pending;
    case 7: return !driver.error.empty();
    case 8: return GetForegroundWindow() == window;
    default: return 0;
    }
}
extern "C" __declspec(dllexport) const char* __cdecl fixture_drive_reason() { return reason.c_str(); }
