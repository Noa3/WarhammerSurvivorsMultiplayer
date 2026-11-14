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
    const bool mixed = std::string(argv[2]) == "mixed";
    const bool deferred = mixed || std::string(argv[2]) == "deferred";
    const bool coop = mixed || std::string(argv[2]) == "coop";
    const auto log = std::filesystem::absolute("wsm-bridge-policy-" + std::to_string(GetCurrentProcessId()) + ".log");
    if (std::filesystem::exists(log)) return 2;
    const auto signal = log.wstring() + L".trigger";
    SetEnvironmentVariableW(L"WSM_BRIDGE_LOG", log.c_str());
    SetEnvironmentVariableW(L"WSM_GAME_LOG", nullptr);
    SetEnvironmentVariableW(L"WSM_MAIN_THREAD_PROBE", L"1");
    SetEnvironmentVariableW(L"WSM_ENABLE_LOCAL_COOP", coop ? L"1" : nullptr);
    SetEnvironmentVariableW(L"WSM_OBSERVATION_SIGNAL", deferred ? signal.c_str() : nullptr);
    HMODULE bridge = LoadLibraryA(argv[1]);
    if (!bridge) { std::puts("FAIL: bridge fixture load"); return 1; }
    std::string text;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(mixed ? 2 : 1);
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream stream(log);
        text.assign(std::istreambuf_iterator<char>(stream), {});
        if (mixed && text.find("ERROR: deferred observation cannot enable local coop") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool refused = text.find("ERROR: deferred observation cannot enable local coop") != std::string::npos;
    const bool started = text.find("WSM bridge started") != std::string::npos;
    FreeLibrary(bridge);
    if (!started || refused != mixed) {
        std::puts("FAIL: read-only observation/coop flag combination policy"); return 1;
    }
    std::error_code ignored;
    std::filesystem::remove(log, ignored);
    std::puts("PASS: deferred observation cannot authorize coop mutation; separate modes preserved");
    return 0;
}
