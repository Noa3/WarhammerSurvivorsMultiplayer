#pragma once
#include "wsm/camera_follow_policy.h"
#include "wsm/player_state_reader.h"
#include <memory>

namespace wsm {
struct PlayerCameraState {
    bool attached = false, has_target = false, spectating = false;
    std::int32_t local_input_id = -1, target_input_id = -1;
    CameraFollowMode mode = CameraFollowMode::OwnPlayer;
    std::uint64_t updates = 0;
    float x = 0, y = 0;
    std::string error;
};
// Uses GameManager.ManualCameraTargetControl, never a machine-code patch.
// Initialize/open/mode/state/close are verified-main-thread operations. One
// camera owner per process; refuse rather than overwrite a foreign callback.
class PlayerCameraController {
public:
    PlayerCameraController();
    ~PlayerCameraController();
    PlayerCameraController(const PlayerCameraController&) = delete;
    PlayerCameraController& operator=(const PlayerCameraController&) = delete;
    bool initialize(HMODULE module, Il2CppApi api, Il2CppDomain* domain,
                    DWORD native_thread, std::int32_t managed_thread,
                    std::string& error);
    // Requires a running stage, unique live manager and normal stock camera.
    // NotReady does not install a callback. Error may retain an owned callback
    // after a setter's partial effect; caller must keep this owner and close.
    PlayerReadStatus open(std::int32_t local_input_id, CameraFollowMode mode,
                          std::string& error);
    bool set_mode(CameraFollowMode mode, std::string& error);
    PlayerCameraState state() const;
    // A failed close retains callback/root ownership for a later safe retry.
    bool close(std::string& error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
