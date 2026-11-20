#include "wsm/ingame_runtime.h"
#include <cstdio>
#include <cstring>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SetEnvironmentVariableA("WSM_BINDING_BAD_VERSION", nullptr);
    SetEnvironmentVariableA("WSM_BINDING_OVERSIZE", nullptr);
    wsm::InGameRuntime runtime;
    std::string error;
    if (runtime.ready() || runtime.host(7780, false) >= 0 || runtime.leave() >= 0 ||
        runtime.load(L"relative-runtime.dll", error) || error.empty()) return 1;
    const bool bad_version = std::strcmp(argv[2], "version") == 0;
    if (bad_version) SetEnvironmentVariableA("WSM_BINDING_BAD_VERSION", "1");
    const bool loaded = runtime.load(std::filesystem::absolute(argv[1]).wstring(), error);
    if (bad_version) {
        if (loaded || runtime.ready() || error.empty()) {
            std::puts("FAIL: incompatible native ABI version was admitted"); return 1;
        }
        std::puts("PASS: incompatible native ABI version rejected"); return 0;
    }
    if (!loaded || !runtime.ready() || !error.empty()) { std::puts("FAIL: fixture ABI load"); return 1; }
    if (runtime.load(std::filesystem::absolute(argv[1]).wstring(), error)) return 1;
    if (runtime.host(-1, false) >= 0 || runtime.host(65536, false) >= 0 ||
        runtime.join("https://127.0.0.1", 7780) >= 0 || runtime.join("127.0.0.1", 0) >= 0) return 1;
    if (runtime.host(7780, false) != 0 || runtime.status() != "fixture-host-request") return 1;
    if (std::strcmp(argv[2], "legacy") == 0) {
        if (runtime.can_edit_host_rules() || runtime.host(7780, true, {}) != 0 ||
            runtime.host(7780, true, {0, 2, 0}) >= 0 || runtime.host(7780, true, {1, 1, 0}) >= 0 ||
            runtime.host(7780, true, {0, 1, 1}) >= 0) return 1;
    } else if (!runtime.can_edit_host_rules() || runtime.host(7780, true, {8, 2, 1}) != 0 ||
        runtime.status() != "fixture-configured-host") return 1;
    if (runtime.join("127.0.0.1", 7780) != 0 || runtime.status() != "fixture-join-request") return 1;
    if (runtime.leave() != 0 || runtime.status() != "fixture-left") return 1;
    SetEnvironmentVariableA("WSM_BINDING_OVERSIZE", "1");
    if (runtime.status() != "Netzwerkstatus ist voruebergehend nicht verfuegbar.") {
        std::puts("FAIL: oversized status was not rejected"); return 1;
    }
    std::puts("PASS: native ABI binding, input guards, bounded status; fixture only, not TCP");
    return 0;
}
