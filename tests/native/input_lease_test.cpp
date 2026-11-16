#include "wsm/input_lease.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

int main() {
    int failures = 0;
    auto check = [&](const char* name, bool ok) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    wsm::InputLease lease;
    check("reject primary player and invalid epoch", !lease.begin(1, 0) && !lease.begin(0, 1) && !lease.begin(1, 4));
    check("open remote lease", lease.begin(7, 1));
    check("reject replacement of active owner", !lease.begin(8, 2));
    check("no implicit movement", !lease.sample(100).live);
    check("accept fresh ordered axes", lease.submit(7, 1, 0.5f, -0.25f, 100, 120));
    const auto axes = lease.sample(101);
    check("leased remote axes delivered", axes.live && axes.horizontal == 0.5f && axes.vertical == -0.25f);
    check("stale and foreign traffic refused", !lease.submit(6, 2, 1, 0, 102, 120) &&
          !lease.submit(7, 1, 1, 0, 102, 120) && !lease.submit(7, 0, 1, 0, 102, 120));
    check("invalid traffic cannot extend deadline", !lease.submit(7, 2, std::numeric_limits<float>::quiet_NaN(), 0, 219, 120) &&
          !lease.submit(7, 2, 2, 0, 219, 120) && !lease.submit(7, 2, 0, 0, 219, 251) &&
          !lease.submit(7, 2, 0, 0, 219, 0));
    const auto expired = lease.sample(220);
    check("deadline neutralizes exactly at expiry", !expired.live && expired.horizontal == 0 && expired.vertical == 0);
    check("expiry retains sequence barrier", !lease.submit(7, 1, 1, 0, 221, 120));
    check("fresh input after expiry", lease.submit(7, 2, 1, 1, 221, 120));
    const auto diagonal = lease.sample(222);
    check("diagonal cannot increase speed", diagonal.live && std::abs(std::hypot(diagonal.horizontal, diagonal.vertical) - 1) < 0.0001f);
    lease.set_enabled(false);
    check("disabled owner rejects new input", !lease.submit(7, 3, 1, 0, 223, 120) && !lease.sample(223).live);
    lease.set_enabled(true);
    check("focus return cannot replay cached input", !lease.sample(224).live && !lease.submit(7, 2, 1, 0, 224, 120));
    check("fresh input after enabling", lease.submit(7, 3, -1, 0, 224, 120));
    check("clock reversal neutralizes permanently", !lease.sample(223).live && !lease.sample(225).live &&
          !lease.submit(7, 4, 1, 0, 223, 120));
    lease.stop();
    check("stop rejects in-flight and reused epoch", !lease.submit(7, 4, 1, 0, 226, 120) &&
          !lease.begin(7, 1) && !lease.sample(226).live);
    check("new epoch cannot receive old packets", lease.begin(8, 1) && !lease.submit(7, 100, 1, 0, 227, 120));
    check("reject deadline overflow", !lease.submit(8, 0, 1, 0, (std::numeric_limits<std::uint64_t>::max)() - 5, 120));
    check("new epoch accepts initial sequence zero", lease.submit(8, 0, 0, 0, 228, 120) && lease.sample(228).live);
    lease.stop();
    check("concurrent epoch opened", lease.begin(9, 1));
    std::atomic<bool> invalid{false};
    std::thread sender([&] { for (std::uint64_t i = 1; i <= 10000; ++i) lease.submit(9, i, 0.3f, 0.4f, 300, 120); });
    std::thread reader([&] { for (int i = 0; i < 10000; ++i) {
        const auto v = lease.sample(301);
        if (v.live && (v.horizontal != 0.3f || v.vertical != 0.4f)) invalid = true;
    }});
    sender.join(); reader.join();
    check("concurrent reads never publish a partial vector", !invalid && lease.sample(301).live);
    lease.stop();
    return failures ? 1 : 0;
}
