#pragma once
#include "wsm/input_lease.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace wsm {
// Implemented by the engine adapter. A successful close guarantees that its
// control override is released; a failed/throwing operation may have had a
// partial effect and must be safe to retry on the verified engine thread.
class InputDriverBackend {
public:
    virtual ~InputDriverBackend() = default;
    virtual bool apply(float horizontal, float vertical, std::string& error) = 0;
    virtual bool close(std::string& error) = 0;
};

enum class InputDriverState { idle, active, cleanup_pending };
enum class InputNeutrality { unknown, neutral, nonzero };
struct InputDriverStatus {
    std::uint64_t epoch = 0;
    InputDriverState state = InputDriverState::idle;
    bool cleanup_pending = false;
    InputNeutrality neutrality = InputNeutrality::unknown;
    std::uint64_t nonzero_attempts = 0;
    std::string error;
};

// Process-lifetime owner contract: this object MUST outlive all producers and
// callback registrations and MUST NOT be destroyed until cleanup has actually
// succeeded (idle). Violation terminates rather than silently losing a backend.
// Only parent-verified engine/main thread may call either pump. The caller must
// arrange independent recovery pumps: deadlines cannot act while pumps stop.
// submit/stop/snapshot are thread-safe. stop only latches; an in-flight setter
// may finish after stop and is then immediately followed by cleanup in the pump.
// Reentrant/concurrent pumps are refused; backend calls occur without our mutex.
class InputDriver {
public:
    InputDriver() = default;
    ~InputDriver();
    InputDriver(const InputDriver&) = delete;
    InputDriver& operator=(const InputDriver&) = delete;
    // Ownership transfers only on success. Epochs must strictly increase.
    bool start(std::uint64_t epoch, std::int32_t remote_player,
               std::unique_ptr<InputDriverBackend>& backend);
    bool submit(std::uint64_t epoch, std::uint64_t sequence, float x, float y,
                std::uint64_t now_ms, std::uint32_t duration_ms);
    void stop();
    InputDriverStatus snapshot() const;
    bool event_tick(std::uint64_t now_ms, bool focused, bool lifecycle_valid) noexcept;
    // Recovery may zero/close but never apply nonzero, including with a live lease.
    bool recovery_tick(std::uint64_t now_ms) noexcept;
private:
    bool pump(std::uint64_t now_ms, bool event, bool focused, bool lifecycle_valid);
    void request_cleanup_locked();
    mutable std::mutex mutex_;
    InputLease lease_;
    std::unique_ptr<InputDriverBackend> backend_;
    InputDriverStatus status_;
    bool pumping_ = false;
};
}
