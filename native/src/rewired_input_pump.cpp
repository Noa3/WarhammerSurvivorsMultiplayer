#include "wsm/rewired_input_pump.h"
#include "wsm/window_hook.h"
#include <array>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace wsm {
namespace {
bool same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
}

struct RewiredInputPump::Impl {
    static constexpr unsigned capacity = 256;
    using Callback = void (__cdecl*)();
    static std::array<std::atomic<Impl*>, capacity> owners;
    static std::atomic<unsigned> next_slot;
    static std::atomic<Impl*> registry;
    Impl* registry_next = nullptr;
    Il2CppApi api{};
    Il2CppDomain* domain{};
    HMODULE game{}, bridge{};
    HWND window{};
    DWORD os{};
    std::int32_t managed{};
    MainThreadGuard guard;
    Il2CppClass* action{};
    Il2CppClass* system_type{};
    const MethodInfo *add{}, *remove{}, *convert{};
    Il2CppClass* (*from_type)(const Il2CppType*){};
    Il2CppClass* (*method_owner)(const MethodInfo*){};
    bool (*assignable)(Il2CppClass*, Il2CppClass*){};
    void* (*root_new)(Il2CppObject*, bool){};
    Il2CppObject* (*root_target)(void*){};
    void (*root_free)(void*){};
    void* action_root{};
    unsigned slot = capacity;
    HANDLE gate{};
    std::unique_ptr<InputPumpClient> client;
    std::atomic<InputPumpClient*> published_client{nullptr};
    std::atomic<bool> attempted{false}, stopping{false}, done{false}, lost{false};
    std::atomic<bool> busy{false};
    std::atomic<unsigned> inflight{0};
    std::atomic<std::uint64_t> foreign{0}, reentrant{0};
    std::atomic<unsigned> error_status{0};
    std::atomic<const char*> hook_error{nullptr};
    char error_text[256]{};
    mutable std::mutex view_mutex;
    RewiredInputPumpState view;
    // Main-thread-only. An unsuccessful remove remains potentially live.
    bool pending = false, removed = false;
#ifdef WSM_PLAYER_STATE_TESTING
    unsigned start_fault = 0;
    void fault(unsigned step) { if (start_fault == step) throw std::runtime_error("Injected pre-worker input pump failure"); }
#else
    void fault(unsigned) {}
#endif

    void diagnostic(const char* text) noexcept {
        unsigned empty = 0;
        if (!error_status.compare_exchange_strong(empty, 1, std::memory_order_acq_rel)) return;
        std::size_t i = 0;
        if (text) for (; i + 1 < sizeof(error_text) && text[i]; ++i) error_text[i] = text[i];
        error_text[i] = '\0';
        error_status.store(2, std::memory_order_release);
    }
    void request_stop() noexcept {
        stopping.store(true, std::memory_order_release);
        if (auto* c = published_client.load(std::memory_order_acquire)) c->request_stop();
    }
    void failed_start(bool worker_created, const char* error) {
        diagnostic(error);
        request_stop();
        if (worker_created) {
            SetEvent(gate);
        } else {
            // No client resource or subscription was acquired. Preserve the
            // terminal result without wedging a later, fresh observation owner.
            if (gate) { CloseHandle(gate); gate = nullptr; }
            { std::lock_guard<std::mutex> lock(view_mutex); view.cleanup_complete = true; }
            done.store(true, std::memory_order_release);
        }
    }
    bool window_valid() const noexcept {
        DWORD pid = 0;
        return IsWindow(window) && GetWindowThreadProcessId(window, &pid) == os && pid == GetCurrentProcessId();
    }
    void window_gone() noexcept {
        lost.store(true, std::memory_order_release);
        diagnostic("Owned Unity window was lost; cleanup remains pending");
        request_stop();
    }
    bool verify() {
        MainThreadIdentity id; std::string error;
        if (!guard.verify(os, managed, id, error)) {
            diagnostic("Main-thread identity verification failed"); request_stop(); return false;
        }
        return true;
    }
    Il2CppClass* klass(const char* image, const char* ns, const char* name) {
        std::size_t count = 0; auto assemblies = api.domain_get_assemblies(domain, &count);
        if (!assemblies) throw std::runtime_error("Assembly list unavailable");
        Il2CppClass* found = nullptr;
        for (std::size_t i = 0; i < count; ++i) {
            auto im = api.assembly_get_image(assemblies[i]);
            if (!im || !same(api.image_get_name(im), image)) continue;
            for (std::size_t j = 0; j < api.image_get_class_count(im); ++j) {
                auto c = const_cast<Il2CppClass*>(api.image_get_class(im, j));
                if (c && same(api.class_get_namespace(c), ns) && same(api.class_get_name(c), name)) {
                    if (found) throw std::runtime_error("Ambiguous input pump class");
                    found = c;
                }
            }
        }
        if (!found) throw std::runtime_error(std::string("Missing input pump class ") + name);
        return found;
    }
    const MethodInfo* method(Il2CppClass* cls, const char* name, Il2CppClass* result,
                             std::initializer_list<Il2CppClass*> args) {
        const MethodInfo* found = nullptr; void* it = nullptr;
        while (auto m = api.class_get_methods(cls, &it)) {
            if (method_owner(m) != cls || !same(api.method_get_name(m), name) ||
                api.method_get_param_count(m) != args.size() || from_type(api.method_get_return_type(m)) != result) continue;
            std::uint32_t impl = 0; auto flags = api.method_get_flags(m, &impl);
            if ((flags & 7u) != 6u || !(flags & 16u)) continue;
            unsigned i = 0; bool match = true;
            for (auto arg : args) if (from_type(api.method_get_param(m, i++)) != arg) match = false;
            if (!match) continue;
            if (found) throw std::runtime_error(std::string("Ambiguous input pump method ") + name);
            found = m;
        }
        if (!found) throw std::runtime_error(std::string("Missing exact input pump method ") + name);
        return found;
    }
    void bind() {
#define LOAD(member, name) do { member = reinterpret_cast<decltype(member)>(GetProcAddress(game, name)); if (!member) throw std::runtime_error("Missing export " name); } while (false)
        LOAD(from_type, "il2cpp_class_from_type");
        LOAD(method_owner, "il2cpp_method_get_class");
        LOAD(assignable, "il2cpp_class_is_assignable_from");
        LOAD(root_new, "il2cpp_gchandle_new");
        LOAD(root_target, "il2cpp_gchandle_get_target");
        LOAD(root_free, "il2cpp_gchandle_free");
#undef LOAD
        std::string error;
        if (!MainThreadGuard::create(api, domain, guard, error)) throw std::runtime_error(error);
        auto helper = klass("Rewired_Core.dll", "Rewired", "ReInput");
        auto void_type = klass("mscorlib.dll", "System", "Void");
        action = klass("mscorlib.dll", "System", "Action");
        system_type = klass("mscorlib.dll", "System", "Type");
        auto marshal = klass("mscorlib.dll", "System.Runtime.InteropServices", "Marshal");
        auto intptr_type = klass("mscorlib.dll", "System", "IntPtr");
        auto delegate_type = klass("mscorlib.dll", "System", "Delegate");
        add = method(helper, "add_InputSourceUpdateEvent", void_type, {action});
        remove = method(helper, "remove_InputSourceUpdateEvent", void_type, {action});
        convert = method(marshal, "GetDelegateForFunctionPointer", delegate_type, {intptr_type, system_type});
    }
    Il2CppObject* invoke(const MethodInfo* m, void** args) {
        auto r = api.invoke(m, nullptr, args);
        if (r.exception) throw std::runtime_error("Managed input pump invocation raised an exception");
        return r.value;
    }
    static unsigned reserve() {
        auto n = next_slot.load(std::memory_order_relaxed);
        do { if (n >= capacity) throw std::runtime_error("Input pump callback capacity exhausted"); }
        while (!next_slot.compare_exchange_weak(n, n + 1, std::memory_order_relaxed));
        return n;
    }
    template<std::size_t N> static void __cdecl callback() noexcept { dispatch(static_cast<unsigned>(N)); }
    template<std::size_t... N> static constexpr std::array<Callback, sizeof...(N)> entries(std::index_sequence<N...>) {
        return {{&callback<N>...}};
    }
    struct BusyScope {
        std::atomic<bool>& flag;
        ~BusyScope() { flag.store(false, std::memory_order_release); }
    };
    static void dispatch(unsigned n) noexcept {
        auto* p = owners[n].load(std::memory_order_acquire);
        if (!p) return;
        p->inflight.fetch_add(1, std::memory_order_acq_rel);
        struct Flight { Impl* p; ~Flight() { p->inflight.fetch_sub(1, std::memory_order_acq_rel); } } flight{p};
        if (owners[n].load(std::memory_order_acquire) != p || p->done.load(std::memory_order_acquire)) return;
        if (GetCurrentThreadId() != p->os) {
            p->foreign.fetch_add(1, std::memory_order_relaxed);
            p->diagnostic("Input event refused a foreign thread"); p->request_stop(); return;
        }
        if (p->busy.exchange(true, std::memory_order_acq_rel)) {
            p->reentrant.fetch_add(1, std::memory_order_relaxed);
            p->diagnostic("Reentrant input event refused"); p->request_stop(); return;
        }
        BusyScope busy_scope{p->busy};
        try {
            if (p->lost.load(std::memory_order_acquire) || !p->window_valid()) { p->window_gone(); return; }
            if (!p->verify() || p->stopping.load(std::memory_order_acquire)) return;
            const auto now = static_cast<std::uint64_t>(GetTickCount64());
            p->client->input_tick(now);
            std::lock_guard<std::mutex> lock(p->view_mutex);
            ++p->view.input_updates; p->view.last_input_ms = now;
        } catch (...) { p->diagnostic("Input event client tick failed"); p->request_stop(); }
    }
    void recover() {
        if (lost.load(std::memory_order_acquire) || !window_valid()) { window_gone(); return; }
        if (busy.exchange(true, std::memory_order_acq_rel)) {
            reentrant.fetch_add(1, std::memory_order_relaxed);
            diagnostic("Reentrant recovery callback refused"); request_stop(); return;
        }
        BusyScope busy_scope{busy};
        if (!verify()) return;
        const auto now = static_cast<std::uint64_t>(GetTickCount64());
        try {
            client->recovery_tick(now);
            { std::lock_guard<std::mutex> lock(view_mutex); ++view.recovery_updates; view.last_recovery_ms = now; }
        } catch (...) { diagnostic("Input pump recovery tick failed"); request_stop(); }
        if (!client->stopped()) return;
        // Run-end, death or a backend failure can finish the client without an
        // external producer stop. Its event registration must retire as well.
        if (!stopping.load(std::memory_order_acquire)) request_stop();
        if (pending && !removed) {
            try {
                auto* value = root_target(action_root);
                if (!value) throw std::runtime_error("Missing input Action root");
                void* args[]{value}; invoke(remove, args);
                removed = true;
                owners[slot].store(nullptr, std::memory_order_release);
            } catch (...) { diagnostic("Input event removal failed; retrying"); return; }
        }
        if (inflight.load(std::memory_order_acquire) != 0) return;
        if (slot < capacity) owners[slot].store(nullptr, std::memory_order_release);
        if (action_root) { root_free(action_root); action_root = nullptr; }
        pending = false;
        { std::lock_guard<std::mutex> lock(view_mutex); view.subscription_pending = false; view.cleanup_complete = true; }
        done.store(true, std::memory_order_release);
    }
    static void recovery_callback(void* context) noexcept {
        auto* p = static_cast<Impl*>(context);
        try { p->recover(); }
        catch (...) { p->diagnostic("Input pump recovery failed"); p->request_stop(); }
    }
    static DWORD WINAPI worker(void* context) noexcept {
        auto* p = static_cast<Impl*>(context);
        WaitForSingleObject(p->gate, INFINITE);
        while (!p->done.load(std::memory_order_acquire)) {
            if (!p->window_valid()) { p->window_gone(); break; }
            try {
                auto result = run_window_hook(p->bridge, p->window, p->os, &recovery_callback, p, 100);
                if (result != HookResult::completed) {
                    const char* empty = nullptr;
                    p->hook_error.compare_exchange_strong(empty, result == HookResult::timeout ?
                        "Input pump recovery dispatch timed out; retrying" : "Input pump recovery dispatch failed; retrying");
                }
            } catch (...) {
                const char* empty = nullptr;
                p->hook_error.compare_exchange_strong(empty, "Input pump recovery dispatch threw; retrying");
            }
            if (!p->done.load(std::memory_order_acquire)) Sleep(16);
        }
        { std::lock_guard<std::mutex> lock(p->view_mutex); p->view.recovery_running = false; }
        return 0;
    }
};
std::array<std::atomic<RewiredInputPump::Impl*>, RewiredInputPump::Impl::capacity> RewiredInputPump::Impl::owners{};
std::atomic<unsigned> RewiredInputPump::Impl::next_slot{0};
std::atomic<RewiredInputPump::Impl*> RewiredInputPump::Impl::registry{nullptr};
RewiredInputPump::RewiredInputPump() = default;
RewiredInputPump::~RewiredInputPump() { stop(); }

bool RewiredInputPump::initialize(HMODULE game, Il2CppApi api, Il2CppDomain* domain, HMODULE bridge,
                                  HWND window, DWORD os_thread, std::int32_t managed_thread, std::string& error) {
    error.clear();
    try {
        if (impl_) throw std::runtime_error("Input pump already initialized");
        if (!game || !bridge || !domain || !window || !os_thread || managed_thread <= 0 ||
            !api.thread_current || !api.thread_current() || !api.domain_get_assemblies || !api.assembly_get_image ||
            !api.image_get_name || !api.image_get_class_count || !api.image_get_class || !api.class_get_name ||
            !api.class_get_namespace || !api.class_get_methods || !api.method_get_name || !api.method_get_param_count ||
            !api.method_get_param || !api.method_get_return_type || !api.method_get_flags || !api.type_get_name || !api.free ||
            !api.runtime_invoke || !api.object_get_class || !api.object_unbox || !api.class_get_type || !api.type_get_object)
            throw std::runtime_error("Incomplete input pump runtime API or unattached thread");
        auto p = std::make_unique<Impl>();
        p->game = game; p->bridge = bridge; p->api = api; p->domain = domain; p->window = window; p->os = os_thread; p->managed = managed_thread;
        if (!p->window_valid()) throw std::runtime_error("Invalid owned Unity window or thread");
        p->bind();
        auto* raw = p.release();
        auto* head = Impl::registry.load(std::memory_order_relaxed);
        do { raw->registry_next = head; } while (!Impl::registry.compare_exchange_weak(head, raw, std::memory_order_release, std::memory_order_relaxed));
        impl_ = raw; return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool RewiredInputPump::start(std::unique_ptr<InputPumpClient> client, std::string& error) {
    error.clear();
    Impl* p = impl_;
    bool worker_created = false, owns_attempt = false;
    try {
        if (!p || !client) throw std::runtime_error("Input pump or client is missing");
        if (p->attempted.exchange(true)) throw std::runtime_error("Input pump may only be started once");
        owns_attempt = true;
        if (!p->verify()) throw std::runtime_error("Input pump start requires the verified main thread");
        if (!p->window_valid()) { p->window_gone(); throw std::runtime_error("Owned Unity window was lost"); }
        if (p->stopping.load()) throw std::runtime_error("Input pump is stopping");
        if (p->busy.exchange(true)) throw std::runtime_error("Reentrant input pump start");
        Impl::BusyScope busy_scope{p->busy};
        p->fault(4);
        p->slot = Impl::reserve();
        constexpr auto callbacks = Impl::entries(std::make_index_sequence<Impl::capacity>{});
        auto callback = callbacks[p->slot];
        HMODULE pinned = nullptr;
        p->fault(1);
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                reinterpret_cast<LPCWSTR>(callback), &pinned) || pinned != p->bridge)
            throw std::runtime_error("Input callback module pin mismatch");
        p->fault(2);
        p->gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!p->gate) throw std::runtime_error("Unable to create recovery gate");
        p->fault(3);
        HANDLE thread = CreateThread(nullptr, 0, &Impl::worker, p, 0, nullptr);
        if (!thread) { CloseHandle(p->gate); p->gate = nullptr; throw std::runtime_error("Unable to create recovery worker"); }
        CloseHandle(thread); worker_created = true;
        p->client = std::move(client);
        p->published_client.store(p->client.get(), std::memory_order_release);
#ifdef WSM_PLAYER_STATE_TESTING
        // Deterministic producer-stop interleaving before client.start().
        if (p->start_fault == 5) p->request_stop();
#endif
        { std::lock_guard<std::mutex> lock(p->view_mutex); p->view.recovery_running = true; }
        if (p->stopping.load(std::memory_order_acquire)) { p->client->request_stop(); throw std::runtime_error("Input pump stopped during start"); }
        std::string client_error;
        if (!p->client->bind_context({p->window, p->os, p->managed}, client_error))
            throw std::runtime_error(client_error.empty() ? "Input client owner binding failed" : client_error);
        if (!p->client->start(client_error)) throw std::runtime_error(client_error.empty() ? "Input client start failed" : client_error);
        if (p->stopping.load(std::memory_order_acquire)) throw std::runtime_error("Input pump stopped during client start");
        auto* type = p->api.type_get_object(p->api.class_get_type(p->action));
        if (!type || !p->assignable(p->system_type, p->api.object_get_class(type))) throw std::runtime_error("Invalid Action type object");
        auto type_root = p->root_new(type, true);
        if (!type_root) throw std::runtime_error("Unable to root Action type");
        struct TypeRoot { Impl* p; void* h; ~TypeRoot() { p->root_free(h); } } type_scope{p, type_root};
        type = p->root_target(type_root);
        if (!type) throw std::runtime_error("Lost Action type root");
        std::intptr_t pointer = reinterpret_cast<std::intptr_t>(callback);
        void* args[]{&pointer, type};
        auto* action = p->invoke(p->convert, args);
        if (!action || p->api.object_get_class(action) != p->action) throw std::runtime_error("Invalid marshalled Action");
        p->action_root = p->root_new(action, true);
        if (!p->action_root || !p->root_target(p->action_root)) throw std::runtime_error("Unable to root input Action");
        Impl::owners[p->slot].store(p, std::memory_order_release);
        p->pending = true;
        { std::lock_guard<std::mutex> lock(p->view_mutex); p->view.subscription_pending = true; }
        void* add_args[]{p->root_target(p->action_root)};
        p->invoke(p->add, add_args);
        if (p->stopping.load(std::memory_order_acquire)) throw std::runtime_error("Input pump stopped during event subscription");
        { std::lock_guard<std::mutex> lock(p->view_mutex); p->view.started = true; }
        SetEvent(p->gate); return true;
    } catch (const std::exception& e) {
        if (p && owns_attempt) p->failed_start(worker_created, e.what());
        error = e.what(); return false;
    } catch (...) {
        if (p && owns_attempt) p->failed_start(worker_created, "Input pump start failed");
        error = "Unknown input pump start failure"; return false;
    }
}
void RewiredInputPump::stop() noexcept { if (impl_) impl_->request_stop(); }
#ifdef WSM_PLAYER_STATE_TESTING
void RewiredInputPump::test_fail_start(unsigned step) { if (impl_) impl_->start_fault = step; }
#endif
RewiredInputPumpState RewiredInputPump::state() const {
    if (!impl_) return {};
    auto& p = *impl_;
    RewiredInputPumpState result;
    { std::lock_guard<std::mutex> lock(p.view_mutex); result = p.view; }
    result.stop_requested = p.stopping.load(std::memory_order_acquire);
    result.window_lost = p.lost.load(std::memory_order_acquire);
    result.foreign_callbacks = p.foreign.load(std::memory_order_relaxed);
    result.reentrant_callbacks = p.reentrant.load(std::memory_order_relaxed);
    if (p.error_status.load(std::memory_order_acquire) == 2) result.error = p.error_text;
    else if (auto* diagnostic = p.hook_error.load(std::memory_order_acquire)) result.error = diagnostic;
    return result;
}
}
