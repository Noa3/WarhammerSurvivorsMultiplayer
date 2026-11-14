#pragma once
#include "wsm/il2cpp_api.h"
#include <memory>
#include <vector>

namespace wsm {
struct PlayerStateSnapshot {
    std::int32_t slot = 0, input_id = 0, character_type = 0, level = 0;
    float x = 0, y = 0, hp = 0, xp = 0;
    bool dead = false;
};
struct SceneStateSnapshot { std::int32_t stage_type = 0; std::vector<PlayerStateSnapshot> players; };
struct UpgradeActorContext {
    std::int32_t slot = 0, input_id = 0, character_type = 0;
    bool weapons_manager_present = false, accessories_manager_present = false;
    bool accessories_collection_present = false;
};
struct UpgradeContextSnapshot {
    std::vector<UpgradeActorContext> actors;
    std::int32_t level_up_page_count = 0;
    bool page_game_session_present = false, active_character_present = false, level_up_factory_present = false;
    // -1: no active reference; -2: reference outside MainPlayers; otherwise exact actor slot.
    std::int32_t active_character_slot = -1;
    bool choice_group_present = false, choice_group_items_present = false;
    // Managed reference presence only; neither active state nor Unity native liveness.
    std::vector<bool> choice_group_item_references;
};
enum class PlayerReadStatus { Captured, NotReady, Error };
// Metadata is borrowed for the runtime lifetime. The expected IDs must be
// independently established by the caller; managed_thread may be zero.
class PlayerStateReader {
public:
    PlayerStateReader();
    ~PlayerStateReader();
    PlayerStateReader(const PlayerStateReader&) = delete;
    PlayerStateReader& operator=(const PlayerStateReader&) = delete;
    bool initialize(HMODULE module, Il2CppApi api, Il2CppDomain* domain, DWORD native_thread,
                    std::int32_t managed_thread, std::string& error);
    [[nodiscard]] PlayerReadStatus read(SceneStateSnapshot& output, std::string& error) const;
    // Explicit diagnostic only. Presence means a managed reference, not Unity native-object liveness.
    // Optional metadata is resolved here, never required by normal initialize/read.
    [[nodiscard]] PlayerReadStatus read_upgrade_context(UpgradeContextSnapshot& output, std::string& error) const;
#ifdef WSM_PLAYER_STATE_TESTING
    void test_fail_next_scene_root(bool players);
    void test_use_wrong_equipment_collection();
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
