#include "wsm/rewired_control_pulse.h"
#include "wsm/input_lease.h"
#include "wsm/window_hook.h"
#include <cmath>
#include <mutex>

namespace wsm {
namespace {
std::mutex pulse_gate;
struct Pulse {
    HWND window;
    RewiredControllerSession& control;
    PlayerStateReader& reader;
    InputLease lease;
    RewiredPulseResult result;
    bool stopping = false, failed = false;
    std::string error;
};
void failed(Pulse& p, const std::string& error) {
    p.lease.stop();p.stopping=p.failed=true;
    if(p.error.empty())p.error=error;
}
// Emergency zero/removal must not depend on scene reading or actor liveness.
// A new potentially mutating attempt invalidates every older neutrality proof.
void neutralize_tick(void* raw) {
    auto& p=*static_cast<Pulse*>(raw);
    p.lease.stop();p.stopping=true;p.result.neutralized=false;
    if(p.result.resources_closed){p.result.neutralized=true;++p.result.updates;return;}
    std::string write_error;
    try {
        if(p.control.set_axes(0,0,write_error)){p.result.neutralized=true;++p.result.updates;return;}
    }catch(const std::exception& e){write_error=e.what();}
    catch(...){write_error="Unknown zero-setter failure";}
    failed(p,"Owned zero write/readback failed: "+write_error);
    std::string cleanup_error;
    try {
        if(p.control.close(cleanup_error)){p.result.resources_closed=p.result.neutralized=true;++p.result.updates;return;}
    }catch(const std::exception& e){cleanup_error=e.what();}
    catch(...){cleanup_error="Unknown owned cleanup failure";}
    p.error+="; owned cleanup failed: "+cleanup_error;
}
void observe_tick(void* raw) {
    auto& p=*static_cast<Pulse*>(raw);
    try {
        SceneStateSnapshot scene;std::string error;
        const auto status=p.reader.read(scene,error);
        if(status!=PlayerReadStatus::Captured||scene.players.size()!=2||scene.players[0].input_id!=0||scene.players[1].input_id!=1||scene.players[0].dead||scene.players[1].dead)
            failed(p,error.empty()?"Pulse requires the same two living run actors":error);
        else {if(p.result.before.players.empty())p.result.before=scene;p.result.after=std::move(scene);}
    }catch(const std::exception& e){failed(p,e.what());}
    catch(...){failed(p,"Unknown owned pulse observation failure");}
    if(GetForegroundWindow()!=p.window){p.result.focus_lost=true;p.stopping=true;}
}
void pulse_tick(void* raw) {
    auto& p=*static_cast<Pulse*>(raw);
    observe_tick(raw);
    if(p.stopping){neutralize_tick(raw);return;}
    const auto axes=p.lease.sample(GetTickCount64());
    if(axes.horizontal==0&&axes.vertical==0){neutralize_tick(raw);return;}
    if(p.result.resources_closed){failed(p,"Owned resources already closed");return;}
    p.result.neutralized=false;
    try {
        std::string error;
        if(p.control.set_axes(axes.horizontal,axes.vertical,error)){++p.result.updates;p.result.nonzero_written=true;return;}
        failed(p,"Owned nonzero write failed: "+error);
    }catch(const std::exception& e){failed(p,e.what());}
    catch(...){failed(p,"Unknown owned pulse write failure");}
    neutralize_tick(raw);
}
}
bool run_rewired_control_pulse(HMODULE bridge, HWND window, DWORD thread,
    RewiredControllerSession& control, PlayerStateReader& reader,
    float horizontal, float vertical, std::uint32_t duration_ms,
    RewiredPulseResult& output, std::string& error) {
    error.clear();
    std::unique_lock lock(pulse_gate, std::try_to_lock);
    DWORD pid = 0;
    if (!lock.owns_lock() || !bridge || !window || !thread ||
        GetWindowThreadProcessId(window, &pid) != thread || pid != GetCurrentProcessId() ||
        !std::isfinite(horizontal) || !std::isfinite(vertical) || std::abs(horizontal) > 1 || std::abs(vertical) > 1 ||
        (horizontal == 0 && vertical == 0) || !duration_ms || duration_ms > InputLease::maximum_duration_ms) {
        error = "Invalid, overlapping or unowned bounded input pulse";
        return false;
    }
    const auto owned = control.state();
    if (owned.player_id != 1 || !owned.controller_created || !owned.controller_attached || !owned.map_registered) {
        error = "Pulse requires a verified owned second-player controller/map";
        return false;
    }
    Pulse p{window, control, reader};
    const auto start = GetTickCount64();
    if (!p.lease.begin(1, 1) || !p.lease.submit(1, 1, horizontal, vertical, start, duration_ms)) {
        error = "Input lease was refused";
        return false;
    }
    // No detached worker retains the callback context. The existing hook waits
    // for every started callback to finish before this stack can be released.
    do {
        if (run_window_hook(bridge, window, thread, pulse_tick, &p, 1000) != HookResult::completed) {
            p.failed = true; p.error = "Owned pulse dispatch did not complete"; break;
        }
        if (p.stopping || GetTickCount64() - start >= duration_ms) break;
        Sleep(4);
    } while (IsWindow(window) && p.result.updates < 64);
    p.stopping = true;
    p.lease.stop();
    // Explicitly neutralize, including focus loss and failed dispatch. Failed
    // engine calls are reported, never converted to a successful cleanup claim.
    bool zero_confirmed = false;
    for (int attempt = 0; attempt < 3 && IsWindow(window); ++attempt) {
        const auto prior_updates = p.result.updates;
        const auto status = run_window_hook(bridge, window, thread, neutralize_tick, &p, 1000);
        if (status == HookResult::completed && p.result.updates > prior_updates && p.result.neutralized) {
            zero_confirmed = true;
            break;
        }
    }
    p.result.neutralized = zero_confirmed;
    if (zero_confirmed && IsWindow(window)) {
        Sleep(20);
        if (run_window_hook(bridge, window, thread, observe_tick, &p, 1000) != HookResult::completed) {
            p.failed = true; p.error = "Post-neutral observation did not complete";
        }
    }
    if (!zero_confirmed) {
        p.failed = true;
        p.error = "Owned pulse neutralization could not be verified; retain ownership and stop";
    }
    output = std::move(p.result);
    error = std::move(p.error);
    if (!p.failed && (!output.nonzero_written || output.focus_lost)) error = "Pulse expired or lost focus; do not interpret movement as a valid experiment";
    return !p.failed && !output.focus_lost && output.nonzero_written && output.neutralized;
}
}
