#pragma once
#include "wsm/player_state_reader.h"

namespace wsm {
struct RewiredControllerDefinitionSnapshot {
    std::int32_t source_id = -1;
    std::vector<std::int32_t> axis_ids;
};
struct RewiredInputProfileSnapshot {
    std::int32_t manager_count = 0;
    std::int32_t horizontal_action_id = -1, vertical_action_id = -1;
    std::vector<RewiredControllerDefinitionSnapshot> definitions;
};
// Explicit read-only discovery on the verified Unity main thread. This does not
// create controllers, alter maps, set axes, or establish input isolation.
// A captured empty definition list is a complete negative within the 16-entry
// bound. Each definition has at most 32 unique nonnegative axis identifiers.
// Error/NotReady leave the entire caller snapshot unchanged.
[[nodiscard]] PlayerReadStatus read_rewired_input_profile(
    HMODULE module, Il2CppApi api, Il2CppDomain* domain, DWORD native_thread,
    std::int32_t managed_thread, RewiredInputProfileSnapshot& output, std::string& error);
}
