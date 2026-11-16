#pragma once
#include <cstdint>
#include <mutex>

namespace wsm {
struct LeasedAxes { float horizontal = 0, vertical = 0; bool live = false; };
// Thread-safe native input mailbox. Deadlines use the receiver's monotonic
// clock, never a remote timestamp. It contains no engine objects or callbacks.
class InputLease {
public:
    static constexpr std::uint32_t maximum_duration_ms = 250;
    bool begin(std::uint64_t epoch, std::int32_t remote_player);
    bool submit(std::uint64_t epoch, std::uint64_t sequence, float x, float y,
                std::uint64_t now_ms, std::uint32_t duration_ms);
    LeasedAxes sample(std::uint64_t now_ms);
    void set_enabled(bool enabled);
    void stop();
private:
    std::mutex mutex_;
    std::uint64_t epoch_ = 0, sequence_ = 0, issued_ = 0, deadline_ = 0;
    float x_ = 0, y_ = 0;
    bool active_ = false, enabled_ = false, accepted_ = false, seen_ = false;
};
}
