#include "wsm/code_byte_patch.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
unsigned fail_flush = 0, fail_restore = 0;
BOOL WINAPI scripted_protect(LPVOID address, SIZE_T length, DWORD protection, PDWORD previous) {
    if (protection == PAGE_EXECUTE_READ && fail_restore) {
        --fail_restore; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
    }
    return ::VirtualProtect(address, length, protection, previous);
}
BOOL WINAPI scripted_flush(HANDLE process, LPCVOID address, SIZE_T length) {
    if (fail_flush) { --fail_flush; SetLastError(ERROR_GEN_FAILURE); return FALSE; }
    return ::FlushInstructionCache(process, address, length);
}
}
// Compile the actual utility with only its OS failure returns controlled.
// The normal build has no testing switch or injectable production callback.
#define VirtualProtect scripted_protect
#define FlushInstructionCache scripted_flush
#include "../../native/src/code_byte_patch.cpp"
#undef VirtualProtect
#undef FlushInstructionCache

int main() {
    constexpr unsigned char code[] = {0x48,0x85,0xc9,0x74,0x06,0xb8,42,0,0,0,0xc3,
        0xb8,0xff,0xff,0xff,0xff,0xc3,0xb8,7,0,0,0,0xc3};
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory) return 2;
    std::memcpy(memory, code, sizeof(code));
    DWORD old = 0;
    if (!VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), memory, sizeof(code))) return 2;
    auto* target = memory + 4;
    const DWORD owner = GetCurrentThreadId();
    using Function = int (*)(void*);
    const auto function = reinterpret_cast<Function>(memory);
    int failures = 0;
    const auto check = [&](const char* name, bool result) { std::printf("%s %s\n", result ? "PASS" : "FAIL", name); if (!result) ++failures; };
    const auto protection = [&] { MEMORY_BASIC_INFORMATION info{}; return VirtualQuery(memory, &info, sizeof(info)) ? info.Protect : 0; };
    const auto recovered = [&](const wsm::CodeBytePatch& patch) {
        return !patch.active() && !patch.protection_pending() && *target == 0x06 && protection() == PAGE_EXECUTE_READ && function(nullptr) == -1 && function(memory) == 42;
    };
    std::string error;
    wsm::CodeBytePatch patch;
    fail_flush = 1;
    check("install cache failure retains owned byte", !patch.install(target, 0x06, 0x0c, owner, error) && patch.active() && !patch.protection_pending() && *target == 0x0c && error.find("Win32 error 31") != std::string::npos);
    check("install cache failure restores page protection", protection() == PAGE_EXECUTE_READ);
    check("install cache failure removable", patch.remove(error) && recovered(patch));

    fail_restore = 1;
    check("install protection failure retains pending ownership", !patch.install(target, 0x06, 0x0c, owner, error) && patch.active() && patch.protection_pending() && *target == 0x0c && protection() == PAGE_EXECUTE_READWRITE && error.find("Win32 error 5") != std::string::npos);
    check("install protection failure fully recoverable", patch.remove(error) && recovered(patch));

    check("setup for remove cache failure", patch.install(target, 0x06, 0x0c, owner, error));
    fail_flush = 1;
    check("remove cache failure retains cache ownership", !patch.remove(error) && patch.active() && !patch.protection_pending() && *target == 0x06 && protection() == PAGE_EXECUTE_READ && error.find("Win32 error 31") != std::string::npos);
    check("remove cache failure retry verifies baseline", patch.remove(error) && recovered(patch));

    check("setup for remove protection failure", patch.install(target, 0x06, 0x0c, owner, error));
    fail_restore = 1;
    check("remove protection failure retains protection-only ownership", !patch.remove(error) && !patch.active() && patch.protection_pending() && *target == 0x06 && protection() == PAGE_EXECUTE_READWRITE && error.find("Win32 error 5") != std::string::npos);
    check("remove protection-only failure recoverable", patch.remove(error) && recovered(patch));
    if (!patch.active() && !patch.protection_pending()) VirtualFree(memory, 0, MEM_RELEASE);
    else { std::printf("FAIL unresolved fixture ownership\n"); ++failures; }
    return failures ? 1 : 0;
}
