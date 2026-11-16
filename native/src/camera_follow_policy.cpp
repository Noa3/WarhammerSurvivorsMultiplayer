#include "wsm/camera_follow_policy.h"
#include <cmath>
#include <set>

namespace wsm {
bool choose_camera_focus(const std::vector<CameraSubject>& subjects,
                         CameraFollowMode mode, std::int32_t local_input_id,
                         std::int32_t previous_spectator_id,
                         CameraFocus& output, std::string& error) {
    error.clear();
    const auto reject = [&](const char* reason) { error = reason; return false; };
    if (mode != CameraFollowMode::OwnPlayer && mode != CameraFollowMode::SharedAlive)
        return reject("Unknown camera mode");
    if (local_input_id < 0 || subjects.size() > 4)
        return reject("Invalid camera identity or roster bound");
    CameraFocus next;
    if (subjects.empty()) { output = next; return true; }
    std::set<std::int32_t> ids;
    const CameraSubject* local = nullptr;
    const CameraSubject* spectator = nullptr;
    const CameraSubject* first_alive = nullptr;
    double x = 0, y = 0;
    std::size_t living = 0;
    for (const auto& actor : subjects) {
        if (actor.input_id < 0 || !ids.insert(actor.input_id).second)
            return reject("Invalid or duplicate camera actor identity");
        if (!std::isfinite(actor.x) || !std::isfinite(actor.y))
            return reject("Non-finite camera actor position");
        if (actor.input_id == local_input_id) local = &actor;
        if (actor.dead) continue;
        if (!first_alive || actor.input_id < first_alive->input_id) first_alive = &actor;
        if (actor.input_id == previous_spectator_id) spectator = &actor;
        x += actor.x; y += actor.y; ++living;
    }
    if (!local) return reject("Local camera actor identity is absent");
    if (!living) { output = next; return true; }
    next.has_target = true;
    next.spectating = local->dead;
    if (mode == CameraFollowMode::SharedAlive) {
        next.x = static_cast<float>(x / living);
        next.y = static_cast<float>(y / living);
        next.input_id = living == 1 ? first_alive->input_id : -1;
    } else {
        const auto* target = !local->dead ? local : spectator ? spectator : first_alive;
        next.input_id = target->input_id;
        next.x = target->x; next.y = target->y;
    }
    output = next;
    return true;
}
}
