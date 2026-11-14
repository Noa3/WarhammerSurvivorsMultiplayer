#include "wsm/il2cpp_api.h"
#include "wsm/main_probe.h"
#include "wsm/ingame_loop.h"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
void log_line(const std::string& line) {
    DWORD size = GetEnvironmentVariableW(L"WSM_BRIDGE_LOG", nullptr, 0);
    if (!size) { OutputDebugStringA(("WSM bridge: WSM_BRIDGE_LOG not set; " + line + "\n").c_str()); return; }
    std::vector<wchar_t> path(size);
    if (!GetEnvironmentVariableW(L"WSM_BRIDGE_LOG", path.data(), size)) return;
    HANDLE file = CreateFileW(path.data(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { OutputDebugStringA("WSM bridge: cannot open WSM_BRIDGE_LOG\n"); return; }
    std::string text = line + "\r\n"; DWORD written;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}
const char* safe(const char* s) { return s ? s : "<null>"; }
bool scripting_started() {
    const DWORD size = GetEnvironmentVariableW(L"WSM_GAME_LOG", nullptr, 0);
    if (!size) return false;
    std::vector<wchar_t> path(size);
    if (!GetEnvironmentVariableW(L"WSM_GAME_LOG", path.data(), size)) return false;
    std::ifstream stream{std::filesystem::path(path.data())};
    std::string line;
    // A non-null IL2CPP domain exists before runtime initialization has finished.
    // This game-owned managed startup message proves script execution is underway.
    while (std::getline(stream, line))
        if (line.starts_with("[VERSION_INFO][GAME] :: Warhammer Survivors - ")) return true;
    return false;
}
bool observation_requested() {
    const DWORD size = GetEnvironmentVariableW(L"WSM_OBSERVATION_SIGNAL", nullptr, 0);
    if (!size) return true;
    if (size > 32768) { log_line("ERROR: observation signal path exceeds supported length"); return false; }
    std::vector<wchar_t> path(size);
    if (GetEnvironmentVariableW(L"WSM_OBSERVATION_SIGNAL", path.data(), size) != size - 1 ||
        !std::filesystem::path(path.data()).is_absolute()) {
        log_line("ERROR: observation signal must be a stable absolute path"); return false;
    }
    log_line("WSM_OBSERVATION_WAITING: read-only sample awaits an explicit operator signal; deadline 15 minutes");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    while (std::chrono::steady_clock::now() < deadline) {
        const DWORD attributes = GetFileAttributesW(path.data());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
                log_line("ERROR: observation signal must be a regular non-linked file"); return false;
            }
            log_line("WSM_OBSERVATION_REQUESTED"); return true;
        }
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
            log_line("ERROR: observation signal is inaccessible"); return false;
        }
        Sleep(50);
    }
    log_line("ERROR: observation signal deadline expired without a request");
    return false;
}
void inspect(wsm::Il2CppApi& api, Il2CppDomain* domain) {
    std::size_t count = 0;
    const Il2CppAssembly** assemblies = api.domain_get_assemblies(domain, &count);
    if (!assemblies) { log_line("ERROR: il2cpp_domain_get_assemblies returned null; runtime metadata unavailable"); return; }
    bool manager = false, controller = false, multiplayer = false, coop_button = false;
    for (std::size_t i = 0; i < count; ++i) {
        const Il2CppImage* image = api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        log_line(std::string("image: ") + safe(api.image_get_name(image)));
        auto classes = api.image_get_class_count(image);
        for (std::size_t j = 0; j < classes; ++j) {
            auto* klass = const_cast<Il2CppClass*>(api.image_get_class(image, j));
            if (!klass) continue;
            const char* name = api.class_get_name(klass);
            const char* ns = api.class_get_namespace(klass);
            if (!name) continue;
            bool is_manager = ns && std::strcmp(ns, "VampireSurvivors.Framework") == 0 && std::strcmp(name, "GameManager") == 0;
            bool is_controller = std::strcmp(name, "CharacterController") == 0 && ns && std::strncmp(ns, "VampireSurvivors", 16) == 0;
            bool is_multiplayer = ns && std::strcmp(ns, "VampireSurvivors.Framework") == 0 && std::strcmp(name, "MultiplayerManager") == 0;
            bool is_coop_button = ns && std::strcmp(ns, "VampireSurvivors.App.Scripts.UI") == 0 && std::strcmp(name, "EnterCoopButton") == 0;
            if (!is_manager && !is_controller && !is_multiplayer && !is_coop_button) continue;
            manager |= is_manager; controller |= is_controller;
            multiplayer |= is_multiplayer; coop_button |= is_coop_button;
            std::string label = std::string(safe(ns)) + "." + name;
            log_line("class: " + label + " in " + safe(api.image_get_name(image)));
            void* iter = nullptr;
            while (const MethodInfo* method = api.class_get_methods(klass, &iter)) {
                log_line("  method: " + label + "." + safe(api.method_get_name(method)) + "(" + std::to_string(api.method_get_param_count(method)) + " params)");
            }
            iter = nullptr;
            while (FieldInfo* field = api.class_get_fields(klass, &iter))
                log_line("  field: " + label + "." + safe(api.field_get_name(field)));
        }
    }
    log_line(std::string("status: GameManager=") + (manager ? "found" : "MISSING") + " CharacterController=" + (controller ? "found" : "MISSING") + "; metadata only, no gameplay adapter installed");
    log_line(std::string("status: MultiplayerManager=") + (multiplayer ? "found" : "MISSING") + " EnterCoopButton=" + (coop_button ? "found" : "MISSING") + "; metadata does not prove a live instance");
    if (!manager || !controller) log_line("ACTION: compare game version and runtime image/class names above before enabling any adapter");
}
DWORD WINAPI worker(void* module) {
    log_line("WSM bridge started; waiting up to 60 seconds for managed startup in WSM_GAME_LOG");
    wchar_t coop[2] = {};
    bool coop_enabled = GetEnvironmentVariableW(L"WSM_ENABLE_LOCAL_COOP", coop, 2) == 1 && coop[0] == L'1';
    wchar_t probe[2] = {};
    bool probe_enabled = GetEnvironmentVariableW(L"WSM_MAIN_THREAD_PROBE", probe, 2) == 1 && probe[0] == L'1';
    wchar_t ui[2] = {};
    const bool ui_enabled = GetEnvironmentVariableW(L"WSM_IN_GAME_UI", ui, 2) == 1 && ui[0] == L'1';
    if (ui_enabled && coop_enabled) { log_line("ERROR: in-game lobby cannot invoke experimental local coop"); return 1; }
    if (ui_enabled && GetEnvironmentVariableW(L"WSM_OBSERVATION_SIGNAL", nullptr, 0) != 0) {
        log_line("ERROR: deferred observation cannot enable the in-game lobby"); return 1;
    }
    if (coop_enabled && GetEnvironmentVariableW(L"WSM_OBSERVATION_SIGNAL", nullptr, 0) != 0) {
        log_line("ERROR: deferred observation cannot enable local coop; use a separate explicitly state-changing probe");
        return 1;
    }
    if (coop_enabled && !probe_enabled)
        log_line("ERROR: WSM_ENABLE_LOCAL_COOP=1 requires WSM_MAIN_THREAD_PROBE=1; coop unavailable");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    HMODULE game = nullptr; wsm::Il2CppApi api; Il2CppDomain* domain = nullptr;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!scripting_started()) { Sleep(100); continue; }
        if (!game) {
            // Pin the runtime before retaining exported function pointers.
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"GameAssembly.dll", &game)) {
                std::string missing;
                if (!wsm::Il2CppApi::resolve(game, api, missing)) {
                    log_line("ERROR: missing required IL2CPP export " + missing + "; unsupported runtime, inspection stopped"); return 1;
                }
            }
        }
        if (game && (domain = api.domain_get())) break;
        Sleep(100);
    }
    if (!domain) { log_line("ERROR: timed out waiting for GameAssembly.dll / initialized IL2CPP domain; check launcher and game startup"); return 1; }
    Il2CppThread* thread = api.thread_attach(domain);
    if (!thread) { log_line("ERROR: il2cpp_thread_attach failed; inspection stopped"); return 1; }
    log_line("IL2CPP domain ready; inspection thread attached");
    inspect(api, domain);
    if (ui_enabled) wsm::run_ingame_menu(static_cast<HMODULE>(module), game, api, domain, thread, log_line);
    else if (probe_enabled && observation_requested()) wsm::run_main_probe(static_cast<HMODULE>(module), game, api, domain, coop_enabled, log_line);
    if (!ui_enabled) api.thread_detach(thread); // The persistent UI detaches before dispatching.
    log_line("inspection thread detached");
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HMODULE pinned = nullptr;
        // The worker may outlive the loader's reference. A process-lifetime pin
        // avoids executing unloaded code without waiting under the loader lock.
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(instance), &pinned)) return FALSE;
        HANDLE thread = CreateThread(nullptr, 0, worker, instance, 0, nullptr);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
