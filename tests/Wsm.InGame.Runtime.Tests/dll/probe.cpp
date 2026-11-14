// Real C ABI consumer for Wsm.InGame.Runtime. Loads the Native AOT DLL with LoadLibrary,
// resolves every export through GetProcAddress and drives real loopback TCP sessions.
// Usage:
//   probe.exe <dll> version
//   probe.exe <dll> abi
//   probe.exe <dll> host  <port> <holdMs>
//   probe.exe <dll> join  <ip>   <port> <holdMs>
#include "wsm_ingame_abi.h"
#include <windows.h>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {
using version_fn = int (*)();
using host_fn = int (*)(int, int);
using join_fn = int (*)(const uint8_t*, int, int);
using leave_fn = int (*)();
using buffer_fn = int (*)(uint8_t*, int);
using nullary_fn = int (*)();
using shutdown_fn = int (*)();

struct Api {
    version_fn version = nullptr;
    host_fn host = nullptr;
    join_fn join = nullptr;
    leave_fn leave = nullptr;
    buffer_fn status = nullptr;
    buffer_fn last_error = nullptr;
    nullary_fn state = nullptr;
    nullary_fn peer_count = nullptr;
    nullary_fn bound_port = nullptr;
    shutdown_fn shutdown = nullptr;
};

int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++g_failures; std::printf("CHECK_FAIL=%s\n", what); }
}

std::string read_buffer(buffer_fn fn) {
    const int required = fn(nullptr, 0);
    if (required < 0) return std::string("<query-failed>");
    std::vector<uint8_t> buffer(static_cast<size_t>(required) + 1, 0xAA);
    const int written = fn(buffer.data(), required + 1);
    if (written != required) return std::string("<write-failed>");
    return std::string(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(written));
}

int poll_state(Api& api, int wanted, int milliseconds) {
    for (int waited = 0; waited <= milliseconds; waited += 50) {
        if (api.state() == wanted) return wanted;
        Sleep(50);
    }
    return api.state();
}

void print_status(Api& api, const char* prefix) {
    const std::string status = read_buffer(api.status);
    const std::string error = read_buffer(api.last_error);
    std::printf("%sSTATE=%d\n%sPEERS=%d\n%sBOUND=%d\n%sERROR=%s\n%sSTATUS=%s\n",
                prefix, api.state(), prefix, api.peer_count(), prefix, api.bound_port(),
                prefix, error.c_str(), prefix, status.c_str());
    std::fflush(stdout);
}

int run_abi(Api& api) {
    check(api.version() == 1, "version");
    const std::string idle = read_buffer(api.status);
    check(!idle.empty(), "status text must not be empty");
    check(api.status(nullptr, 5) == WSM_INGAME_INVALID_ARGUMENT, "null buffer with capacity > 0 is invalid");
    check(api.status(nullptr, -1) == WSM_INGAME_INVALID_ARGUMENT, "negative capacity is invalid");
    const int required = api.status(nullptr, 0);
    std::vector<uint8_t> exact(static_cast<size_t>(required) + 1, 0);
    check(api.status(exact.data(), required + 1) == required, "write returns required length");
    check(exact[static_cast<size_t>(required)] == 0, "write must be null terminated");
    check(std::memchr(exact.data(), 0, exact.size()) == exact.data() + required, "only the terminator is NUL");
    std::vector<uint8_t> too_small(static_cast<size_t>(required), 0xAA);
    check(api.status(too_small.data(), required) == WSM_INGAME_INSUFFICIENT_CAPACITY, "capacity without terminator is insufficient");
    check(std::all_of(too_small.begin(), too_small.end(), [](uint8_t b) { return b == 0xAA; }),
          "insufficient capacity must leave the buffer unchanged");

    const uint8_t good[] = "127.0.0.1";
    check(api.join(nullptr, 9, 7780) == WSM_INGAME_INVALID_ARGUMENT, "null ip pointer");
    check(api.join(good, 0, 7780) == WSM_INGAME_INVALID_ARGUMENT, "zero ip length");
    check(api.join(good, 20, 7780) == WSM_INGAME_INVALID_ARGUMENT, "ip length above bound");
    check(api.join(good, 9, 0) == WSM_INGAME_INVALID_ARGUMENT, "join port 0");
    check(api.join(good, 9, 70000) == WSM_INGAME_INVALID_ARGUMENT, "join port above bound");
    check(api.host(-1, 0) == WSM_INGAME_INVALID_ARGUMENT, "host negative port");
    check(api.host(7780, 2) == WSM_INGAME_INVALID_ARGUMENT, "lanBind out of range");
    const uint8_t not_ip[] = "hostname";
    check(api.join(not_ip, 8, 7780) == WSM_INGAME_INVALID_ARGUMENT, "dns names are rejected");
    return g_failures == 0 ? 0 : 1;
}

int run_host(Api& api, int port, int hold_ms) {
    check(api.host(port, 0) == WSM_INGAME_ACCEPTED, "host accepted");
    if (poll_state(api, WSM_INGAME_STATE_HOSTING, 8000) != WSM_INGAME_STATE_HOSTING) {
        print_status(api, "FAILED_");
        return 1;
    }
    print_status(api, "");
    Sleep(static_cast<DWORD>(hold_ms));
    print_status(api, "AGAIN_");
    check(api.leave() == WSM_INGAME_ACCEPTED, "leave accepted");
    if (poll_state(api, WSM_INGAME_STATE_DISCONNECTED, 5000) != WSM_INGAME_STATE_DISCONNECTED) {
        print_status(api, "LEAVE_FAILED_");
        return 1;
    }
    print_status(api, "AFTER_");
    return 0;
}

int run_join(Api& api, const char* ip, int port, int hold_ms) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(ip);
    const int length = static_cast<int>(std::strlen(ip));
    check(api.join(bytes, length, port) == WSM_INGAME_ACCEPTED, "join accepted");
    for (int waited = 0; waited <= 12000; waited += 50) {
        const int state = api.state();
        if (state == WSM_INGAME_STATE_CONNECTED || state == WSM_INGAME_STATE_FAILED) break;
        Sleep(50);
    }
    print_status(api, "");
    Sleep(static_cast<DWORD>(hold_ms));
    print_status(api, "AGAIN_");
    check(api.leave() == WSM_INGAME_ACCEPTED, "leave accepted");
    poll_state(api, WSM_INGAME_STATE_DISCONNECTED, 5000);
    print_status(api, "AFTER_");
    return g_failures == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: probe <dll> <command> ...\n"); return 2; }
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) { std::printf("FAIL=LoadLibrary %lu\n", GetLastError()); return 2; }
    Api api;
    api.version = reinterpret_cast<version_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_version")));
    api.host = reinterpret_cast<host_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_host")));
    api.join = reinterpret_cast<join_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_join")));
    api.leave = reinterpret_cast<leave_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_leave")));
    api.status = reinterpret_cast<buffer_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_status")));
    api.last_error = reinterpret_cast<buffer_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_last_error")));
    api.state = reinterpret_cast<nullary_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_state")));
    api.peer_count = reinterpret_cast<nullary_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_peer_count")));
    api.bound_port = reinterpret_cast<nullary_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_bound_port")));
    api.shutdown = reinterpret_cast<shutdown_fn>(reinterpret_cast<void*>(GetProcAddress(module, "wsm_ingame_shutdown")));
    if (!api.version || !api.host || !api.join || !api.leave || !api.status || !api.last_error ||
        !api.state || !api.peer_count || !api.bound_port || !api.shutdown) {
        std::printf("FAIL=missing export\n");
        return 2;
    }
    const std::string command = argv[2];
    int code;
    if (command == "version") {
        std::printf("VERSION=%d\n", api.version());
        code = api.version() == 1 ? 0 : 1;
    } else if (command == "abi") {
        code = run_abi(api);
        std::printf("ABI=%s\nFAILURES=%d\n", code == 0 ? "PASS" : "FAIL", g_failures);
    } else if (command == "host" && argc == 5) {
        code = run_host(api, std::atoi(argv[3]), std::atoi(argv[4]));
    } else if (command == "join" && argc == 6) {
        code = run_join(api, argv[3], std::atoi(argv[4]), std::atoi(argv[5]));
    } else {
        std::printf("FAIL=unsupported command\n");
        code = 2;
    }
    std::fflush(stdout);
    return code;
}
