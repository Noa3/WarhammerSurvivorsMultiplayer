#include "wsm/player_observer.h"
#include "wsm/window_hook.h"
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace wsm {
std::string player_observation_json(PlayerReadStatus status, const SceneStateSnapshot& scene,
                                    const MainThreadIdentity& id, unsigned sample) {
    const char* state = nullptr;
    switch (status) {
        case PlayerReadStatus::Captured: state = "captured"; break;
        case PlayerReadStatus::NotReady: state = "not_ready"; break;
        case PlayerReadStatus::Error: state = "error"; break;
        default: throw std::invalid_argument("Unknown player observation status");
    }
    if (status == PlayerReadStatus::Captured) {
        if (!id.os_thread || id.managed_thread <= 0 || id.managed_thread != id.engine_main_thread ||
            scene.players.empty() || scene.players.size() > 4)
            throw std::invalid_argument("Capture lacks verified thread or bounded player membership");
        for (const auto& p : scene.players)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.hp) || !std::isfinite(p.xp))
                throw std::invalid_argument("Capture contains non-finite values");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "{\"schemaVersion\":1,\"sample\":" << sample << ",\"state\":\"" << state
        << "\",\"thread\":{\"native\":" << id.os_thread << ",\"managed\":" << id.managed_thread
        << ",\"engineMain\":" << id.engine_main_thread << '}';
    if (status == PlayerReadStatus::Captured) out << ",\"stageType\":" << scene.stage_type;
    out << ",\"players\":[";
    if (status == PlayerReadStatus::Captured) {
        bool first = true;
        for (const auto& p : scene.players) {
            if (!first) out << ',';
            first = false;
            out << "{\"slot\":" << p.slot << ",\"inputId\":" << p.input_id << ",\"characterType\":" << p.character_type
                << ",\"level\":" << p.level << ",\"x\":" << p.x << ",\"y\":" << p.y
                << ",\"hp\":" << p.hp << ",\"xp\":" << p.xp << ",\"dead\":" << (p.dead ? "true" : "false") << '}';
        }
    }
    out << "]}";
    return out.str();
}
namespace {
struct Window { HWND hwnd = nullptr; DWORD thread = 0; };
BOOL CALLBACK find_window(HWND hwnd, LPARAM raw) {
    DWORD pid = 0;
    const auto thread = GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t name[128]{};
    if (GetClassNameW(hwnd, name, 128) && std::wcscmp(name, L"UnityWndClass") == 0) {
        *reinterpret_cast<Window*>(raw) = {hwnd, thread};
        return FALSE;
    }
    return TRUE;
}
std::filesystem::path output_directory() {
    const DWORD size = GetEnvironmentVariableW(L"WSM_PLAYER_OBSERVATION_DIR", nullptr, 0);
    if (!size || size > 32768) throw std::invalid_argument("Player observation directory is missing or too long");
    std::vector<wchar_t> text(size);
    if (GetEnvironmentVariableW(L"WSM_PLAYER_OBSERVATION_DIR", text.data(), size) != size - 1)
        throw std::invalid_argument("Player observation directory changed during setup");
    std::filesystem::path path(text.data());
    if (!path.is_absolute()) throw std::invalid_argument("Player observation directory must be absolute");
    for (auto ancestor = path; !ancestor.empty();) {
        const DWORD flags = GetFileAttributesW(ancestor.c_str());
        if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::invalid_argument("Player observation directory must have existing regular directory ancestors");
        const auto parent = ancestor.parent_path();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    if (!std::filesystem::is_empty(path)) throw std::invalid_argument("Choose a fresh empty player observation directory");
    return path;
}
bool request_exists(const std::filesystem::path& path) {
    const DWORD flags = GetFileAttributesW(path.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return false;
        throw std::runtime_error("Observation request path is unavailable");
    }
    if (flags & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Observation request must be a regular non-linked file");
    return true;
}
void publish(const std::filesystem::path& path, const std::string& text) {
    auto temporary = path;
    temporary += L".tmp";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot exclusively create observation output");
    DWORD written = 0;
    const bool complete = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                          written == text.size() && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != FALSE;
    if (!complete || !closed) throw std::runtime_error("Observation write failed; partial file retained");
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish observation without replacing existing output");
}
struct Context {
    PlayerStateReader reader;
    MainThreadGuard guard;
    Window window;
    SceneStateSnapshot scene;
    MainThreadIdentity identity;
    PlayerReadStatus status = PlayerReadStatus::Error;
    std::string error;
};
void capture(void* raw) {
    auto& c = *static_cast<Context*>(raw);
    try {
        if (!c.guard.verify(c.window.thread, 0, c.identity, c.error)) return;
        c.status = c.reader.read(c.scene, c.error);
    } catch (const std::exception& error) {
        c.status = PlayerReadStatus::Error;
        c.error = error.what();
    }
}
}
void run_player_observer(HMODULE bridge, HMODULE game, Il2CppApi api, Il2CppDomain* domain,
                         Il2CppThread* thread, void (*log)(const std::string&)) {
    struct Attachment {
        Il2CppApi& api;
        Il2CppThread* thread;
        void detach() { if (thread) { api.thread_detach(thread); thread = nullptr; } }
        ~Attachment() { detach(); }
    } attachment{api, thread};
    try {
        const auto directory = output_directory();
        Context c;
        const auto startup_deadline = GetTickCount64() + 45000;
        while (!c.window.hwnd && GetTickCount64() < startup_deadline) {
            EnumWindows(find_window, reinterpret_cast<LPARAM>(&c.window));
            if (!c.window.hwnd) Sleep(50);
        }
        if (!c.window.hwnd) throw std::runtime_error("No owned Unity window for player observation");
        if (!api.resolve_probe(game, c.error) || !MainThreadGuard::create(api, domain, c.guard, c.error) ||
            !c.reader.initialize(game, api, domain, c.window.thread, 0, c.error))
            throw std::runtime_error("Player state contract: " + c.error);
        attachment.detach();
        log("WSM_PLAYER_OBSERVER_READY: metadata worker detached; request-000001 through request-000008; 15 minute deadline");
        const auto deadline = GetTickCount64() + 15ull * 60 * 1000;
        for (unsigned sample = 1; sample <= 8 && IsWindow(c.window.hwnd); ++sample) {
            std::ostringstream stem;
            stem << std::setw(6) << std::setfill('0') << sample;
            const auto request = directory / ("request-" + stem.str());
            while (IsWindow(c.window.hwnd) && GetTickCount64() < deadline && !request_exists(request)) Sleep(50);
            if (!IsWindow(c.window.hwnd)) break;
            if (GetTickCount64() >= deadline) { log("WSM_PLAYER_OBSERVER_STOP: request deadline reached"); return; }
            c.status = PlayerReadStatus::Error;
            c.scene = {};
            c.identity = {};
            c.error.clear();
            const auto result = run_window_hook(bridge, c.window.hwnd, c.window.thread, capture, &c, 2000);
            if (result != HookResult::completed) {
                c.status = PlayerReadStatus::Error;
                c.error = "Owned Unity callback did not complete";
            }
            publish(directory / ("state-" + stem.str() + ".json"), player_observation_json(c.status, c.scene, c.identity, sample));
            log("WSM_PLAYER_OBSERVATION=" + std::to_string(sample) + " status=" + std::to_string(static_cast<int>(c.status)));
            if (!c.error.empty()) log("WSM_PLAYER_OBSERVATION_ERROR: " + c.error);
        }
        log("WSM_PLAYER_OBSERVER_STOP: sample limit or owned window exit");
    } catch (const std::exception& error) { log(std::string("ERROR: player observation stopped: ") + error.what()); }
}
}
