// The fixture links the production implementation; it does not reproduce it.
#include "wsm/coop_roster.h"
#include "wsm/il2cpp_api.h"
#include <memory>
#include <string>
#include <vector>

namespace {
std::unique_ptr<wsm::CoopRoster> roster;
std::string reason;
}
extern "C" __declspec(dllexport) const char* __cdecl fixture_reason() { return reason.c_str(); }
extern "C" __declspec(dllexport) int __cdecl fixture_open(unsigned os, int managed) {
    roster.reset(); reason.clear();
    HMODULE module = GetModuleHandleW(L"GameAssembly.dll");
    wsm::Il2CppApi api;
    if (!module || !wsm::Il2CppApi::resolve(module, api, reason) || !api.resolve_probe(module, reason)) return 1;
    auto candidate = std::make_unique<wsm::CoopRoster>();
    if (!candidate->initialize(module, api, api.domain_get(), os, managed, reason)) return 2;
    roster = std::move(candidate); return 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_inspect(int* configured, int* configured_count, int* enrolled, int* enrolled_count) {
    if (!roster) return 3;
    wsm::CoopRosterSnapshot out;
    const auto status = roster->inspect(out, reason);
    if (status != wsm::CoopRosterStatus::Captured) return status == wsm::CoopRosterStatus::NotReady ? 1 : 2;
    if (out.configured_ids.size() > 16 || out.enrolled_ids.size() > 16) return 4;
    *configured_count = static_cast<int>(out.configured_ids.size());
    *enrolled_count = static_cast<int>(out.enrolled_ids.size());
    for (int i = 0; i < *configured_count; ++i) configured[i] = out.configured_ids[i];
    for (int i = 0; i < *enrolled_count; ++i) enrolled[i] = out.enrolled_ids[i];
    return 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_enroll(int id) { return roster && roster->enroll(id, reason) ? 0 : 1; }
extern "C" __declspec(dllexport) int __cdecl fixture_copy_character(int id) { return roster && roster->copy_menu_character(id, reason) ? 0 : 1; }
extern "C" __declspec(dllexport) int __cdecl fixture_select(int id) {
    if (!roster || !roster->select_menu_player(id, reason)) return 1;
    // The independently asserted target case must keep its roster and selection
    // intact if rollback is requested before the original player is restored.
    if (id == 41 && (roster->rollback(reason) || !roster->has_pending_enrollment())) return 2;
    return 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_rollback() { return roster && roster->rollback(reason) ? 0 : 1; }
extern "C" __declspec(dllexport) void __cdecl fixture_close() { roster.reset(); }
