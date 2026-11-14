#include "wsm/ingame_ui.h"
#include <windows.h>
#include <cstdio>
#include <string>

namespace {
wsm::InGameMenu menu;
int counts[3] = {};
int last_port = 0;
std::string last_address;
HANDLE tick_event = nullptr;
ULONGLONG next_tick = 0;
void host(void*, int port) { ++counts[0]; last_port = port; }
void join(void*, const std::string& address, int port) { ++counts[1]; last_address = address; last_port = port; }
void leave(void*) { ++counts[2]; }
std::string status(void*, const std::string& language) { return language == "de" ? "Fixture: keine Netzwerkverbindung" : "Fixture: no network connection"; }
wsm::InGameMenuActions actions{nullptr, host, join, leave, status};
BOOL CALLBACK find_window(HWND hwnd, LPARAM p) {
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    wchar_t cls[128]{};
    if (pid == GetCurrentProcessId() && GetClassNameW(hwnd, cls, 128) && wcscmp(cls, L"UnityWndClass") == 0) {
        *reinterpret_cast<HWND*>(p) = hwnd; return FALSE;
    }
    return TRUE;
}
}
extern "C" __declspec(dllexport) int __cdecl fixture_init() {
    auto module = GetModuleHandleW(L"GameAssembly.dll"); wsm::Il2CppApi api; std::string error; HWND hwnd = nullptr;
    EnumWindows(find_window, reinterpret_cast<LPARAM>(&hwnd));
    if (!module || !hwnd || !wsm::Il2CppApi::resolve(module, api, error) || !api.resolve_probe(module, error)) { printf("MENU_NATIVE api/window error: %s\n", error.c_str()); return 1; }
    if (!menu.initialize(module, api, api.domain_get(), hwnd, error)) { printf("MENU_NATIVE initialize error: %s\n", error.c_str()); fflush(stdout); return 2; }
    return 0;
}
extern "C" __declspec(dllexport) void __cdecl fixture_tick() {
    const bool quick_test = GetEnvironmentVariableW(L"WSM_MENU_QUICK_INPUT_TEST", nullptr, 0) != 0;
    if (quick_test && GetTickCount64() < next_tick) return;
    menu.tick(actions);
    if (quick_test) {
        next_tick = GetTickCount64() + 250;
        if (!tick_event) {
            wchar_t name[256]{};
            const DWORD size = GetEnvironmentVariableW(L"WSM_MENU_TICK_EVENT", name, 256);
            if (size && size < 256) tick_event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
        }
        if (tick_event) SetEvent(tick_event);
    }
}
extern "C" __declspec(dllexport) void __cdecl fixture_fail_language_label() { menu.test_fail_next_language_label(); }
extern "C" __declspec(dllexport) void __cdecl fixture_input(int foreground, int mouse, int f6, float x, float y) {
    menu.test_input(foreground != 0, mouse != 0, f6 != 0, x, y, actions);
}
extern "C" __declspec(dllexport) int __cdecl fixture_click(float x, float y) { return menu.test_click(x, y, actions) ? 1 : 0; }
extern "C" __declspec(dllexport) int __cdecl fixture_count(int i) { return i >= 0 && i < 3 ? counts[i] : -1; }
extern "C" __declspec(dllexport) int __cdecl fixture_port() { return last_port; }
extern "C" __declspec(dllexport) int __cdecl fixture_address_is(const char* s) { return s && last_address == s; }
extern "C" __declspec(dllexport) void __cdecl fixture_shutdown() { menu.shutdown(); if (tick_event) { CloseHandle(tick_event); tick_event = nullptr; } }
