#include "wsm/player_camera_controller.h"
#include "wsm/player_state_reader.h"
#include <iostream>
#include <memory>

// Script only the engine boundary. Compile and execute the real in-game loop
// below; the separate IL2CPP fixture verifies the actual controller/reader.
namespace wsm {
struct CameraLoopControllerBoundary {
    PlayerCameraState value;
    bool allow_close = true;
    int close_calls = 0, open_calls = 0;
    std::int32_t opened_id = -1;
    CameraFollowMode opened_mode = CameraFollowMode::SharedAlive;
    bool initialize(HMODULE, Il2CppApi, Il2CppDomain*, DWORD, std::int32_t, std::string& error) {
        error.clear(); return true;
    }
    PlayerReadStatus open(std::int32_t id, CameraFollowMode mode, std::string& error) {
        ++open_calls; opened_id = id; opened_mode = mode;
        error.clear(); value = {}; value.attached = true;
        return PlayerReadStatus::Captured;
    }
    bool set_mode(CameraFollowMode mode, std::string& error) { value.mode = mode; error.clear(); return true; }
    PlayerCameraState state() const { return value; }
    bool close(std::string& error) {
        ++close_calls;
        if (!allow_close) { error = "scripted close failure"; return false; }
        error.clear(); value.attached = false; value.has_target = false; return true;
    }
};
struct CameraLoopLifecycleBoundary {
    bool running = true;
    bool initialize(HMODULE, Il2CppApi, Il2CppDomain*, DWORD, std::int32_t, std::string& error) {
        error.clear(); return true;
    }
    PlayerReadStatus read_run_state(bool& result, std::string& error) {
        error.clear(); result = running; return PlayerReadStatus::Captured;
    }
};
}
#define PlayerCameraController CameraLoopControllerBoundary
#define PlayerStateReader CameraLoopLifecycleBoundary
#include "../../native/src/ingame_loop.cpp"
#undef PlayerStateReader
#undef PlayerCameraController

namespace {
void quiet(const std::string&) {}
void prepare(Context& context, bool attached, const std::string& error) {
    context.camera = std::make_unique<wsm::CameraLoopControllerBoundary>();
    context.camera_initialized = true; context.camera_open = true; context.log = quiet;
    context.camera->value.attached = attached;
    context.camera->value.has_target = attached;
    context.camera->value.error = error;
}
}
int main() {
    int failures = 0;
    auto check = [&](const char* name, bool pass) {
        std::cout << "CAMERA_LOOP " << name << '=' << (pass ? "PASS" : "FAIL") << '\n';
        if (!pass) ++failures;
    };
    wsm::MainThreadIdentity identity;
    {
        Context context; prepare(context, true, "scripted callback failure");
        tick_camera(context, identity);
        check("attached_error_closes",context.camera->close_calls == 1 && !context.camera_open &&
            context.camera_failed && context.camera_error == "scripted callback failure" && context.camera->open_calls == 0);
    }
    {
        Context context; prepare(context, true, "scripted callback failure"); context.camera->allow_close = false;
        tick_camera(context, identity);
        check("attached_error_retains_failed_close",context.camera->close_calls == 1 && context.camera_open &&
            context.camera_failed && context.camera->open_calls == 0);
        context.camera->allow_close = true;
        tick_camera(context, identity);
        check("pending_close_retried",context.camera->close_calls == 2 && !context.camera_open &&
            context.camera_failed && context.camera->open_calls == 0);
    }
    {
        Context context; prepare(context, false, "");
        tick_camera(context, identity);
        check("inactive_owner_closed_before_reopen",context.camera->close_calls == 1 && context.camera->open_calls == 1 &&
            context.camera_open && !context.camera_failed && context.camera->opened_id == 0 &&
            context.camera->opened_mode == wsm::CameraFollowMode::OwnPlayer);
    }
    {
        Context context; prepare(context, true, ""); context.camera_failed = true; context.camera_lifecycle.running = false;
        tick_camera(context, identity);
        check("stopped_run_resets_failure",context.camera->close_calls == 1 && !context.camera_open &&
            !context.camera_failed && context.camera_error.empty());
    }
    std::cout << "CAMERA_LOOP RESULT failures=" << failures << '\n';
    return failures ? 1 : 0;
}
