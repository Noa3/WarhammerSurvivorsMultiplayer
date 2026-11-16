#include "wsm/upgrade_pass_hook.h"
#include "wsm/upgrade_pass_profile.h"
#include <filesystem>
#include <vector>

namespace wsm {
UpgradePassHookState UpgradePassHook::state() const {
    return {prepared_, patch_.active() || patch_.protection_pending(),
            verified_applied_, patch_.protection_pending()};
}
bool UpgradePassHook::prepare(HMODULE game, std::string& error) {
    error.clear();
    if (state().ownership_retained) { error="Upgrade hook already owns runtime state"; return false; }
    prepared_=false;
    if (!verify_upgrade_pass_image(game,false,error)) return false;
    std::vector<wchar_t> name(32768);
    const DWORD count=GetModuleFileNameW(game,name.data(),static_cast<DWORD>(name.size()));
    if (!count || count>=name.size()) { error="Cannot resolve loaded GameAssembly path"; return false; }
    const std::filesystem::path assembly(std::wstring(name.data(),count));
    const auto metadata=assembly.parent_path()/L"WarhammerSurvivors_Data/il2cpp_data/Metadata/global-metadata.dat";
    if (!verify_upgrade_pass_files(assembly,metadata,error)) return false;
    game_=game;prepared_=true;
    return true;
}
bool UpgradePassHook::install(DWORD, const MainThreadGuard&,
                              const PlayerStateReader&, std::string& error) {
    error="Upgrade hook mutation disabled: no verified quiescent patch window";
    return false;
}
bool UpgradePassHook::remove(DWORD, const MainThreadGuard&,
                             const PlayerStateReader&, std::string& error) {
    error="Upgrade hook mutation disabled: no verified quiescent patch window";
    return false;
}
}
