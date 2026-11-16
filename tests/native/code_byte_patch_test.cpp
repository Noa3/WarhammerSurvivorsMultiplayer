#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <thread>
#include "wsm/code_byte_patch.h"

// test rcx,rcx; jz failure; return 42; failure: return -1; skip: return 7.
static constexpr unsigned char code[] = {
    0x48, 0x85, 0xc9, 0x74, 0x06, 0xb8, 42, 0, 0, 0, 0xc3,
    0xb8, 0xff, 0xff, 0xff, 0xff, 0xc3, 0xb8, 7, 0, 0, 0, 0xc3
};

int main(int argc, char** argv) {
    void* memory = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!memory) { std::printf("VirtualAlloc failed: %lu\n", GetLastError()); return 2; }
    std::memcpy(memory, code, sizeof(code));
    DWORD old = 0;
    if (!VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), memory, sizeof(code))) {
        std::printf("fixture setup failed: %lu\n", GetLastError()); return 2;
    }
    using Function = int (*)(void*);
    const auto function = reinterpret_cast<Function>(memory);
    if (argc == 2 && std::strcmp(argv[1], "--red") == 0) {
        int actual = function(nullptr);
        std::printf("%s unpatched null branch: expected 7, got %d\n", actual == 7 ? "PASS" : "FAIL", actual);
        VirtualFree(memory, 0, MEM_RELEASE);
        return actual == 7 ? 0 : 1;
    }
    int failures = 0;
    const auto check = [&](const char* name, bool ok) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    auto* target = static_cast<unsigned char*>(memory) + 4;
    const DWORD owner = GetCurrentThreadId();
    const auto protection = [&]() {
        MEMORY_BASIC_INFORMATION info{};
        return VirtualQuery(target, &info, sizeof(info)) ? info.Protect : 0ul;
    };
    const auto set_byte = [&](unsigned char byte) {
        DWORD prev = 0, ignored = 0;
        if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &prev)) return false;
        *target = byte;
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), target, 1) != 0;
        const bool restored = VirtualProtect(target, 1, prev, &ignored) != 0;
        return flushed && restored;
    };
    wsm::CodeBytePatch patch;
    std::string error;
    check("baseline native behavior", function(nullptr) == -1 && function(memory) == 42 && protection() == PAGE_EXECUTE_READ);
    check("wrong original refused", !patch.install(target, 0x07, 0x0c, owner, error) && *target == 0x06 && !patch.active());
    check("null address refused", !patch.install(nullptr, 0x06, 0x0c, owner, error));
    check("equal bytes refused", !patch.install(target, 0x06, 0x06, owner, error));
    check("wrong owner refused", !patch.install(target, 0x06, 0x0c, 0, error) && *target == 0x06);
    bool install_wrong_thread = false;
    std::thread wrong_installer([&] { std::string thread_error; install_wrong_thread = !patch.install(target, 0x06, 0x0c, owner, thread_error) && !thread_error.empty(); });
    wrong_installer.join();
    check("foreign thread cannot install", install_wrong_thread && *target == 0x06 && !patch.active());
    check("invalid pointer reports Win32 error", !patch.install(reinterpret_cast<void*>(UINTPTR_MAX), 0x06, 0x0c, owner, error) && error.find("Win32 error ") != std::string::npos);
    void* nx = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!nx) { std::printf("FAIL nx fixture VirtualAlloc: %lu\n", GetLastError()); ++failures; }
    else {
        *static_cast<unsigned char*>(nx) = 0x06;
        DWORD prior = 0;
        if (!VirtualProtect(nx, 4096, PAGE_READONLY, &prior)) { std::printf("FAIL nx fixture VirtualProtect: %lu\n", GetLastError()); ++failures; }
        else check("read-only NX region refused", !patch.install(nx, 0x06, 0x0c, owner, error) && *static_cast<unsigned char*>(nx) == 0x06);
        VirtualFree(nx, 0, MEM_RELEASE);
    }
    void* reserved = VirtualAlloc(nullptr, 4096, MEM_RESERVE, PAGE_NOACCESS);
    if (!reserved) { std::printf("FAIL reserved fixture VirtualAlloc: %lu\n", GetLastError()); ++failures; }
    else {
        check("uncommitted region refused", !patch.install(reserved, 0x06, 0x0c, owner, error));
        VirtualFree(reserved, 0, MEM_RELEASE);
    }
    void* guard = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!guard) { std::printf("FAIL guard fixture VirtualAlloc: %lu\n", GetLastError()); ++failures; }
    else {
        *static_cast<unsigned char*>(guard) = 0x06;
        DWORD prior = 0;
        if (!VirtualProtect(guard, 4096, PAGE_EXECUTE_READ | PAGE_GUARD, &prior)) { std::printf("FAIL guard fixture VirtualProtect: %lu\n", GetLastError()); ++failures; }
        else {
            MEMORY_BASIC_INFORMATION info{};
            const bool refused = !patch.install(guard, 0x06, 0x0c, owner, error);
            check("guard page refused without consuming guard", refused && VirtualQuery(guard, &info, sizeof(info)) && (info.Protect & PAGE_GUARD));
        }
        VirtualFree(guard, 0, MEM_RELEASE);
    }
    check("install owned code byte", patch.install(target, 0x06, 0x0c, owner, error) && patch.active() && !patch.protection_pending());
    check("patched native behavior", *target == 0x0c && function(nullptr) == 7 && function(memory) == 42 && protection() == PAGE_EXECUTE_READ);
    check("double install refused", !patch.install(target, 0x06, 0x0c, owner, error) && patch.active() && *target == 0x0c);
    bool wrong_thread = false;
    std::thread other([&] { std::string thread_error; wrong_thread = !patch.remove(thread_error) && !thread_error.empty(); });
    other.join();
    check("foreign thread cannot remove", wrong_thread && patch.active() && *target == 0x0c);
    check("fixture sets foreign byte", set_byte(0x08));
    check("foreign byte not overwritten", !patch.remove(error) && patch.active() && *target == 0x08 && protection() == PAGE_EXECUTE_READ);
    check("fixture recovers replacement byte", set_byte(0x0c));
    check("remove restores byte and ownership", patch.remove(error) && !patch.active() && !patch.protection_pending() && *target == 0x06);
    check("baseline behavior and protection restored", *target == 0x06 && protection() == PAGE_EXECUTE_READ && function(nullptr) == -1 && function(memory) == 42);
    check("repeated remove is harmless", patch.remove(error));
    check("reinstall and remove", patch.install(target, 0x06, 0x0c, owner, error) && patch.remove(error) && *target == 0x06 && protection() == PAGE_EXECUTE_READ);
    if (patch.active() || patch.protection_pending()) {
        std::printf("FAIL fixture retains patch ownership: %s\n", error.c_str());
        ++failures;
    } else if (!VirtualFree(memory, 0, MEM_RELEASE)) {
        std::printf("FAIL VirtualFree: %lu\n", GetLastError()); ++failures;
    }
    return failures ? 1 : 0;
}
