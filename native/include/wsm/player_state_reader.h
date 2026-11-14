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
struct EquipmentItemSnapshot {
    bool reference_present = false, is_weapon = false, fields_observed = false, owner_present = false;
    std::int32_t weapon_type = 0, owner_slot = -1;
    std::string runtime_type;
};
struct EquipmentActorSnapshot {
    std::int32_t slot = 0, input_id = 0, character_type = 0;
    bool manager_present = false, collection_present = false;
    std::vector<EquipmentItemSnapshot> items;
};
struct EquipmentSnapshot { std::vector<EquipmentActorSnapshot> actors; };
struct UpgradeActorContext {
    std::int32_t slot = 0, input_id = 0, character_type = 0;
    bool weapons_manager_present = false, accessories_manager_present = false;
    bool accessories_collection_present = false;
};
struct UpgradeChoiceItemSnapshot {
    bool child_present = false, child_unity_alive = false;
    bool component_present = false, component_unity_alive = false;
    bool fields_observed = false, data_present = false, is_new = false;
    std::int32_t weapon_type = 0;
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
    bool choice_selection_observed = false;
    std::vector<bool> choice_group_item_unity_alive, choice_group_item_active;
    // Diagnostic first Unity-reported live/active entry, not proven stock call-time selection.
    std::int32_t selected_choice_container_slot = -1;
    // False/-1 when not requested or no container was selected; never a truncated prefix.
    bool choice_items_observed = false;
    std::int32_t choice_child_count = -1;
    std::vector<UpgradeChoiceItemSnapshot> choice_items;
    bool fourth_option_hint_observed = false, fourth_option_hint_present = false;
    bool fourth_option_hint_unity_alive = false, fourth_option_hint_comparison_complete = false;
    std::int32_t fourth_option_hint_child_slot = -1;
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
    // Separate opt-in managed-reference inventory: at most 32 equipment entries per actor.
    // Owner slot is -1 for null and -2 for an owner outside the captured MainPlayers.
    // This neither establishes Unity liveness nor proves a weapon hit.
    [[nodiscard]] PlayerReadStatus read_equipment(EquipmentSnapshot& output, std::string& error) const;
    // Explicit diagnostic only. Presence means a managed reference, not Unity native-object liveness.
    // Optional metadata is resolved here, never required by normal initialize/read.
    [[nodiscard]] PlayerReadStatus read_upgrade_context(UpgradeContextSnapshot& output, std::string& error) const;
    // Separate opt-in query: additionally invokes validated Unity engine observation methods.
    [[nodiscard]] PlayerReadStatus read_upgrade_selection(UpgradeContextSnapshot& output, std::string& error) const;
    // Separate opt-in query: at most 32 direct children of the selected container.
    [[nodiscard]] PlayerReadStatus read_upgrade_items(UpgradeContextSnapshot& output, std::string& error) const;
    // Separate opt-in query: compare the page hint with the same bounded child GameObjects.
    [[nodiscard]] PlayerReadStatus read_upgrade_hint(UpgradeContextSnapshot& output, std::string& error) const;
#ifdef WSM_PLAYER_STATE_TESTING
    void test_fail_next_scene_root(bool players);
    void test_use_wrong_equipment_collection();
#endif
private:
    PlayerReadStatus read_upgrade_context_impl(UpgradeContextSnapshot& output, std::string& error, bool observe_selection, bool observe_items, bool observe_hint) const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
