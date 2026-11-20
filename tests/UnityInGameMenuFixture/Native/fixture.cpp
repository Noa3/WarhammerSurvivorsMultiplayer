#include "wsm/ingame_ui.h"
#include "wsm/ingame_localization.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
wsm::InGameMenu menu;
int counts[5] = {};
int last_port = 0;
std::string last_address;
int effective_camera = 0;
HANDLE tick_event = nullptr;
ULONGLONG next_tick = 0;
void host(void*, int port) { ++counts[0]; last_port = port; }
void join(void*, const std::string& address, int port) { ++counts[1]; last_address = address; last_port = port; }
void leave(void*) { ++counts[2]; }
std::string status(void*, const std::string& language) { return language == "de" ? "Fixture: keine Netzwerkverbindung" : "Fixture: no network connection"; }
void camera_mode(void*, bool shared) { ++counts[shared ? 4 : 3]; }
std::string camera_status(void*, const std::string& language) {
    constexpr wsm::i18n::Text states[] = {wsm::i18n::Text::CameraInactive, wsm::i18n::Text::CameraActive,
        wsm::i18n::Text::CameraSpectating, wsm::i18n::Text::CameraUnavailable};
    return std::string(wsm::i18n::text(states[effective_camera], language));
}
int name_state = 0;
wsm::Il2CppApi original_api{};
bool omit_name_metadata = false;
const MethodInfo* methods_without_rich_text(Il2CppClass* type, void** iterator) {
    while (auto method = original_api.class_get_methods(type, iterator))
        if (std::strcmp(original_api.method_get_name(method), "set_supportRichText") != 0) return method;
    return nullptr;
}
std::string local_name(void*, const std::string&) {
    if (name_state == 1) return {};
    if (name_state == 2) return std::string(48, 'W');
    if (name_state == 3) return "勇者";
    return "Fixture Captain";
}
std::string player_names(void*, const std::string&) {
    if (name_state == 1) return {};
    if (name_state == 2) return std::string(48, 'W') + "\nScout\nMage\nGuard";
    if (name_state == 3) return "勇者\nScout";
    if (name_state == 4) return "Fixture Captain";
    if (name_state == 5) return "Host\nGuest 1\nGuest 2\nGuest 3\nGuest 4\nGuest 5\nGuest 6\nGuest 7\nGuest 8";
    return "Fixture Captain\nRune <b>Hunter</b>";
}
wsm::InGameMenuActions actions{nullptr, host, join, leave, status, camera_mode, camera_status, local_name, player_names};
int rules_hosts = 0;
wsm::HostRules last_rules;
void record_rules(void*, int port, wsm::HostRules rules) {
    ++rules_hosts; last_port = port; last_rules = rules;
}
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
    original_api = api;
    if (omit_name_metadata) api.class_get_methods = methods_without_rich_text;
    if (!menu.initialize(module, api, api.domain_get(), hwnd, error)) { printf("MENU_NATIVE initialize error: %s\n", error.c_str()); fflush(stdout); return 2; }
    return 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_init_without_name_metadata() {
    omit_name_metadata = true;
    const int result = fixture_init();
    omit_name_metadata = false;
    return result;
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
extern "C" __declspec(dllexport) void __cdecl fixture_camera_callbacks(int mode, int state) {
    actions.camera_mode = mode ? camera_mode : nullptr;
    actions.camera_status = state ? camera_status : nullptr;
}
extern "C" __declspec(dllexport) void __cdecl fixture_camera_effective(int state) { if (state >= 0 && state < 4) effective_camera = state; }
extern "C" __declspec(dllexport) void __cdecl fixture_name_state(int state) { name_state = state; }
extern "C" __declspec(dllexport) void __cdecl fixture_name_callbacks(int local, int roster) {
    actions.local_player_name = local ? local_name : nullptr;
    actions.player_names = roster ? player_names : nullptr;
}
extern "C" __declspec(dllexport) void __cdecl fixture_input(int foreground, int mouse, int f6, float x, float y) {
    menu.test_input(foreground != 0, mouse != 0, f6 != 0, x, y, actions);
}
extern "C" __declspec(dllexport) int __cdecl fixture_click(float x, float y) { return menu.test_click(x, y, actions) ? 1 : 0; }
extern "C" __declspec(dllexport) int __cdecl fixture_count(int i) { return i >= 0 && i < 5 ? counts[i] : -1; }
extern "C" __declspec(dllexport) int __cdecl fixture_port() { return last_port; }
extern "C" __declspec(dllexport) void __cdecl fixture_rules_editable(int editable) {
    actions.host_with_rules = record_rules; actions.rules_editable = editable != 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_rules_receipt(int* bots, int* difficulty, int* death) {
    *bots = last_rules.bots; *difficulty = last_rules.difficulty; *death = last_rules.death;
    return rules_hosts;
}
extern "C" __declspec(dllexport) int __cdecl fixture_address_is(const char* s) { return s && last_address == s; }
extern "C" __declspec(dllexport) void __cdecl fixture_shutdown() { menu.shutdown(); if (tick_event) { CloseHandle(tick_event); tick_event = nullptr; } }
