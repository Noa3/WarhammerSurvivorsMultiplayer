#include "wsm/input_lease.h"
#include <cmath>
#include <limits>

namespace wsm {
bool InputLease::begin(std::uint64_t epoch, std::int32_t remote_player) {
    std::lock_guard lock(mutex_);
    if (active_ || !epoch || epoch <= epoch_ || remote_player < 1 || remote_player > 3) return false;
    epoch_ = epoch;
    sequence_ = issued_ = deadline_ = 0;
    x_ = y_ = 0;
    active_ = enabled_ = true;
    accepted_ = seen_ = false;
    return true;
}
bool InputLease::submit(std::uint64_t epoch, std::uint64_t sequence, float x, float y,
                        std::uint64_t now_ms, std::uint32_t duration_ms) {
    std::lock_guard lock(mutex_);
    if (!active_ || !enabled_ || epoch != epoch_ || (seen_ && (sequence <= sequence_ || now_ms < issued_)) ||
        !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1 || std::abs(y) > 1 ||
        !duration_ms || duration_ms > maximum_duration_ms ||
        now_ms > (std::numeric_limits<std::uint64_t>::max)() - duration_ms) return false;
    const float length = std::hypot(x, y);
    if (length > 1) { x /= length; y /= length; }
    sequence_ = sequence;
    issued_ = now_ms;
    deadline_ = now_ms + duration_ms;
    x_ = x;
    y_ = y;
    accepted_ = seen_ = true;
    return true;
}
LeasedAxes InputLease::sample(std::uint64_t now_ms) {
    std::lock_guard lock(mutex_);
    if (!active_ || !enabled_ || !accepted_ || now_ms < issued_ || now_ms >= deadline_) {
        accepted_ = false;
        x_ = y_ = 0;
        return {};
    }
    return {x_, y_, true};
}
void InputLease::set_enabled(bool enabled) {
    std::lock_guard lock(mutex_);
    enabled_ = active_ && enabled;
    if (!enabled_) { accepted_ = false; x_ = y_ = 0; }
}
void InputLease::stop() {
    std::lock_guard lock(mutex_);
    active_ = enabled_ = accepted_ = false;
    x_ = y_ = 0;
    // Retain the generation and sequence barriers: late traffic cannot reopen it.
}
}
