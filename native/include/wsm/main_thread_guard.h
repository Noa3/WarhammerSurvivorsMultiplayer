#pragma once
#include "wsm/il2cpp_api.h"

namespace wsm {
struct MainThreadIdentity {
    DWORD os_thread = 0;
    std::int32_t managed_thread = 0;
    std::int32_t engine_main_thread = 0;
};

// Metadata is process-lifetime; managed instances are acquired only inside verify().
class MainThreadGuard {
public:
    static bool create(const Il2CppApi& api, Il2CppDomain* domain, MainThreadGuard& result, std::string& error);
    bool verify(DWORD expected_os_thread, std::int32_t expected_managed_thread,
                MainThreadIdentity& identity, std::string& error) const;
private:
    Il2CppApi api_;
    Il2CppClass* unity_context_ = nullptr;
    const MethodInfo* current_context_ = nullptr;
    const MethodInfo* main_thread_id_ = nullptr;
    const MethodInfo* current_thread_ = nullptr;
    const MethodInfo* managed_thread_id_ = nullptr;
};
}
