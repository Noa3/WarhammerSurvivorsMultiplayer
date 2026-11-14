#include "wsm/main_thread_guard.h"
#include <cstring>

namespace {
bool same(const char* left, const char* right) { return left && std::strcmp(left, right) == 0; }
Il2CppClass* resolve_class(const wsm::Il2CppApi& api, Il2CppDomain* domain,
                          const char* image_name, const char* ns, const char* name) {
    std::size_t count = 0;
    auto assemblies = api.domain_get_assemblies(domain, &count);
    if (!assemblies) return nullptr;
    Il2CppClass* match = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        auto image = api.assembly_get_image(assemblies[i]);
        if (!image || !same(api.image_get_name(image), image_name)) continue;
        for (std::size_t j = 0; j < api.image_get_class_count(image); ++j) {
            auto klass = const_cast<Il2CppClass*>(api.image_get_class(image, j));
            if (!klass || !same(api.class_get_namespace(klass), ns) || !same(api.class_get_name(klass), name)) continue;
            if (match) return nullptr;
            match = klass;
        }
    }
    return match;
}
const MethodInfo* resolve_getter(const wsm::Il2CppApi& api, Il2CppClass* klass,
                                 const char* name, const char* result, bool is_static) {
    if (!klass) return nullptr;
    const MethodInfo* match = nullptr;
    void* iterator = nullptr;
    while (auto method = api.class_get_methods(klass, &iterator)) {
        if (!same(api.method_get_name(method), name) || api.method_get_param_count(method) != 0) continue;
        std::uint32_t implementation = 0;
        if (((api.method_get_flags(method, &implementation) & 0x10u) != 0) != is_static) continue;
        auto type_name = api.type_get_name(api.method_get_return_type(method));
        const bool matches = same(type_name, result);
        if (type_name) api.free(type_name);
        if (!matches) continue;
        if (match) return nullptr;
        match = method;
    }
    return match;
}
bool read_int(const wsm::Il2CppApi& api, const wsm::Il2CppApi::Invocation& invocation, std::int32_t& value) {
    if (invocation.exception || !invocation.value) return false;
    auto klass = api.object_get_class(invocation.value);
    if (!klass || !same(api.class_get_namespace(klass), "System") || !same(api.class_get_name(klass), "Int32")) return false;
    const void* data = api.object_unbox(invocation.value);
    if (!data) return false;
    std::memcpy(&value, data, sizeof(value));
    return true;
}
}
namespace wsm {
bool MainThreadGuard::create(const Il2CppApi& api, Il2CppDomain* domain, MainThreadGuard& result, std::string& error) {
    error.clear();
    if (!domain || !api.thread_current || !api.thread_current()) {
        error = "Main-thread metadata resolution requires an initialized attached runtime."; return false;
    }
    MainThreadGuard candidate;
    candidate.api_ = api;
    auto synchronization = resolve_class(api, domain, "mscorlib.dll", "System.Threading", "SynchronizationContext");
    auto thread = resolve_class(api, domain, "mscorlib.dll", "System.Threading", "Thread");
    candidate.unity_context_ = resolve_class(api, domain, "UnityEngine.CoreModule.dll", "UnityEngine", "UnitySynchronizationContext");
    candidate.current_context_ = resolve_getter(api, synchronization, "get_Current", "System.Threading.SynchronizationContext", true);
    candidate.current_thread_ = resolve_getter(api, thread, "get_CurrentThread", "System.Threading.Thread", true);
    candidate.managed_thread_id_ = resolve_getter(api, thread, "get_ManagedThreadId", "System.Int32", false);
    candidate.main_thread_id_ = resolve_getter(api, candidate.unity_context_, "get_MainThreadId", "System.Int32", false);
    if (!candidate.unity_context_ || !candidate.current_context_ || !candidate.current_thread_ || !candidate.managed_thread_id_ || !candidate.main_thread_id_) {
        error = "Missing or ambiguous main-thread metadata: unity_context=" + std::to_string(candidate.unity_context_ != nullptr) +
            " current_context=" + std::to_string(candidate.current_context_ != nullptr) +
            " current_thread=" + std::to_string(candidate.current_thread_ != nullptr) +
            " managed_id=" + std::to_string(candidate.managed_thread_id_ != nullptr) +
            " engine_main_id=" + std::to_string(candidate.main_thread_id_ != nullptr);
        return false;
    }
    result = candidate;
    return true;
}
bool MainThreadGuard::verify(DWORD expected_os_thread, std::int32_t expected_managed_thread,
                             MainThreadIdentity& identity, std::string& error) const {
    identity = {}; error.clear(); identity.os_thread = GetCurrentThreadId();
    if (!expected_os_thread || identity.os_thread != expected_os_thread) { error = "Native OS thread mismatch."; return false; }
    if (!unity_context_ || !api_.thread_current || !api_.thread_current()) { error = "Runtime thread is not attached or guard is not initialized."; return false; }
    const auto context = api_.invoke(current_context_, nullptr, nullptr);
    if (context.exception || !context.value || api_.object_get_class(context.value) != unity_context_) {
        error = "Current synchronization context is absent or is not UnitySynchronizationContext."; return false;
    }
    const auto thread = api_.invoke(current_thread_, nullptr, nullptr);
    if (thread.exception || !thread.value ||
        !read_int(api_, api_.invoke(main_thread_id_, context.value, nullptr), identity.engine_main_thread) ||
        !read_int(api_, api_.invoke(managed_thread_id_, thread.value, nullptr), identity.managed_thread)) {
        error = "Thread identity getter failed or returned a non-Int32 result."; return false;
    }
    if (identity.engine_main_thread <= 0 || identity.engine_main_thread != identity.managed_thread ||
        (expected_managed_thread > 0 && identity.managed_thread != expected_managed_thread)) {
        error = "Managed current thread does not match the independently observed Unity context/caller."; return false;
    }
    return true;
}
}
