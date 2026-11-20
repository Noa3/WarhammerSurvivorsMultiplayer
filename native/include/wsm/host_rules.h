#pragma once

namespace wsm {
// Lobby configuration only. No actor creation or run control.
struct HostRules {
    int bots = 0, difficulty = 1, death = 0;
    bool defaults() const { return bots == 0 && difficulty == 1 && death == 0; }
    bool valid() const { return bots >= 0 && bots <= 8 && difficulty >= 0 && difficulty <= 2 && death >= 0 && death <= 1; }
};
}