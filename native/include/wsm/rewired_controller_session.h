#pragma once
#include "wsm/player_state_reader.h"
#include <memory>
#include <vector>

namespace wsm {
struct RewiredNamedId { std::int32_t id = -1; std::string name; };
struct RewiredActionBinding {
    std::int32_t action_id = -1, element_id = -1, element_type = -1, axis_range = -1;
    bool inverted = false;
};
struct RewiredMapState {
    std::int32_t id = -1, controller_id = -1, controller_type = -1, category_id = -1, layout_id = -1;
    bool enabled = false;
    std::vector<RewiredActionBinding> bindings;
};
struct RewiredAssignedCustomSource {
    std::int32_t controller_id = -1, source_id = -1;
    std::vector<std::int32_t> axis_ids, map_ids;
};
struct RewiredPlayerInputState {
    std::int32_t player_id = -1;
    float horizontal = 0, vertical = 0;
    std::vector<RewiredMapState> maps;
    std::vector<RewiredAssignedCustomSource> custom_sources;
};
struct RewiredControlEnvironment {
    std::int32_t horizontal_action_id = -1, vertical_action_id = -1;
    std::vector<RewiredNamedId> categories, layouts;
    std::vector<RewiredPlayerInputState> players;
    bool custom_sources_captured = false;
};
struct RewiredControlOptions {
    std::int32_t player_id = -1, source_id = -1, category_id = -1, layout_id = -1;
    std::int32_t horizontal_axis_id = -1, vertical_axis_id = -1;
};
struct RewiredOwnedControlState {
    std::int32_t player_id = -1, controller_id = -1, map_id = -1;
    bool controller_created = false, controller_attached = false, map_registered = false;
    bool cleanup_required = false;
    bool factory_outcome_unknown = false, primary_verified = false, release_verified = false;
    std::int32_t last_controller_id = -1, last_map_id = -1;
    std::int32_t global_customs_before = -1, global_customs_after = -1;
    std::string observation_error;
    float horizontal = 0, vertical = 0;
};
// All operations require the verified Unity main thread. Owns only one newly
// created custom controller/map; never removes or changes existing resources.
// The caller must supply a bounded lease/update driver before nonzero writes.
class RewiredControllerSession {
public:
    RewiredControllerSession();
    ~RewiredControllerSession();
    RewiredControllerSession(const RewiredControllerSession&) = delete;
    RewiredControllerSession& operator=(const RewiredControllerSession&) = delete;
    bool initialize(HMODULE, Il2CppApi, Il2CppDomain*, DWORD, std::int32_t, std::string&);
    PlayerReadStatus inspect(RewiredControlEnvironment&, std::string&);
    PlayerReadStatus inspect_sources(RewiredControlEnvironment&, std::string&);
    bool open(const RewiredControlOptions&, std::string&);
    bool set_axes(float horizontal, float vertical, std::string&);
    bool close(std::string&);
    [[nodiscard]] RewiredOwnedControlState state() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
