#include "wsm/camera_follow_policy.h"
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    int failures = 0;
    auto check = [&](const char* name, bool value) {
        std::printf("%s %s\n", value ? "PASS" : "FAIL", name);
        if (!value) ++failures;
    };
    using wsm::CameraSubject;
    using wsm::CameraFollowMode;
    wsm::CameraFocus focus;
    std::string error;
    std::vector<CameraSubject> actors{{77, 2, 6, false}, {5, 10, 14, false}};
    auto choose = [&](CameraFollowMode mode, int id = 77, int previous = -1) {
        return wsm::choose_camera_focus(actors, mode, id, previous, focus, error);
    };
    check("camera own follows identity rather than slot", choose(CameraFollowMode::OwnPlayer) &&
          focus.has_target && focus.input_id == 77 && focus.x == 2 && focus.y == 6 && !focus.spectating);
    check("camera shared living centroid", choose(CameraFollowMode::SharedAlive) &&
          focus.has_target && focus.input_id == -1 && focus.x == 6 && focus.y == 10);
    actors[0].dead = true;
    check("camera dead first actor excluded from shared seed", choose(CameraFollowMode::SharedAlive) &&
          focus.has_target && focus.input_id == 5 && focus.x == 10 && focus.y == 14);
    check("camera dead local spectates survivor", choose(CameraFollowMode::OwnPlayer) &&
          focus.spectating && focus.input_id == 5 && focus.x == 10);
    actors.push_back({3, -20, -8, false});
    check("camera spectator selection stable by id", choose(CameraFollowMode::OwnPlayer) && focus.input_id == 3);
    check("camera existing live spectator retained", choose(CameraFollowMode::OwnPlayer, 77, 5) && focus.input_id == 5);
    actors[0].dead = false;
    check("camera revival returns to local actor", choose(CameraFollowMode::OwnPlayer, 77, 5) &&
          !focus.spectating && focus.input_id == 77);
    actors[1].dead = actors[2].dead = true;
    check("camera dead secondary never pulls own target", choose(CameraFollowMode::OwnPlayer) &&
          focus.input_id == 77 && focus.x == 2 && focus.y == 6);
    check("camera sole shared survivor equals its position", choose(CameraFollowMode::SharedAlive) &&
          focus.input_id == 77 && focus.x == 2 && focus.y == 6);
    actors[0].dead = true;
    check("camera all dead publishes no target", choose(CameraFollowMode::OwnPlayer) && !focus.has_target);
    actors.clear();
    check("camera empty lifecycle publishes no target", choose(CameraFollowMode::OwnPlayer) && !focus.has_target);
    actors = {{77, 2, 6, false}, {5, 10, 14, false}};
    auto unchanged = [&] {
        focus = {true, true, 999, 123, -456};
        auto ok = choose(CameraFollowMode::OwnPlayer);
        return !ok && !error.empty() && focus.has_target && focus.spectating && focus.input_id == 999 && focus.x == 123 && focus.y == -456;
    };
    actors[1].input_id = 77;
    check("camera duplicate identity refused atomically", unchanged());
    actors[1].input_id = -2;
    check("camera negative identity refused atomically", unchanged());
    actors[1].input_id = 5;
    actors[1].x = std::numeric_limits<float>::quiet_NaN();
    check("camera nonfinite coordinates refused atomically", unchanged());
    actors[1].x = 10;
    check("camera unknown local identity refused", !choose(CameraFollowMode::OwnPlayer, 999));
    check("camera unknown mode refused", !choose(static_cast<CameraFollowMode>(5)));
    actors.push_back({8, 0, 0, false}); actors.push_back({9, 0, 0, false}); actors.push_back({10, 0, 0, false});
    check("camera oversized roster refused atomically", unchanged());
    return failures ? 1 : 0;
}
