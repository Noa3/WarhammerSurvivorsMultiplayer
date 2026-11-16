#include "wsm/input_driver.h"
#include <exception>
#include <utility>

namespace wsm {
namespace {
bool apply_safely(InputDriverBackend& backend, float x, float y, std::string& error) {
    try {
        if (backend.apply(x, y, error)) return true;
        if (error.empty()) error = "backend apply failed";
    } catch (const std::exception& e) {
        error = std::string("backend apply threw: ") + e.what();
    } catch (...) { error = "backend apply threw"; }
    return false;
}
bool close_safely(InputDriverBackend& backend, std::string& error) {
    try {
        if (backend.close(error)) return true;
        if (error.empty()) error = "backend close failed";
    } catch (const std::exception& e) {
        error = std::string("backend close threw: ") + e.what();
    } catch (...) { error = "backend close threw"; }
    return false;
}
}

InputDriver::~InputDriver() {
    // There is no safe engine call here and no safe place to discard ownership.
    if (backend_ || pumping_) std::terminate();
}
bool InputDriver::start(std::uint64_t epoch, std::int32_t player,
                        std::unique_ptr<InputDriverBackend>& backend) {
    std::lock_guard lock(mutex_);
    if (pumping_ || backend_ || !backend || !lease_.begin(epoch, player)) return false;
    backend_ = std::move(backend);
    status_.epoch = epoch;
    status_.state = InputDriverState::active;
    status_.cleanup_pending = false;
    status_.neutrality = InputNeutrality::unknown;
    status_.nonzero_attempts = 0;
    status_.error.clear();
    return true;
}
bool InputDriver::submit(std::uint64_t epoch, std::uint64_t sequence, float x, float y,
                         std::uint64_t now_ms, std::uint32_t duration_ms) {
    std::lock_guard lock(mutex_);
    return status_.state == InputDriverState::active && lease_.submit(epoch, sequence, x, y, now_ms, duration_ms);
}
void InputDriver::request_cleanup_locked() {
    if (!backend_) return;
    lease_.stop();
    status_.state = InputDriverState::cleanup_pending;
    status_.cleanup_pending = true;
}
void InputDriver::stop() {
    std::lock_guard lock(mutex_);
    request_cleanup_locked();
}
InputDriverStatus InputDriver::snapshot() const {
    std::lock_guard lock(mutex_);
    return status_;
}
bool InputDriver::event_tick(std::uint64_t now, bool focused, bool valid) noexcept {
    try { return pump(now, true, focused, valid); }
    catch (...) {
        // No exception may escape an engine callback. Preserve ownership for retry.
        try { std::lock_guard lock(mutex_); request_cleanup_locked(); pumping_ = false; status_.neutrality = InputNeutrality::unknown; status_.error = "driver exception"; } catch (...) { std::terminate(); }
        return false;
    }
}
bool InputDriver::recovery_tick(std::uint64_t now) noexcept {
    try { return pump(now, false, false, false); }
    catch (...) {
        try { std::lock_guard lock(mutex_); request_cleanup_locked(); pumping_ = false; status_.neutrality = InputNeutrality::unknown; status_.error = "driver exception"; } catch (...) { std::terminate(); }
        return false;
    }
}
bool InputDriver::pump(std::uint64_t now, bool event, bool focused, bool valid) {
    std::unique_lock lock(mutex_);
    if (pumping_) return false;
    if (!backend_) return true;
    pumping_ = true;
    if (event && !valid) request_cleanup_locked();
    if (event && status_.state == InputDriverState::active) lease_.set_enabled(focused);
    LeasedAxes axes;
    if (status_.state == InputDriverState::active) axes = lease_.sample(now);
    const bool nonzero = event && axes.live && (axes.horizontal != 0 || axes.vertical != 0);
    const bool apply = status_.state == InputDriverState::active &&
        (nonzero || ((!axes.live || event) && status_.neutrality != InputNeutrality::neutral));
    bool failed = false;
    if (apply) {
        auto* const target = backend_.get();
        // Unknown before calling: failure/exception may be a partial setter.
        status_.neutrality = InputNeutrality::unknown;
        if (nonzero) ++status_.nonzero_attempts;
        lock.unlock();
        std::string error;
        const bool ok = apply_safely(*target, nonzero ? axes.horizontal : 0, nonzero ? axes.vertical : 0, error);
        lock.lock();
        if (ok) status_.neutrality = nonzero ? InputNeutrality::nonzero : InputNeutrality::neutral;
        else { status_.error = std::move(error); failed = true; request_cleanup_locked(); }
    }
    if (status_.state == InputDriverState::cleanup_pending) {
        auto* const target = backend_.get();
        // Always try both: successful close releases ownership even if zero failed.
        status_.neutrality = InputNeutrality::unknown;
        lock.unlock();
        std::string zero_error, close_error;
        const bool zero_ok = apply_safely(*target, 0, 0, zero_error);
        const bool closed = close_safely(*target, close_error);
        lock.lock();
        if (!zero_ok) status_.error = std::move(zero_error);
        if (!closed) status_.error = std::move(close_error);
        if (closed) {
            status_.neutrality = InputNeutrality::neutral;
            status_.state = InputDriverState::idle;
            status_.cleanup_pending = false;
            auto retired = std::move(backend_);
            lock.unlock();
            retired.reset(); // no arbitrary destructor under the mutex
            lock.lock();
        } else {
            status_.neutrality = zero_ok ? InputNeutrality::neutral : InputNeutrality::unknown;
            failed = true;
        }
    }
    pumping_ = false;
    return !failed;
}
}
