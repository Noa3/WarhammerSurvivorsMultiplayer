#include "wsm/player_observer.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>

namespace {
struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
int failures = 0;
void check(bool ok, const char* name) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}
template<class F> void rejects(F operation, const char* name) {
    try { operation(); check(false, name); }
    catch (const std::invalid_argument&) { }
}
}
int main() {
    const auto previous = std::locale();
    std::locale::global(std::locale(previous, new Comma));
    wsm::SceneStateSnapshot scene;
    scene.stage_type = 7;
    scene.players.push_back({0, 41, 3, 5, 1.25f, -2.5f, 83.5f, 12.25f, false});
    const wsm::MainThreadIdentity identity{17, 3, 3};
    const auto captured = wsm::player_observation_json(wsm::PlayerReadStatus::Captured, scene, identity, 1);
    check(captured.find("\"state\":\"captured\"") != std::string::npos, "capture category");
    check(captured.find("\"x\":1.25") != std::string::npos, "locale-independent float");
    check(captured.find("\"hp\":83.5") != std::string::npos, "float HP is not truncated");
    check(captured.find("\"inputId\":41") != std::string::npos, "observed input id");
    check(captured.find("\"native\":17") != std::string::npos, "observed thread identity");
    std::cout << captured << '\n';
    for (const auto status : {wsm::PlayerReadStatus::NotReady, wsm::PlayerReadStatus::Error}) {
        const auto absent = wsm::player_observation_json(status, scene, identity, 2);
        check(absent.find("\"players\":[]") != std::string::npos && absent.find("\"stageType\"") == std::string::npos, "unavailable state does not publish stale scene");
        std::cout << absent << '\n';
    }
    auto invalid = scene;
    invalid.players[0].x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { wsm::player_observation_json(wsm::PlayerReadStatus::Captured, invalid, identity, 3); }, "nonfinite capture refused");
    invalid.players.clear();
    rejects([&] { wsm::player_observation_json(wsm::PlayerReadStatus::Captured, invalid, identity, 3); }, "empty capture refused");
    rejects([&] { wsm::player_observation_json(wsm::PlayerReadStatus::Captured, scene, {}, 3); }, "unverified capture refused");
    rejects([&] { wsm::player_observation_json(static_cast<wsm::PlayerReadStatus>(99), scene, identity, 3); }, "unknown status refused");
    std::locale::global(previous);
    return failures ? 1 : 0;
}
