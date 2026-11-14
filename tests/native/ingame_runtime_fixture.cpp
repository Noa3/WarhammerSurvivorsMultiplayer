// ABI-only fixture. This is not a networking implementation.
#include <windows.h>
#include <cstring>

namespace { const char* state = "fixture-idle"; int current_state = 0, peers = 0, port = 0; const char* failure = ""; }
extern "C" __declspec(dllexport) void __cdecl wsm_fixture_status(int value, int count, int bound, const char* code) {
    current_state = value; peers = count; port = bound; failure = code;
}
#ifndef WSM_BINDING_NO_STATUS_DETAILS
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
