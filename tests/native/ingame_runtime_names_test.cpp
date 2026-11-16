#include "wsm/ingame_runtime.h"
#include <concepts>
#include <cstdio>
#include <filesystem>
#include <string>

// Missing optional API is a runnable contract failure, not a compiler failure.
template<class Runtime>
int check_names(Runtime& runtime, const std::wstring& path, bool legacy) {
    if constexpr (!requires(Runtime& r, const std::string& name) {
        { r.names_ready() } -> std::same_as<bool>;
        { r.set_player_name(name) } -> std::same_as<int>;
        { r.local_player_name() } -> std::same_as<std::string>;
        { r.player_names() } -> std::same_as<std::string>;
    }) {
        std::puts("FAIL: optional player-name runtime contract is absent");
        return 1;
    } else {
        std::string error;
        if (runtime.names_ready() || runtime.set_player_name("Captain") >= 0 ||
            !runtime.local_player_name().empty() || !runtime.player_names().empty()) return 2;
        if (!runtime.load(path, error)) return 3;
        if (legacy) {
            if (runtime.names_ready() || runtime.set_player_name("Captain") >= 0 ||
                !runtime.local_player_name().empty() || !runtime.player_names().empty()) return 4;
            if (runtime.host(7780, false) != 0) return 5;
            std::puts("PASS: older connection ABI remains usable without name exports");
            return 0;
        }
        if (!runtime.names_ready() || runtime.set_player_name("Captain") != 0 ||
            runtime.local_player_name() != "Captain" || runtime.player_names() != "Captain\nScout") return 6;
        if (runtime.set_player_name(std::string(4097, 'x')) >= 0) return 7;
        SetEnvironmentVariableA("WSM_BINDING_BAD_NAMES", "oversize");
        if (!runtime.local_player_name().empty() || !runtime.player_names().empty()) return 8;
        SetEnvironmentVariableA("WSM_BINDING_BAD_NAMES", "unterminated");
        if (!runtime.local_player_name().empty() || !runtime.player_names().empty()) return 9;
        SetEnvironmentVariableA("WSM_BINDING_BAD_NAMES", nullptr);
        if (runtime.local_player_name() != "Captain" || runtime.player_names() != "Captain\nScout") return 10;
        std::puts("PASS: optional named runtime ABI, bounded buffers and recovery; no TCP claim");
        return 0;
    }
}
int main(int argc, char** argv) {
    if (argc != 3) return 64;
    SetEnvironmentVariableA("WSM_BINDING_BAD_VERSION", nullptr);
    SetEnvironmentVariableA("WSM_BINDING_BAD_NAMES", nullptr);
    wsm::InGameRuntime runtime;
    return check_names(runtime, std::filesystem::absolute(argv[1]).wstring(), std::string(argv[2]) == "legacy");
}
