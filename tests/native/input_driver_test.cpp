#include "wsm/input_driver.h"
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace wsm;
namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
struct Record {
    float x = 0, y = 0;
    int applies = 0, nonzero = 0, closes = 0, destroyed = 0;
    bool fail_zero = false, fail_nonzero = false, fail_close = false;
    bool throw_nonzero = false, throw_close_after = false;
    std::function<void()> on_apply, on_close;
};
struct Backend final : InputDriverBackend {
    explicit Backend(Record& r) : r(r) {}
    ~Backend() override { ++r.destroyed; }
    bool apply(float x, float y, std::string& error) override {
        ++r.applies;
        const bool nz = x != 0 || y != 0;
        if (nz) ++r.nonzero;
        r.x = x; // model a partial first-axis setter before an error
        if (r.on_apply) r.on_apply();
        if (nz && r.throw_nonzero) throw std::runtime_error("second axis threw");
        if ((nz && r.fail_nonzero) || (!nz && r.fail_zero)) { error = "setter failed"; return false; }
        r.y = y;
        return true;
    }
    bool close(std::string& error) override {
        ++r.closes;
        if (r.on_close) r.on_close();
        if (r.throw_close_after) { r.x = r.y = 0; throw std::runtime_error("close uncertain"); }
        if (r.fail_close) { error = "close failed"; return false; }
        r.x = r.y = 0; return true;
    }
    Record& r;
};
Record* destruction_probe = nullptr;
void terminate_probe() { std::_Exit(destruction_probe && destruction_probe->destroyed == 0 ? 42 : 43); }
std::unique_ptr<InputDriverBackend> backend(Record& r) { return std::make_unique<Backend>(r); }
void stop_and_drain(InputDriver& d, std::uint64_t now) { d.stop(); d.recovery_tick(now); }
}
int main(int argc, char** argv) {
    // Separate process: violation must fail fast, before the owned backend dies.
    if (argc == 2 && std::strcmp(argv[1], "--pending-destructor") == 0) {
        Record r; destruction_probe = &r; std::set_terminate(terminate_probe);
        { InputDriver d; auto b = backend(r); if (!d.start(1, 1, b)) return 44; d.stop(); }
        return 45;
    }
    {
        InputDriver d; Record r; auto b = backend(r);
        check("start validates player and epoch without taking rejected backend", !d.start(0, 1, b) && !d.start(1, 0, b) && !d.start(1, 4, b) && b && d.start(1, 1, b) && !b);
        d.stop(); check("stop before first submit latches cleanup", d.snapshot().cleanup_pending && r.closes == 0);
        check("recovery closes stopped backend", d.recovery_tick(0) && r.closes == 1 && r.destroyed == 1 && d.snapshot().state == InputDriverState::idle);
    }
    {
        InputDriver d; Record r; auto b = backend(r); d.start(7, 2, b);
        check("valid diagonal accepted", d.submit(7, 1, 1, 1, 100, 120)); d.event_tick(101, true, true);
        check("diagonal normalized by actual driver", std::abs(std::hypot(r.x, r.y) - 1) < 0.0001f && r.nonzero == 1);
        check("reject replay old nonfinite range duration and overflow", !d.submit(6, 2, 1, 0, 102, 120) && !d.submit(7, 1, 1, 0, 102, 120) && !d.submit(7, 2, std::numeric_limits<float>::quiet_NaN(), 0, 102, 120) && !d.submit(7, 2, 2, 0, 102, 120) && !d.submit(7, 2, 1, 0, 102, 251) && !d.submit(7, 2, 1, 0, (std::numeric_limits<std::uint64_t>::max)(), 2));
        check("actual nonzero attempt status", d.snapshot().nonzero_attempts == 1 && d.snapshot().neutrality == InputNeutrality::nonzero);
        const int applied_before_recovery = r.applies;
        d.recovery_tick(219); check("recovery never reapplies live nonzero", r.nonzero == 1 && r.x != 0 && r.applies == applied_before_recovery);
        check("deadline does not autonomously touch backend without pump", r.x != 0);
        d.recovery_tick(220); check("recovery neutralizes exactly at deadline", r.x == 0 && r.y == 0 && r.nonzero == 1);
        check("fresh sequence after expiry", d.submit(7, 2, 1, 0, 221, 120)); d.event_tick(222, true, true);
        d.event_tick(220, true, true); check("backward clock neutralizes", r.x == 0 && !d.submit(7, 3, 1, 0, 220, 120));
        d.event_tick(223, true, true); check("backward clock cannot regenerate cached input", r.x == 0);
        check("fresh after backwards sample", d.submit(7, 3, 1, 0, 224, 120)); d.event_tick(224, true, true);
        d.event_tick(225, false, true); check("focus loss neutralizes and disables submit", r.x == 0 && !d.submit(7, 4, 1, 0, 225, 120));
        d.event_tick(226, true, true); check("focus return does not replay", r.x == 0 && !d.submit(7, 3, 1, 0, 226, 120));
        check("focus return accepts fresh generation", d.submit(7, 4, 0, 1, 227, 120)); d.event_tick(227, true, true);
        d.event_tick(228, true, false); check("invalid lifecycle stops and closes", r.x == 0 && r.y == 0 && r.closes == 1 && d.snapshot().state == InputDriverState::idle);
    }
    {
        InputDriver d; Record r; r.fail_nonzero = true; r.fail_zero = true; r.fail_close = true; auto b = backend(r); d.start(1, 1, b);
        d.submit(1, 1, 1, 1, 1, 100); check("partial setter failure visible", !d.event_tick(1, true, true) && d.snapshot().cleanup_pending && d.snapshot().neutrality == InputNeutrality::unknown && !d.snapshot().error.empty());
        check("both cleanup operations failed retain ownership", r.destroyed == 0 && r.closes == 1);
        auto next = backend(r); check("pending cleanup refuses new and stale epoch without consuming", !d.start(2, 2, next) && !d.start(1, 2, next) && next);
        r.fail_zero = false; r.fail_close = false; check("later recovery retries real cleanup", d.recovery_tick(2) && r.closes == 2 && r.destroyed == 1 && d.snapshot().state == InputDriverState::idle);
        check("old epoch remains refused, newer epoch accepted", !d.start(1, 1, next) && d.start(2, 3, next)); stop_and_drain(d, 3);
    }
    {
        InputDriver d; Record r; r.fail_zero = true; auto b = backend(r); d.start(1, 1, b); d.stop();
        check("zero failure close success releases backend", d.recovery_tick(1) && r.closes == 1 && r.destroyed == 1 && d.snapshot().neutrality == InputNeutrality::neutral);
    }
    {
        InputDriver d; Record r; r.throw_nonzero = true; r.throw_close_after = true; auto b = backend(r); d.start(1, 1, b);
        r.on_close = [&] { d.stop(); check("reentrant pump refused", !d.recovery_tick(1)); };
        d.submit(1, 1, 1, 1, 1, 100); check("thrown apply and uncertain close retained", !d.event_tick(1, true, true) && d.snapshot().cleanup_pending && r.destroyed == 0 && r.closes == 1);
        r.on_close = {}; r.throw_close_after = false; check("close throws after effect retried", d.recovery_tick(2) && r.closes == 2 && r.destroyed == 1);
    }
    {
        InputDriver d; Record r; auto b = backend(r); d.start(1, 1, b);
        bool reentered = false;
        r.on_apply = [&] { if (!reentered) { reentered = true; d.stop(); check("reentrant event refused", !d.event_tick(1, true, true)); } };
        d.submit(1, 1, 1, 0, 1, 10); d.event_tick(1, true, true);
        check("reentrant stop latches and drains after setter", r.x == 0 && r.closes == 1 && r.destroyed == 1 && d.snapshot().state == InputDriverState::idle);
    }
    {
        InputDriver d; Record r; auto b = backend(r); d.start(1, 1, b);
        std::mutex m; std::condition_variable cv; bool entered = false, stopped = false;
        r.on_apply = [&] {
            std::unique_lock lock(m);
            if (!entered) { entered = true; cv.notify_one(); cv.wait(lock, [&] { return stopped; }); }
        };
        std::thread stopper([&] {
            std::unique_lock lock(m); cv.wait(lock, [&] { return entered; });
            lock.unlock(); d.stop(); lock.lock(); stopped = true; lock.unlock(); cv.notify_one();
        });
        d.submit(1, 1, 1, 0, 1, 10); d.event_tick(1, true, true); stopper.join();
        check("concurrent stop during setter drains before pump returns", r.x == 0 && r.y == 0 && r.closes == 1 && r.destroyed == 1 && d.snapshot().state == InputDriverState::idle);
    }
    {
        InputDriver d; Record r; auto b = backend(r); d.start(1, 1, b); std::atomic<bool> go{true};
        std::thread sender([&] { for (std::uint64_t i = 1; i <= 30000; ++i) d.submit(1, i, .3f, .4f, 100, 100); go = false; });
        std::thread observer([&] { while (go) (void)d.snapshot(); d.stop(); });
        while (go) d.event_tick(101, true, true);
        sender.join(); observer.join(); d.recovery_tick(300);
        check("concurrent submit stop snapshot and single pump drain", d.snapshot().state == InputDriverState::idle && r.destroyed == 1 && r.x == 0 && r.y == 0);
    }
    std::printf("RESULT failures=%d\n", failures);
    return failures ? 1 : 0;
}
