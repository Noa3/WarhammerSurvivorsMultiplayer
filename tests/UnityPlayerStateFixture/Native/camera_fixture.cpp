#include "wsm/player_camera_controller.h"
#include <cmath>
#include <memory>
#include <string>
#include <thread>

namespace {
std::unique_ptr<wsm::PlayerCameraController> camera;
std::string reason;
int fail(int code, const std::string& message) { reason = message; return code; }
bool close_enough(float a, float b) { return std::fabs(a-b) < 0.001f; }
}
extern "C" __declspec(dllexport) const char* __cdecl fixture_camera_reason() { return reason.c_str(); }
extern "C" __declspec(dllexport) int __cdecl fixture_camera_init(int managed, unsigned os) {
    reason.clear();
    if (camera) return fail(1, "camera owner already exists");
    auto module=GetModuleHandleW(L"GameAssembly.dll"); wsm::Il2CppApi api;
    if (!module || !wsm::Il2CppApi::resolve(module,api,reason) || !api.resolve_probe(module,reason)) return 2;
    camera=std::make_unique<wsm::PlayerCameraController>();
    if (!camera->initialize(module,api,api.domain_get(),os,managed,reason)) return 3;
    return 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_camera_open(int id, int mode, int expect_success) {
    reason.clear(); if (!camera) return fail(4,"no owner");
    const auto result=camera->open(id,static_cast<wsm::CameraFollowMode>(mode),reason);
    return (result==wsm::PlayerReadStatus::Captured)==(expect_success!=0) ? 0 : fail(5,"unexpected open status: "+reason);
}
extern "C" __declspec(dllexport) int __cdecl fixture_camera_mode(int mode) {
    reason.clear(); if (!camera) return fail(6,"no owner");
    return camera->set_mode(static_cast<wsm::CameraFollowMode>(mode),reason)?0:7;
}
extern "C" __declspec(dllexport) unsigned long long __cdecl fixture_camera_updates() {
    return camera ? camera->state().updates : 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_camera_active(int expected) {
    reason.clear(); if (!camera) return fail(14,"no owner");
    const auto state=camera->state();
    return state.attached==(expected!=0) && (expected || !state.has_target) ? 0 :
        fail(15,"stale active camera state after current manager replacement");
}
extern "C" __declspec(dllexport) int __cdecl fixture_camera_state(int has, int spectating, int id, float x, float y, unsigned long long before) {
    reason.clear(); if (!camera) return fail(8,"no owner");
    const auto s=camera->state();
    // The production counter records successful position writes. With no
    // surviving target, both the Transform and this counter must stay still.
    if (!s.attached || s.has_target!=(has!=0) || (has ? s.updates<=before : s.updates!=before) ||
        (has && (s.spectating!=(spectating!=0) || (id!=-99 && s.target_input_id!=id) || !close_enough(s.x,x) || !close_enough(s.y,y))))
        return fail(9,"state mismatch: attached="+std::to_string(s.attached)+" has="+std::to_string(s.has_target)+
            " spectator="+std::to_string(s.spectating)+" id="+std::to_string(s.target_input_id)+
            " xy="+std::to_string(s.x)+","+std::to_string(s.y)+" updates="+std::to_string(s.updates)+" error="+s.error);
    return 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_camera_wrong_thread() {
    reason.clear(); if (!camera) return fail(10,"no owner");
    bool accepted=true; std::string worker_error;
    std::thread worker([&] { accepted=camera->set_mode(wsm::CameraFollowMode::SharedAlive,worker_error); });
    worker.join();
    return !accepted && !worker_error.empty() ? 0 : fail(11,"worker mode change accepted or missing error");
}
extern "C" __declspec(dllexport) int __cdecl fixture_camera_close() {
    reason.clear(); if (!camera) return fail(12,"no owner");
    if (!camera->close(reason)) return 13;
    camera.reset(); return 0;
}
extern "C" __declspec(dllexport) int __cdecl fixture_camera_close_keep() {
    reason.clear(); if (!camera) return fail(16,"no owner");
    return camera->close(reason)?0:17;
}
extern "C" __declspec(dllexport) int __cdecl fixture_camera_diagnostic(int expected) {
    reason.clear(); if (!camera) return fail(18,"no owner");
    const auto state=camera->state();
    return (!state.error.empty())==(expected!=0) ? 0 : fail(19,"unexpected callback generation diagnostic: "+state.error);
}
