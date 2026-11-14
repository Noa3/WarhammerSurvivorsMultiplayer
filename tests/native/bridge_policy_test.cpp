#include <windows.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const std::string mode = argv[2];
    const bool mixed = mode == "mixed";
    const bool roster = mode.starts_with("roster_");
    const bool player = mode.starts_with("players_") || mode == "roster_players";
    const bool ui = mode == "ui" || mode == "ui_deferred" || mode == "ui_coop" || mode == "players_ui" || mode == "roster_ui";
    const bool deferred = mixed || mode == "deferred" || mode == "ui_deferred" || mode == "roster_deferred";
    const bool coop = mixed || mode == "coop" || mode == "ui_coop" || mode == "players_coop" || mode == "roster_coop";
    const char* expected = mixed ? "ERROR: deferred observation cannot enable local coop" :
        mode == "ui_deferred" ? "ERROR: deferred observation cannot enable the in-game lobby" :
        mode == "ui_coop" ? "ERROR: in-game lobby cannot invoke experimental local coop" :
        mode == "players_ui" ? "ERROR: player observation cannot enable the in-game lobby" :
        mode == "players_coop" ? "ERROR: player observation cannot enable local coop" :
        mode == "players_unarmed" ? "ERROR: player observation requires the explicit main-thread probe" :
        mode == "roster_ui" ? "ERROR: roster probe cannot enable the in-game lobby" :
        mode == "roster_players" ? "ERROR: roster probe cannot enable player observation" :
        mode == "roster_coop" ? "ERROR: roster probe cannot enable legacy local coop" :
        mode == "roster_deferred" ? "ERROR: roster probe cannot use a second observation signal" :
        mode == "roster_unarmed" ? "ERROR: roster probe requires the explicit main-thread probe" : nullptr;
    const auto log = std::filesystem::absolute("wsm-bridge-policy-" + std::to_string(GetCurrentProcessId()) + ".log");
    if (std::filesystem::exists(log)) return 2;
    const auto signal = log.wstring() + L".trigger";
    SetEnvironmentVariableW(L"WSM_BRIDGE_LOG", log.c_str());
    SetEnvironmentVariableW(L"WSM_GAME_LOG", nullptr);
    SetEnvironmentVariableW(L"WSM_MAIN_THREAD_PROBE", mode == "players_unarmed" || mode == "roster_unarmed" ? L"0" : L"1");
    SetEnvironmentVariableW(L"WSM_COOP_ROSTER_DIR", roster ? signal.c_str() : nullptr);
    SetEnvironmentVariableW(L"WSM_PLAYER_OBSERVATION_DIR", player ? signal.c_str() : nullptr);
    SetEnvironmentVariableW(L"WSM_IN_GAME_UI", ui ? L"1" : nullptr);
    SetEnvironmentVariableW(L"WSM_ENABLE_LOCAL_COOP", coop ? L"1" : nullptr);
    SetEnvironmentVariableW(L"WSM_OBSERVATION_SIGNAL", deferred ? signal.c_str() : nullptr);
    HMODULE bridge = LoadLibraryA(argv[1]);
    if (!bridge) { std::puts("FAIL: bridge fixture load"); return 1; }
    std::string text;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expected ? 2 : 1);
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream stream(log);
        text.assign(std::istreambuf_iterator<char>(stream), {});
        if (expected && text.find(expected) != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool refused = expected && text.find(expected) != std::string::npos;
    const bool started = text.find("WSM bridge started") != std::string::npos;
    FreeLibrary(bridge);
    if (!started || refused != (expected != nullptr)) {
        std::puts("FAIL: read-only observation/in-game mutation flag combination policy"); return 1;
    }
    std::error_code ignored;
    std::filesystem::remove(log, ignored);
    std::puts("PASS: deferred observation cannot authorize coop mutation; separate modes preserved");
    return 0;
}
