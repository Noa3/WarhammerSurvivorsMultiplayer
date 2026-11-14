#pragma once
#include "wsm/il2cpp_api.h"
#include <memory>
#include <vector>

namespace wsm {
enum class CoopRosterStatus { Captured, NotReady, Error };
struct CoopRosterSnapshot {
    std::vector<std::int32_t> configured_ids;
    std::vector<std::int32_t> enrolled_ids;
};
struct CoopCharacterConfiguration {
    std::vector<std::int32_t> selected_types;
    bool coop_config_present = false;
    bool is_multiplayer = false;
    std::int32_t selected_player_id = -1;
    std::int32_t selected_player_index = -1;
};

// Experimental installed-version metadata contract, not a supported game API.
// Enrollment is an explicit outside-run experiment; the caller must establish
// that phase. This class neither creates characters nor establishes input control.
// Rollback restores only roster membership, not unknown native events/save effects.
// Keep this object within the runtime's lifetime and serialize all operations.
class CoopRoster {
public:
    CoopRoster();
    ~CoopRoster();
    CoopRoster(const CoopRoster&) = delete;
    CoopRoster& operator=(const CoopRoster&) = delete;
    bool initialize(HMODULE module, Il2CppApi api, Il2CppDomain* domain,
                    DWORD os_thread, std::int32_t managed_thread, std::string& error);
    CoopRosterStatus inspect(CoopRosterSnapshot& output, std::string& error) const;
    // Optional read-only diagnostic contract. Missing selection/config metadata
    // is an error, not an empty or default character selection.
    bool inspect_character_configuration(CoopCharacterConfiguration& output, std::string& error) const;
    bool enroll(std::int32_t configured_player_id, std::string& error);
    // Selects an already enrolled player for the stock menu, not a hero. The
    // caller must restore its original player before rollback (otherwise refused)
    // and stay outside a run.
    bool select_menu_player(std::int32_t enrolled_player_id, std::string& error);
    // Copies only the stock menu's currently bought character into an existing
    // owned-roster CoopSelections entry. Does not buy/unlock or create entries.
    // This explicit choice is not undone by membership-only rollback.
    bool copy_menu_character(std::int32_t enrolled_player_id, std::string& error);
    bool rollback(std::string& error);
    bool has_pending_enrollment() const noexcept;
private:
    struct Impl;
    // An unrolled-back instance intentionally retains its bounded roots until
    // process exit rather than making hidden engine calls during destruction.
    std::unique_ptr<Impl> impl_;
};
}
