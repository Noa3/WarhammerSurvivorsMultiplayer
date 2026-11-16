#include "wsm/ingame_loop.h"
#include "wsm/ingame_ui.h"
#include "wsm/ingame_runtime.h"
#include "wsm/ingame_localization.h"
#include "wsm/player_camera_controller.h"
#include "wsm/main_thread_guard.h"
#include "wsm/window_hook.h"
#include <filesystem>
#include <memory>
#include <vector>

namespace {
struct Window { HWND hwnd = nullptr; DWORD thread = 0; };
BOOL CALLBACK find_window(HWND hwnd, LPARAM raw) {
    DWORD pid = 0;
    const auto thread = GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t name[128]{};
    if (GetClassNameW(hwnd, name, 128) && std::wcscmp(name, L"UnityWndClass") == 0) {
        auto& result = *reinterpret_cast<Window*>(raw);
        result = {hwnd, thread}; return FALSE;
    }
    return TRUE;
}
std::wstring environment(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size || size > 32768) return {};
    std::vector<wchar_t> text(size);
    if (GetEnvironmentVariableW(name, text.data(), size) != size - 1) return {};
    return std::wstring(text.data());
}
struct Context {
    wsm::Il2CppApi api;
    Il2CppDomain* domain = nullptr;
    HMODULE game = nullptr;
    Window window;
    wsm::MainThreadGuard guard;
    wsm::InGameRuntime runtime;
    std::unique_ptr<wsm::PlayerCameraController> camera;
    wsm::PlayerStateReader camera_lifecycle;
    wsm::CameraFollowMode camera_mode = wsm::CameraFollowMode::OwnPlayer;
    bool camera_initialized = false, camera_open = false, camera_failed = false;
    bool camera_cleanup_pending = false;
    std::string camera_error;
    // Created and destroyed only inside verified Unity callbacks. If the engine
    // window has already died, retain it until process exit instead of invoking
    // Unity from the worker during engine shutdown.
    wsm::InGameMenu* menu = nullptr;
    bool stop = false, ready = false, fatal = false;
    std::string error, previous_language;
    bool command_failed = false;
    void (*log)(const std::string&) = nullptr;
};
void command_result(Context& c, int result) {
    c.command_failed = result < 0;
}
void host(void* raw, int port) { auto& c = *static_cast<Context*>(raw); command_result(c, c.runtime.host(port, true)); }
void join(void* raw, const std::string& address, int port) { auto& c = *static_cast<Context*>(raw); command_result(c, c.runtime.join(address, port)); }
void leave(void* raw) { auto& c = *static_cast<Context*>(raw); command_result(c, c.runtime.leave()); }
std::string status(void* raw, const std::string& language) { auto& c = *static_cast<Context*>(raw); return c.command_failed ? std::string(wsm::i18n::text(wsm::i18n::Text::ActionRejected, language)) : c.runtime.localized_status(language); }
void camera_mode(void* raw, bool shared) {
    auto& c = *static_cast<Context*>(raw);
    c.camera_mode = shared ? wsm::CameraFollowMode::SharedAlive : wsm::CameraFollowMode::OwnPlayer;
    c.camera_failed = false;
    c.camera_error.clear();
    if (c.camera_initialized && !c.camera->set_mode(c.camera_mode, c.camera_error)) c.camera_failed = true;
}
std::string camera_status(void* raw, const std::string& language) {
    const auto& c = *static_cast<Context*>(raw);
    using wsm::i18n::Text;
    const auto label = [&](Text key) { return std::string(wsm::i18n::text(key, language)); };
    auto result = label(c.camera_mode == wsm::CameraFollowMode::OwnPlayer ? Text::CameraOwn : Text::CameraShared);
    if (!c.camera_error.empty() || c.camera_failed) return result + " | " + label(Text::CameraUnavailable);
    if (!c.camera_initialized) return result + " | " + label(Text::CameraInactive);
    const auto state = c.camera->state();
    if (!state.error.empty()) return result + " | " + label(Text::CameraUnavailable);
    if (!state.attached || !state.has_target) return result + " | " + label(Text::CameraInactive);
    return result + " | " + label(state.spectating ? Text::CameraSpectating : Text::CameraActive);
}
void tick_camera(Context& c, const wsm::MainThreadIdentity& identity) {
    if (!c.camera_initialized) {
        if (c.camera_failed) return;
        auto candidate = std::make_unique<wsm::PlayerCameraController>();
        if (!candidate->initialize(c.game, c.api, c.domain, identity.os_thread, identity.managed_thread, c.camera_error) ||
            !c.camera_lifecycle.initialize(c.game, c.api, c.domain, identity.os_thread, identity.managed_thread, c.camera_error)) {
            c.camera_failed = true;
            c.log("WSM_CAMERA_UNAVAILABLE: " + c.camera_error);
            return;
        }
        c.camera = std::move(candidate); c.camera_initialized = true;
    }
    const auto release = [&] {
        if (!c.camera_open) return true;
        if (!c.camera->close(c.camera_error)) return false;
        c.camera_open = false; return true;
    };
    bool running = false;
    std::string lifecycle_error;
    const auto lifecycle = c.camera_lifecycle.read_run_state(running, lifecycle_error);
    if (lifecycle == wsm::PlayerReadStatus::Captured && !running) {
        if (release()) { c.camera_failed = false; c.camera_error.clear(); }
        return;
    }
    if (c.camera_failed) { release(); return; }
    if (lifecycle != wsm::PlayerReadStatus::Captured) {
        release(); if (!lifecycle_error.empty()) c.camera_error = lifecycle_error;
        return;
    }
    if (c.camera_open) {
        const auto state = c.camera->state();
        if (!state.error.empty()) {
            // Do not burn a new callback generation every UI tick after a
            // persistent engine failure. Retry explicitly or in the next run.
            release(); c.camera_error = state.error; c.camera_failed = true;
            return;
        }
        if (state.attached) return;
        if (!release()) return;
    }
    // The current runtime creates its local primary actor as engine input 0.
    // This is not a network-identity mapping or a second local viewport.
    const auto opened = c.camera->open(0, c.camera_mode, c.camera_error);
    c.camera_open = opened != wsm::PlayerReadStatus::NotReady;
    if (opened == wsm::PlayerReadStatus::Error) {
        const auto original = c.camera_error;
        release(); c.camera_error = original; c.camera_failed = true;
        c.log("WSM_CAMERA_UNAVAILABLE: " + c.camera_error);
    }
}
void callback(void* raw) {
    auto& c = *static_cast<Context*>(raw);
    wsm::MainThreadIdentity identity;
    if (!c.guard.verify(c.window.thread, 0, identity, c.error)) return;
    try {
        if (c.stop) {
            if (c.camera_initialized) c.camera_cleanup_pending = !c.camera->close(c.camera_error);
            if (c.menu) { c.menu->shutdown(); delete c.menu; c.menu = nullptr; }
            return;
        }
        if (!c.menu) {
            auto candidate = std::make_unique<wsm::InGameMenu>();
            if (!candidate->initialize(c.game, c.api, c.domain, c.window.hwnd, c.error)) { c.fatal = true; return; }
            candidate->set_visible(false);
            c.menu = candidate.release(); c.ready = true;
            c.log("WSM_INGAME_MENU_READY: native Unity Canvas, F6/Koop entry, embedded TCP runtime");
            c.log("WSM_UNITY_THREAD_VERIFIED native=" + std::to_string(identity.os_thread) +
                  " managed=" + std::to_string(identity.managed_thread) + " engine_main=" + std::to_string(identity.engine_main_thread));
        }
        tick_camera(c, identity);
        c.menu->tick({&c, host, join, leave, status, camera_mode, camera_status});
        const auto language = c.menu->language();
        if (language != c.previous_language) { c.previous_language = language; c.log("WSM_INGAME_LANGUAGE=" + language); }
    } catch (const std::exception& error) { c.error = error.what(); c.fatal = true; }
}
}
namespace wsm {
void run_ingame_menu(HMODULE bridge, HMODULE game, Il2CppApi api, Il2CppDomain* domain, Il2CppThread* inspection_thread,
                     void (*log)(const std::string&)) {
    struct InspectionAttachment {
        Il2CppApi& api; Il2CppThread* thread;
        void detach() { if (thread) { api.thread_detach(thread); thread = nullptr; } }
        ~InspectionAttachment() { detach(); }
    } attachment{api, inspection_thread};
    Context context; auto& c = context; c.api = api; c.game = game; c.domain = domain; c.log = log;
    if (!c.api.resolve_probe(game, c.error) || !MainThreadGuard::create(c.api, domain, c.guard, c.error)) {
        log("ERROR: in-game menu runtime/main-thread contract: " + c.error); return;
    }
    std::vector<wchar_t> module_path(32768);
    const DWORD count = GetModuleFileNameW(bridge, module_path.data(), static_cast<DWORD>(module_path.size()));
    if (!count || count >= module_path.size()) { log("ERROR: bridge installation path is unavailable"); return; }
    const auto runtime_path = std::filesystem::path(module_path.data()).parent_path() / L"Wsm.InGame.Runtime.dll";
    if (!c.runtime.load(runtime_path.wstring(), c.error)) { log("ERROR: in-game networking: " + c.error); return; }
    const auto stop_signal = environment(L"WSM_INGAME_STOP_SIGNAL");
    if (!stop_signal.empty() && !std::filesystem::path(stop_signal).is_absolute()) {
        log("ERROR: in-game stop signal must be absolute"); return;
    }
    const auto test_seconds = environment(L"WSM_INGAME_TEST_SECONDS");
    ULONGLONG test_deadline = 0;
    if (!test_seconds.empty()) {
        wchar_t* end = nullptr; const auto seconds = std::wcstoul(test_seconds.c_str(), &end, 10);
        if (!seconds || seconds > 1800 || !end || *end) { log("ERROR: invalid bounded in-game test duration"); return; }
        test_deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000;
    }
    const auto init_deadline = GetTickCount64() + 45000;
    while (!c.window.hwnd && GetTickCount64() < init_deadline) {
        EnumWindows(find_window, reinterpret_cast<LPARAM>(&c.window));
        if (!c.window.hwnd) Sleep(50);
    }
    if (!c.window.hwnd) { log("ERROR: in-game menu has no owned Unity window"); return; }
    // The worker now dispatches only Win32 callbacks. Keeping an idle IL2CPP
    // attachment here can make engine shutdown wait for this very worker.
    attachment.detach();
    log("WSM_INGAME_WORKER_DETACHED: Unity access is confined to verified main-thread callbacks");
    log("WSM_INGAME_MENU_STARTING: waiting for a verified Unity callback");
    bool stall_reported = false;
    while (IsWindow(c.window.hwnd) && !c.fatal) {
        if ((!stop_signal.empty() && GetFileAttributesW(stop_signal.c_str()) != INVALID_FILE_ATTRIBUTES) ||
            (test_deadline && GetTickCount64() >= test_deadline)) break;
        const auto result = run_window_hook(bridge, c.window.hwnd, c.window.thread, callback, &c, 2000);
        if (result == HookResult::timeout) {
            if (!stall_reported) { log("WSM_INGAME_UI_WAITING: owned window is not pumping; retaining UI ownership for recovery"); stall_reported = true; }
            continue;
        }
        stall_reported = false;
        if (result != HookResult::completed) { c.error = "owned Unity callback could not complete"; c.fatal = true; break; }
        if (!c.ready && GetTickCount64() >= init_deadline) { c.fatal = true; break; }
        Sleep(33);
    }
    c.runtime.leave();
    c.stop = true;
    bool cleanup_reported = false;
    while ((c.menu || c.camera_cleanup_pending) && IsWindow(c.window.hwnd)) {
        const auto cleanup = run_window_hook_recovering(bridge, c.window.hwnd, c.window.thread, callback, &c, 2000);
        if ((!c.menu && !c.camera_cleanup_pending) || !IsWindow(c.window.hwnd)) break;
        if (!cleanup_reported) { log("WSM_INGAME_UI_CLEANUP_PENDING: retrying verified teardown before releasing its owner"); cleanup_reported = true; }
        // A context guard or managed teardown may fail transiently even after
        // dispatch resumes. Do not leave a persistent overlay without an owner.
        if (cleanup != HookResult::completed || c.menu || c.camera_cleanup_pending) Sleep(50);
    }
    if (c.menu) log("WSM_INGAME_MENU_STOP: Unity ownership retained for process shutdown");
    else log("WSM_INGAME_MENU_STOP: native UI detached; network departure requested");
    if (c.camera_cleanup_pending) log("WSM_CAMERA_STOP: callback ownership retained for process shutdown");
    if (c.fatal) log("ERROR: in-game menu stopped: " + c.error);
}
}
