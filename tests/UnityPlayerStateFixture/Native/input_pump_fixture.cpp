#include "wsm/rewired_input_pump.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
struct Metrics {
    std::atomic<std::uint64_t> inputs{0}, recovery{0}, destroyed{0};
    std::atomic<bool> owns{false}, stopped{false}, stop{false}, blocked{false}, correct_os{true};
    DWORD os{};
    int mode{};
    wsm::Il2CppApi api{};
    const MethodInfo* fire{};
};
// Controlled client boundary for the real production pump. No Rewired axes or
// game actors are changed; controller integration is tested separately.
class Client final : public wsm::InputPumpClient {
    std::shared_ptr<Metrics> m;
public:
    explicit Client(std::shared_ptr<Metrics> metrics) : m(std::move(metrics)) {}
    ~Client() override { ++m->destroyed; }
    bool start(std::string&) override {
        m->owns = true;
        if (m->mode == 1) throw std::runtime_error("Fixture client start after resource acquisition");
        return true;
    }
    void input_tick(std::uint64_t) override {
        ++m->inputs;
        if (GetCurrentThreadId() != m->os) m->correct_os = false;
        if (m->mode == 2) {
            const auto result = m->api.invoke(m->fire, nullptr, nullptr);
            if (result.exception) throw std::runtime_error("Fixture nested event failed");
        }
    }
    void recovery_tick(std::uint64_t) override {
        ++m->recovery;
        if (GetCurrentThreadId() != m->os) m->correct_os = false;
        if (m->stop && !m->blocked) { m->owns = false; m->stopped = true; }
    }
    void request_stop() noexcept override { m->stop = true; }
    bool stopped() const noexcept override { return m->stopped; }
};
struct Case {
    std::unique_ptr<wsm::RewiredInputPump> pump;
    std::shared_ptr<Metrics> metrics = std::make_shared<Metrics>();
};
std::vector<std::unique_ptr<Case>> cases;
Case* current{};
std::string reason;
BOOL CALLBACK find_window(HWND hwnd, LPARAM arg) {
    DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid);
    wchar_t cls[128]{};
    if (pid == GetCurrentProcessId() && GetClassNameW(hwnd, cls, 128) && std::wcscmp(cls, L"UnityWndClass") == 0) {
        *reinterpret_cast<HWND*>(arg) = hwnd;
        return FALSE;
    }
    return TRUE;
}
const MethodInfo* fixture_fire(wsm::Il2CppApi api, Il2CppDomain* domain) {
    std::size_t count{};
    auto assemblies = api.domain_get_assemblies(domain, &count);
    for (std::size_t i = 0; i < count; ++i) {
        auto image = api.assembly_get_image(assemblies[i]);
        if (std::strcmp(api.image_get_name(image), "Rewired_Core.dll") != 0) continue;
        for (std::size_t j = 0; j < api.image_get_class_count(image); ++j) {
            auto type = const_cast<Il2CppClass*>(api.image_get_class(image, j));
            if (std::strcmp(api.class_get_namespace(type), "Rewired") || std::strcmp(api.class_get_name(type), "ReInput")) continue;
            void* iterator{};
            while (auto method = api.class_get_methods(type, &iterator))
                if (std::strcmp(api.method_get_name(method), "FixtureFireInput") == 0 && api.method_get_param_count(method) == 0) return method;
        }
    }
    return nullptr;
}
}
extern "C" __declspec(dllexport) int __cdecl fixture_pump_start(int managed, unsigned long os, int mode) {
    reason.clear();
    if (current && current->pump) {
        const auto prior = current->pump->state();
        if ((prior.started || prior.subscription_pending || current->metrics->owns) && !prior.cleanup_complete) {
            reason = "Previous fixture case still owns cleanup";
            return 1;
        }
    }
    auto value = std::make_unique<Case>();
    current = value.get(); cases.push_back(std::move(value));
    current->pump = std::make_unique<wsm::RewiredInputPump>();
    auto game = GetModuleHandleW(L"GameAssembly.dll");
    wsm::Il2CppApi api;
    HWND window{}; EnumWindows(find_window, reinterpret_cast<LPARAM>(&window));
    HMODULE bridge{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&fixture_pump_start), &bridge) ||
        !wsm::Il2CppApi::resolve(game, api, reason) || !api.resolve_probe(game, reason)) return 2;
    current->metrics->api = api;
    current->metrics->os = os;
    current->metrics->mode = mode;
    current->metrics->fire = fixture_fire(api, api.domain_get());
    if (!current->metrics->fire) { reason = "Fixture event fire method missing"; return 3; }
    if (!current->pump->initialize(game, api, api.domain_get(), bridge, window, os, managed, reason)) return 4;
    if (mode >= 3 && mode <= 6) current->pump->test_fail_start(static_cast<unsigned>(mode - 2));
    return current->pump->start(std::make_unique<Client>(current->metrics), reason) ? 0 : 5;
}
extern "C" __declspec(dllexport) void __cdecl fixture_pump_stop() {
    if (current && current->pump) current->pump->stop();
}
extern "C" __declspec(dllexport) void __cdecl fixture_pump_drop_worker() {
    if (!current || !current->pump) return;
    auto wrapper = std::move(current->pump);
    std::thread producer([owned = std::move(wrapper)]() mutable { owned.reset(); });
    producer.join();
}
extern "C" __declspec(dllexport) void __cdecl fixture_pump_block_cleanup(int value) {
    if (current) current->metrics->blocked = value != 0;
}
extern "C" __declspec(dllexport) unsigned long long __cdecl fixture_pump_read(int field) {
    if (!current) return 0;
    const auto& m = *current->metrics;
    const auto state = current->pump ? current->pump->state() : wsm::RewiredInputPumpState{};
    switch (field) {
    case 0: return m.inputs;
    case 1: return m.recovery;
    case 2: return m.stopped ? 1 : 0;
    case 3: return state.cleanup_complete ? 1 : 0;
    case 4: return m.stop ? 1 : 0;
    case 5: return state.foreign_callbacks;
    case 6: return state.reentrant_callbacks;
    case 7: return state.subscription_pending ? 1 : 0;
    case 8: return m.destroyed;
    case 9: return m.correct_os ? 1 : 0;
    case 10: return state.recovery_running ? 1 : 0;
    default: return 0;
    }
}
extern "C" __declspec(dllexport) const char* __cdecl fixture_pump_reason() { return reason.c_str(); }
