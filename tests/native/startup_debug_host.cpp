#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>

static DWORD WINAPI blocked_worker(void* event) { WaitForSingleObject(event, INFINITE); return 0; }
int wmain(int argc, wchar_t** argv) {
    const wchar_t* mode = argc > 1 ? argv[1] : L"--normal";
    if (wcscmp(mode,L"--no-image")==0) { Sleep(60000); return 7; }
    if (wcscmp(mode,L"--exit")==0) return 7;
    if (wcscmp(mode,L"--arguments")==0) {
        wchar_t value[64]{};
        if (argc!=6 || wcscmp(argv[2],L"") || wcscmp(argv[3],L"space value") ||
            wcscmp(argv[4],L"quote\"inside") || wcscmp(argv[5],L"tail\\") ||
            !GetEnvironmentVariableW(L"WSM_FIXTURE_VALUE",value,64) || wcscmp(value,L"unicode_\u00E4")) return 92;
    }
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr), thread=nullptr;
    if (!event) return 80;
    if (wcscmp(mode,L"--admission")==0) {
        thread=CreateThread(nullptr,0,blocked_worker,event,0,nullptr);
        if (!thread) { CloseHandle(event); return 81; }
    }
    wchar_t path[32768]{};
    DWORD length=GetModuleFileNameW(nullptr,path,32768);
    if (!length || length>=32768) return 90;
    wchar_t* slash=wcsrchr(path,L'\\');
    if (!slash) return 90;
    if (wcscpy_s(slash+1,32768-static_cast<size_t>(slash+1-path),L"StartupDebugFixture.dll")) return 90;
    HMODULE dll=LoadLibraryW(path);
    if (!dll) return 91;
    // Fixture-only window lets the debugger finish detaching before this tiny host exits.
    Sleep(100);
    using Function=int(*)();
    auto function=reinterpret_cast<Function>(GetProcAddress(dll,"StartupDebugTarget"));
    int value=function?function():-1;
    SetEvent(event);
    if (thread) { if (WaitForSingleObject(thread,5000)!=WAIT_OBJECT_0) return 93; CloseHandle(thread); }
    CloseHandle(event);FreeLibrary(dll);
    return value==2?0:value==1?101:94;
}
