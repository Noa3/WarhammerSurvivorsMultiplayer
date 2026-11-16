#pragma once
#include "wsm/code_byte_patch.h"
#include "wsm/main_thread_guard.h"
#include "wsm/player_state_reader.h"

namespace wsm {
struct UpgradePassHookState {
    bool prepared = false;
    bool ownership_retained = false;
    bool last_verified_applied = false;
    bool protection_pending = false;
};
// Own-process, process-lifetime state. Never discard pending ownership or unload
// the owning bridge while the patch is retained. No destructor changes code.
class UpgradePassHook {
public:
    bool prepare(HMODULE game, std::string& error);
    // Runtime code mutation is unavailable: a stopped run and main-thread
    // identity do not prove that the target code is quiescent.
    bool install(DWORD main_thread, const MainThreadGuard& guard,
                 const PlayerStateReader& reader, std::string& error);
    bool remove(DWORD main_thread, const MainThreadGuard& guard,
                const PlayerStateReader& reader, std::string& error);
    UpgradePassHookState state() const;
private:
    HMODULE game_ = nullptr;
    CodeBytePatch patch_;
    bool prepared_ = false;
    bool verified_applied_ = false;
};
}
