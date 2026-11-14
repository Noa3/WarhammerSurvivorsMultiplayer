#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    DWORD size = GetEnvironmentVariableW(L"TMPDIR", nullptr, 0);
    std::vector<wchar_t> tmp;
    if (size) {
        tmp.resize(size);
        if (!GetEnvironmentVariableW(L"TMPDIR", tmp.data(), size)) return 1;
    } else {
        size = GetTempPathW(0, nullptr);
        if (!size) return 1;
        tmp.resize(size);
        if (!GetTempPathW(size, tmp.data())) return 1;
    }
    auto log = std::filesystem::path(tmp.data()) / (L"wsm-bridge-test-" + std::to_wstring(GetCurrentProcessId()) + L".log");
    if (!SetEnvironmentVariableW(L"WSM_BRIDGE_LOG", log.c_str()) ||
        !SetEnvironmentVariableW(L"WSM_ENABLE_LOCAL_COOP", L"1") ||
        !SetEnvironmentVariableW(L"WSM_MAIN_THREAD_PROBE", nullptr) ||
        !SetEnvironmentVariableW(L"WSM_GAME_LOG", nullptr)) return 1;
    HMODULE bridge = LoadLibraryA(argv[1]);
    if (!bridge) { std::fprintf(stderr, "FAIL: bridge DLL could not be loaded\n"); return 1; }
    if (GetProcAddress(bridge, "WsmBridgeMainThreadTick")) {
        std::fprintf(stderr, "FAIL: misleading no-op main-thread entry is still exported\n"); return 1;
    }
    bool diagnosed = false;
    for (int i = 0; i < 100 && !diagnosed; ++i) {
        std::ifstream stream(log);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("WSM_ENABLE_LOCAL_COOP=1 requires WSM_MAIN_THREAD_PROBE=1") != std::string::npos)
                diagnosed = true;
        }
        if (!diagnosed) Sleep(20);
    }
    std::error_code error;
    std::filesystem::remove(log, error);
    if (!diagnosed) { std::fprintf(stderr, "FAIL: unavailable coop request was not diagnosed\n"); return 1; }
    FreeLibrary(bridge);
    std::puts("PASS: bridge loads without a misleading main-thread entry and diagnoses an unavailable coop request");
    return 0;
}
