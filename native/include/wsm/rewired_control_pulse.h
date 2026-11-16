#pragma once
#include "wsm/rewired_controller_session.h"

namespace wsm {
struct RewiredPulseResult {
    SceneStateSnapshot before, after;
    bool nonzero_written = false, neutralized = false, focus_lost = false, resources_closed = false;
    unsigned updates = 0;
};
// Worker-side bounded experiment using only a current-process Unity window.
// Each engine call executes through the verified main-thread session. This is
// deliberately not advertised as a frame/Rewired-update hook.
bool run_rewired_control_pulse(HMODULE bridge, HWND window, DWORD thread,
    RewiredControllerSession& control, PlayerStateReader& reader,
    float horizontal, float vertical, std::uint32_t duration_ms,
    RewiredPulseResult& output, std::string& error);
}
