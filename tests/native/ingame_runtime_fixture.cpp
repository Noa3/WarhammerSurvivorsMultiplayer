// ABI-only fixture. This is not a networking implementation.
#include <windows.h>
#include <cstring>
#include <string>

namespace { const char* state = "fixture-idle"; int current_state = 0, peers = 0, port = 0; const char* failure = ""; }
extern "C" __declspec(dllexport) void __cdecl wsm_fixture_status(int value, int count, int bound, const char* code) {
    current_state = value; peers = count; port = bound; failure = code;
}
#ifndef WSM_BINDING_NO_STATUS_DETAILS
namespace { std::string player_name = "Player"; }
int name_buffer(const std::string& text, unsigned char* buffer, int capacity) {
    char mode[32]{};
    GetEnvironmentVariableA("WSM_BINDING_BAD_NAMES", mode, 32);
    if (std::strcmp(mode, "oversize") == 0) return 5000;
    if (!buffer && capacity == 0) return static_cast<int>(text.size());
    if (!buffer || capacity <= static_cast<int>(text.size())) return -5;
    std::memcpy(buffer, text.c_str(), text.size() + 1);
    if (std::strcmp(mode, "unterminated") == 0) buffer[text.size()] = 'x';
    return static_cast<int>(text.size());
}
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_set_player_name(const unsigned char* name, int length) {
    if (!name || length < 1 || length > 192) return -1;
    player_name.assign(reinterpret_cast<const char*>(name), static_cast<std::size_t>(length)); return 0;
}
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_local_player_name(unsigned char* buffer, int capacity) {
    return name_buffer(player_name, buffer, capacity);
}
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_player_names(unsigned char* buffer, int capacity) {
    return name_buffer(player_name + "\nScout", buffer, capacity);
}
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_state() { return current_state; }
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_peer_count() { return peers; }
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_bound_port() { return port; }
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_last_error(unsigned char* buffer, int capacity) {
    const int size = static_cast<int>(std::strlen(failure));
    if (!buffer && capacity == 0) return size;
    if (!buffer || capacity <= size) return -5;
    std::memcpy(buffer, failure, static_cast<std::size_t>(size + 1)); return size;
}
#endif
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_version() {
    return GetEnvironmentVariableA("WSM_BINDING_BAD_VERSION", nullptr, 0) ? 2 : 1;
}
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_host(int, int) { state = "fixture-host-request"; return 0; }
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_join(const unsigned char*, int, int) { state = "fixture-join-request"; return 0; }
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_leave() { state = "fixture-left"; return 0; }
extern "C" __declspec(dllexport) int __cdecl wsm_ingame_status(unsigned char* buffer, int capacity) {
    if (GetEnvironmentVariableA("WSM_BINDING_OVERSIZE", nullptr, 0)) return 5000;
    const int size = static_cast<int>(std::strlen(state));
    if (!buffer && capacity == 0) return size;
    if (!buffer || capacity <= size) return -(size + 1);
    std::memcpy(buffer, state, static_cast<std::size_t>(size + 1));
    return size;
}
