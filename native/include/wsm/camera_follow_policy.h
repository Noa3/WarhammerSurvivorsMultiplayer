#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wsm {
enum class CameraFollowMode : std::int32_t { OwnPlayer = 0, SharedAlive = 1 };
struct CameraSubject {
    std::int32_t input_id = -1;
    float x = 0, y = 0;
    bool dead = false;
};
struct CameraFocus {
    bool has_target = false, spectating = false;
    std::int32_t input_id = -1;
    float x = 0, y = 0;
};
// Local presentation only. IDs must already refer to this process's actors;
// this function neither authenticates a network participant nor maps one.
// Valid all-dead/empty states publish no target; invalid input preserves output.
bool choose_camera_focus(const std::vector<CameraSubject>& subjects,
                         CameraFollowMode mode, std::int32_t local_input_id,
                         std::int32_t previous_spectator_id,
                         CameraFocus& output, std::string& error);
}
