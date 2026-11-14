#include "wsm/window_hook.h"
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
struct State { std::atomic<int> calls{0}, timeouts{0}; };
void called(void* data) { ++static_cast<State*>(data)->calls; }
void timed_out(void* data) { ++static_cast<State*>(data)->timeouts; }
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
int main() {
    HANDLE ready = CreateEventW(nullptr,TRUE,FALSE,nullptr), resume = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!ready || !resume) return 2;
    HWND window = nullptr; DWORD owner = 0;
    std::thread target([&] {
        owner = GetCurrentThreadId();
        WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=window_proc; cls.lpszClassName=L"WSMOwnedRecoveryFixture";
        RegisterClassW(&cls);
        window = CreateWindowW(cls.lpszClassName,L"owned recovery fixture",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,cls.hInstance,nullptr);
        SetEvent(ready);
        WaitForSingleObject(resume,5000);
        MSG message{};
        while (GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
    });
    if (WaitForSingleObject(ready,5000)!=WAIT_OBJECT_0 || !window) { SetEvent(resume); target.join(); return 2; }
    State state;
    auto result = wsm::HookResult::failed;
    std::thread dispatcher([&] {
        result = wsm::run_window_hook_recovering(GetModuleHandleW(nullptr),window,owner,called,&state,30,timed_out);
    });
    const auto deadline = GetTickCount64()+2000;
    while (state.timeouts.load()<3 && GetTickCount64()<deadline) Sleep(5);
    const bool stalled = state.timeouts.load()>=3 && state.calls.load()==0;
    SetEvent(resume);
    dispatcher.join();
    const bool recovered = result==wsm::HookResult::completed && state.calls.load()==1;
    PostMessageW(window,WM_CLOSE,0,0); target.join();
    const auto ended = wsm::run_window_hook_recovering(GetModuleHandleW(nullptr),window,owner,called,&state,30,timed_out);
    CloseHandle(ready); CloseHandle(resume);
    if (!stalled || !recovered || ended!=wsm::HookResult::failed || state.calls.load()!=1) {
        std::printf("FAIL: stalled=%d recovered=%d callbacks=%d timeouts=%d\n",stalled,recovered,state.calls.load(),state.timeouts.load()); return 1;
    }
    std::puts("PASS: owned native callback survives multiple pump timeouts, runs exactly once after recovery and stops at window destruction; not a Unity/game test.");
    return 0;
}
