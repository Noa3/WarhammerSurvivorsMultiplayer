#include <windows.h>
#include <vector>
namespace {
DWORD WINAPI write_marker(void*) {
    DWORD size = GetEnvironmentVariableW(L"WSM_BRIDGE_LOG", nullptr, 0);
    if (!size) return 1;
    std::vector<wchar_t> path(size);
    if (!GetEnvironmentVariableW(L"WSM_BRIDGE_LOG", path.data(), size)) return 1;
    HANDLE file = CreateFileW(path.data(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 1;
    const char marker[] = "WSM_FIXTURE_LOADED\r\n";
    DWORD written = 0;
    BOOL ok = WriteFile(file, marker, sizeof(marker) - 1, &written, nullptr);
    CloseHandle(file);
    return ok && written == sizeof(marker) - 1 ? 0 : 1;
}
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(instance), &pinned)) return FALSE;
        HANDLE thread = CreateThread(nullptr, 0, write_marker, nullptr, 0, nullptr);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
